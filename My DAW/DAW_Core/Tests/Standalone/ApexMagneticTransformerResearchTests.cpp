// Offline transformer nonlinearity/aliasing tests; NOT UAD validation.
#include "../../Source/CompressorCore/ApexMagneticTransformerResearchCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace APEX::Compressor;
constexpr double pi=3.14159265358979323846;
static void near(double a,double b,double tol) {
    if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>tol) {
        std::cerr<<"Difference "<<a<<" vs "<<b<<" (tol "<<tol<<")\n";
        std::abort();
    }
}
static double harmonic(const std::vector<double>& audio,int hz,int sr,int start) {
    double re=0,im=0;
    for(int n=start;n<static_cast<int>(audio.size());++n){
        const double phase=2*pi*hz*n/sr;
        re+=audio[n]*std::cos(phase);
        im+=audio[n]*std::sin(phase);
    }
    return 2*std::hypot(re,im)/(audio.size()-start);
}
static std::vector<double> render(int factor,double drive,int hz,int sr=48000) {
    ApexMagneticResearchSettings p;
    p.oversampling=factor;p.drive=drive;p.dcBias=0;p.fluxCoupling=0;
    ApexMagneticTransformerResearchCore c;c.prepare(sr,p);
    std::vector<double> output(sr);
    for(int n=0;n<sr;++n)
        output[n]=c.process(.85*std::sin(2*pi*hz*n/sr));
    return output;
}
int main(){
    static_assert(sizeof(ApexMagneticTanhLookup)<=2*sizeof(void*),
                  "Magnetic curve must be shared, never 16kB per instance");
    // Interpolation error must stay significantly below the measured
    // harmonic-alias differences. This is DSP math, not UAD calibration.
    {
        ApexMagneticTanhLookup table;table.prepare();
        double maxAbsoluteError=0;
        for(int i=-16000;i<=16000;++i) {
            const double input=i/1000.0;
            const double error=std::abs(table.evaluate(input)-std::tanh(input));
            maxAbsoluteError=std::max(maxAbsoluteError,error);
        }
        std::cout<<"magnetic tanh LUT max error: "<<maxAbsoluteError<<"\\n";
        assert(maxAbsoluteError<1.0e-5);
        near(table.evaluate(0),0,1.e-8);
        assert(table.evaluate(-250)==-1);
        assert(table.evaluate(250)==1);
    }
    for(int factor:{1,4}) {
        ApexMagneticTransformerResearchCore c;
        ApexMagneticResearchSettings p;p.oversampling=factor;
        c.prepare(48000,p);
        assert(c.oversamplingFactor()==factor);
        assert(c.latencySamples()==(factor==1?0:16));
        for(int n=0;n<2048;++n)
            assert(std::isfinite(c.process(std::sin(n*.025))));
        c.reset();
        near(c.fluxState(),0,0);
    }
    // Third harmonic of 10k folds from 30k to 18k at native 48k.
    const auto linear=render(4,0,10000);
    const auto raw=render(1,2.7,10000);
    const auto filtered=render(4,2.7,10000);
    const double cleanAlias=harmonic(linear,18000,48000,24000);
    const double folded=harmonic(raw,18000,48000,24000);
    const double reduced=harmonic(filtered,18000,48000,24000);
    std::cout<<"alias 10k->18k: clean="<<cleanAlias<<
               " x1="<<folded<<" x4="<<reduced<<"\n";
    assert(folded>.001 && cleanAlias<folded*.1);
    assert(reduced<folded*.65); // demonstrable reduction, not alias-free
    // 44.1 kHz must be a first-class recording rate, not an invalid-input
    // smoke check. 10 kHz -> third harmonic at 30 kHz folds to 14.1 kHz.
    {
        constexpr int rate=44100;
        constexpr int foldedHz=rate-30000;
        const auto unfiltered44=render(1,2.7,10000,rate);
        const auto filtered44=render(4,2.7,10000,rate);
        const double alias1x=harmonic(unfiltered44,foldedHz,rate,rate/2);
        const double alias4x=harmonic(filtered44,foldedHz,rate,rate/2);
        std::cout<<"44.1k alias 10k->14.1k: x1="<<alias1x
                 <<" x4="<<alias4x<<"\\n";
        assert(alias1x>.001);
        assert(alias4x<alias1x*.65);
        ApexMagneticResearchSettings settings;
        settings.drive=0;
        settings.oversampling=4;
        ApexMagneticTransformerResearchCore impulse44;
        impulse44.prepare(rate,settings);
        assert(impulse44.latencySamples()==16);
        double peak44=0;
        int peakAt=-1;
        for(int n=0;n<96;++n) {
            const double v=std::abs(impulse44.process(n==0?1.0:0.0));
            if(v>peak44) { peak44=v;peakAt=n; }
        }
        assert(peak44>.05);
        assert(std::abs(peakAt-16)<=2);
    }
    const double fundamental=harmonic(render(4,0,1000),1000,48000,24000);
    assert(fundamental>.7&&fundamental<1.1);

    // Linear FIR impulse shows unavoidable group delay; this cannot be
    // silently labelled zero-latency in a realtime DAW insert.
    ApexMagneticResearchSettings clean;
    clean.drive=0;clean.oversampling=4;
    ApexMagneticTransformerResearchCore impulse;impulse.prepare(48000,clean);
    double peak=0;int peakSample=-1;
    for(int i=0;i<96;++i){
        const double v=std::abs(impulse.process(i==0?1.0:0.0));
        if(v>peak){peak=v;peakSample=i;}
    }
    assert(peak>.05);
    assert(std::abs(peakSample-impulse.latencySamples())<=2);

    // Invalid controls/audio and multiple sample rates never emit NaNs.
    for(double sr:{44100.,48000.,96000.,192000.}) {
        ApexMagneticResearchSettings p;
        p.drive=INFINITY;p.dcBias=NAN;p.fluxMemoryHz=-1;
        p.fluxCoupling=-INFINITY;p.trim=NAN;
        ApexMagneticTransformerResearchCore c;c.prepare(sr,p);
        for(double value:{std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(),-1.,4.})
            for(int i=0;i<500;++i)
                assert(std::isfinite(c.process(value)));
    }
    // Power-of-two history rings must reset identically after arbitrary
    // previous audio. This also checks stale sample history cannot leak.
    {
        ApexMagneticTransformerResearchCore c;
        c.prepare(48000);
        std::array<double,257> before{},after{};
        for(int n=0;n<257;++n)
            before[static_cast<std::size_t>(n)]=
                c.process(.35*std::sin(n*.071)+.13*std::cos(n*.193));
        for(int n=0;n<601;++n)c.process(.21*std::sin(n*.039));
        c.reset();
        for(int n=0;n<257;++n)
            after[static_cast<std::size_t>(n)]=
                c.process(.35*std::sin(n*.071)+.13*std::cos(n*.193));
        for(int n=0;n<257;++n)
            near(before[static_cast<std::size_t>(n)],
                 after[static_cast<std::size_t>(n)],1.e-13);
    }
    // Informational speed baseline, not a host CPU acceptance measurement.
    ApexMagneticTransformerResearchCore c;c.prepare(48000);
    volatile double checksum=0;
    const auto start=std::chrono::steady_clock::now();
    for(int n=0;n<96000;++n)
        checksum=checksum+c.process(.2*std::sin(n*.011));
    const auto stop=std::chrono::steady_clock::now();
    const auto millis=std::chrono::duration<double,std::milli>(stop-start).count();
    std::cout<<"x4 single-channel 2sec/48k research processing: "<<millis
             <<" ms; checksum "<<checksum<<"\n";
    assert(millis<10000.0);
    std::cout<<"APEX magnetic oversampling & latency TESTS GREEN, no 1:1 claim\n";
}
