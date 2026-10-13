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
static std::vector<double> render(int factor,double drive,int hz) {
    constexpr int sr=48000;
    ApexMagneticResearchSettings p;
    p.oversampling=factor;p.drive=drive;p.dcBias=0;p.fluxCoupling=0;
    ApexMagneticTransformerResearchCore c;c.prepare(sr,p);
    std::vector<double> output(sr);
    for(int n=0;n<sr;++n)
        output[n]=c.process(.85*std::sin(2*pi*hz*n/sr));
    return output;
}
int main(){
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
