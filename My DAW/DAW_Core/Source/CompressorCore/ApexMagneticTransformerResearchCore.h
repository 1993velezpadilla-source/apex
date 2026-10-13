#pragma once
// Original experimental transformer color module, NOT calibrated hardware.
// R/L electrical transfer + flux-history transfer + FIR 4x oversampling.
// 65-tap interpolation/decimation: 16 native samples explicit FIR delay.
// Research only. No dynamic allocations, locks or coefficient work in callback.
#include "ApexAnalogCircuitResearchCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace APEX::Compressor {
struct ApexMagneticResearchSettings {
    ApexTransformerCircuit electrical{};
    double drive=1.3, dcBias=0.045, fluxMemoryHz=24, fluxCoupling=.18, trim=1;
    int oversampling=4;
};
class ApexMagneticTransformerResearchCore final {
public:
    void prepare(double fs,ApexMagneticResearchSettings p={}) noexcept {
        fs_=std::isfinite(fs)&&fs>=8000&&fs<=768000?fs:48000;
        p.drive=safe(p.drive,0,12,1.3);
        p.dcBias=safe(p.dcBias,-.5,.5,.045);
        p.fluxMemoryHz=safe(p.fluxMemoryHz,.1,400,24);
        p.fluxCoupling=safe(p.fluxCoupling,0,1,.18);
        p.trim=safe(p.trim,0,4,1);
        p.oversampling=p.oversampling==1?1:4;
        p_=p;
        electrical_.prepare(fs_,p.electrical);
        fluxPole_=std::exp(-2*pi*p.fluxMemoryHz/(fs_*p.oversampling));
        const double dc=std::tanh(p.drive*p.dcBias);
        const double derivative=p.drive*(1-dc*dc);
        linearNorm_=derivative>1.e-8?1/derivative:1;
        buildFilter();
        reset();
    }
    void reset() noexcept {
        electrical_.reset();
        flux_=0;
        input_.fill(0);
        high_.fill(0);
        inHead_=highHead_=0;
    }
    double process(double sample) noexcept {
        const double x=electrical_.process(sample);
        if(p_.oversampling==1)return p_.trim*shape(x);
        inHead_=(inHead_+1)&(input_.size()-1);
        input_[inHead_]=x;
        for(std::size_t phase=0;phase<4;++phase) {
            double v=0;
            for(std::size_t m=0;m<phaseSize_[phase];++m)
                v+=polyphase_[phase][m]*
                   input_[(inHead_+input_.size()-m)&(input_.size()-1)];
            highHead_=(highHead_+1)&(high_.size()-1);
            high_[highHead_]=shape(v);
        }
        double out=0;
        for(std::size_t k=0;k<fir_.size();++k)
            out+=fir_[k]*high_[(highHead_+high_.size()-k)&(high_.size()-1)];
        return p_.trim*out;
    }
    int latencySamples() const noexcept {return p_.oversampling==4?16:0;}
    int oversamplingFactor() const noexcept {return p_.oversampling;}
    double fluxState() const noexcept {return flux_;}
private:
    static constexpr double pi=3.14159265358979323846;
    static double safe(double x,double lo,double hi,double fallback) noexcept {
        return std::isfinite(x)?std::clamp(x,lo,hi):fallback;
    }
    double shape(double x) noexcept {
        x=safe(x,-24,24,0);
        flux_=fluxPole_*flux_+(1-fluxPole_)*x;
        if(std::abs(flux_)<1.e-15)flux_=0;
        if(p_.drive==0)return x;
        const double offset=p_.dcBias+p_.fluxCoupling*flux_;
        return std::clamp((std::tanh(p_.drive*(x+offset))-
                           std::tanh(p_.drive*offset))*linearNorm_,-16.,16.);
    }
    void buildFilter() noexcept {
        constexpr double cutoff=.115;
        double sum=0;
        for(std::size_t k=0;k<65;++k) {
            const double t=static_cast<double>(k)-32;
            const double sinc=t==0?2*cutoff:std::sin(2*pi*cutoff*t)/(pi*t);
            const double n=static_cast<double>(k)/64;
            const double blackman=.42-.5*std::cos(2*pi*n)+.08*std::cos(4*pi*n);
            fir_[k]=sinc*blackman;sum+=fir_[k];
        }
        for(auto& v:fir_)v/=sum;
        for(std::size_t phase=0;phase<4;++phase) {
            phaseSize_[phase]=0;
            for(std::size_t k=phase;k<65;k+=4)
                polyphase_[phase][phaseSize_[phase]++]=4*fir_[k];
        }
    }
    ApexTransformerResearchCore electrical_;
    ApexMagneticResearchSettings p_{};
    double fs_=48000,fluxPole_=0,linearNorm_=1,flux_=0;
    // Power-of-two circular storage avoids variable-divisor modulo per tap.
    // FIR coefficients/tap counts are unchanged: 65 high-rate and <=17
    // low-rate taps, so output/latency and 4x spectral response are preserved.
    std::array<double,65> fir_{};
    std::array<double,128> high_{};
    std::array<double,32> input_{};
    std::array<std::array<double,17>,4> polyphase_{};
    std::array<std::size_t,4> phaseSize_{};
    std::size_t inHead_=0,highHead_=0;
};
} // namespace APEX::Compressor
