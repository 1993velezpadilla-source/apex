#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// Actual DspCore PDC identity and block bound check.
// This catches stereo clock skew, wrong dry delay, host oversized chunks,
// and sample-rate regressions independent of a live audio interface.
static bool probe(double sampleRate,int preparedBlock,int hostBlock,int channels) {
    constexpr double pi=3.14159265358979323846;
    const int length=static_cast<int>(sampleRate*0.22);
    std::vector<float> left(static_cast<std::size_t>(length)),right(static_cast<std::size_t>(length));
    for(int i=0;i<length;++i) {
        left[static_cast<std::size_t>(i)] =
            static_cast<float>(0.25*std::sin(2*pi*137.0*i/sampleRate)
                                +0.08*std::sin(2*pi*811.0*i/sampleRate));
        right[static_cast<std::size_t>(i)] =
            static_cast<float>(0.18*std::sin(2*pi*221.0*i/sampleRate)
                                +0.11*std::sin(2*pi*511.0*i/sampleRate));
    }
    const auto originalLeft=left,originalRight=right;
    APEX::TuneLive::DspCore dsp;
    dsp.prepare(sampleRate,preparedBlock,channels);
    APEX::TuneLive::DspSettings settings;
    settings.bypass=true;
    for(int start=0;start<length;start+=hostBlock) {
        float* audio[2]={left.data()+start,
                         channels==2?right.data()+start:nullptr};
        dsp.process(audio,channels,std::min(hostBlock,length-start),settings);
    }
    double worst=0.0;
    const int latency=dsp.latencySamples();
    for(int i=latency+128;i<length;++i) {
        const int delayed=i-latency;
        const double l=left[static_cast<std::size_t>(i)]
                        -originalLeft[static_cast<std::size_t>(delayed)];
        worst=std::max(worst,std::abs(l));
        if(channels==2) {
            const double r=right[static_cast<std::size_t>(i)]
                            -originalRight[static_cast<std::size_t>(delayed)];
            worst=std::max(worst,std::abs(r));
        }
    }
    std::cout<<"rate="<<sampleRate<<" prepared="<<preparedBlock
             <<" host="<<hostBlock<<" channels="<<channels
             <<" PDC="<<latency<<" worst_error="<<worst<<"\n";
    return latency>0 && latency<length/2 && worst<1.0e-5;
}
int main() {
    bool pass=true;
    pass&=probe(44100.0,64,64,1);
    pass&=probe(48000.0,128,128,1);
    pass&=probe(48000.0,128,257,2); // oversized callback must chunk safely
    pass&=probe(48000.0,64,64,2);
    pass&=probe(48000.0,256,256,2);
    pass&=probe(96000.0,128,128,1);
    pass&=probe(96000.0,256,256,2);
    if(!pass) {std::cerr<<"FAIL: bypass PDC/host block matrix\n";return 1;}
    std::cout<<"PASS: cross-platform PDC, mono/stereo, oversized block matrix\n";
}
