// APEX Tune Live — real 40ms production DSP vocal-to-fricative-to-silence.
//
// Producer singing scenario: a sustained detuned vowel enters a broadband
// unpitched consonant/breath, then silence. The shifter must not keep
// pitch-modulating the hiss once the pitch detector reports unvoiced.
//
// Check actual wet output against sample-exact PDC dry *AFTER* settling.
// The artist-approved HQ40 algorithm/GUI is NOT modified by this test.
// C++17, deterministic input, no plugin-side allocations in process().
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr int rate=48000,nframes=rate*3,block=128,latency=1920;
constexpr double pi=3.1415926535897932384626433832795;
constexpr int pitchedEnd=rate*115/100;
constexpr int hissEnd=rate*225/100;

bool run(int channels) {
    using namespace APEX::TuneLive;
    std::vector<float> originalL(nframes),originalR(nframes),left,right;
    std::uint32_t seed=0x41504558u;
    double phase=0.0;
    for(int i=0;i<nframes;++i) {
        float sample=0;
        if(i<pitchedEnd) {
            const double frequency=220.0*std::pow(2.0,30.0/1200.0);
            phase+=2.0*pi*frequency/rate;
            sample=static_cast<float>(0.29*std::sin(phase)+
                                       0.07*std::sin(phase*2.0));
        } else if(i<hissEnd) {
            // Broadband, aperiodic pseudo-'s', not a genuine spoken
            // consonant. A repeatable high-frequency transient proxy.
            seed=1664525u*seed+1013904223u;
            const double white=static_cast<std::int32_t>(seed)/2147483648.0;
            sample=static_cast<float>(0.16*white);
        }
        originalL[static_cast<std::size_t>(i)]=sample;
        originalR[static_cast<std::size_t>(i)]=0.76f*sample;
    }
    left=originalL;right=originalR;
    DspCore core;
    core.prepare(rate,block,channels);
    DspSettings user;
    user.correction=1.0f;user.retune=0.0f;
    user.humanize=0.0f;user.flexTune=0.0f;
    user.wet=1.0f;user.preserveFormants=true;
    const std::array<int,7> callbackSizes{{1,7,17,64,128,257,521}};
    std::size_t callback=0;
    float vowelWet=-1.0f,lateHissWet=-1.0f;
    for(int begin=0;begin<nframes;) {
        const int end=begin<pitchedEnd ? pitchedEnd :
                      begin<hissEnd ? hissEnd : nframes;
        const int n=std::min({callbackSizes[callback%callbackSizes.size()],
                              end-begin,nframes-begin});
        float* ptr[2]={left.data()+begin,
                       channels==2?right.data()+begin:nullptr};
        core.process(ptr,channels,n,user);
        begin+=n;++callback;
        if(begin>=rate && begin<pitchedEnd)
            vowelWet=core.debugWetAmount();
        if(begin>=rate*21/10 && begin<hissEnd)
            lateHissWet=core.debugWetAmount();
    }
    double maxHissError=0, maxSilence=0, peak=0;
    for(int i=0;i<nframes;++i) {
        const double sample=left[static_cast<std::size_t>(i)];
        const double other=channels==2 ? right[static_cast<std::size_t>(i)] : sample;
        if(!std::isfinite(sample)||!std::isfinite(other))return false;
        peak=std::max({peak,std::abs(sample),std::abs(other)});
        // At least 550ms after transition: aperiodic breath/hiss should
        // be aligned dry. Do not require hard cuts on the note transition!
        if(i>=rate*17/10 && i<rate*21/10) {
            maxHissError=std::max(maxHissError,std::abs(
              sample-originalL[static_cast<std::size_t>(i-latency)]));
            if(channels==2)
                maxHissError=std::max(maxHissError,std::abs(
                    other-originalR[static_cast<std::size_t>(i-latency)]));
        }
        if(i>=rate*25/10)
            maxSilence=std::max({maxSilence,std::abs(sample),std::abs(other)});
    }
    std::cout<<"TUNE_LIVE_REAL_VOWEL_HISS_RETURN"
             <<" channels="<<channels
             <<" variable_callbacks="<<callback
             <<" vowel_effective_wet="<<vowelWet
             <<" late_hiss_effective_wet="<<lateHissWet
             <<" max_aligned_late_hiss_error="<<maxHissError
             <<" max_after_silence="<<maxSilence
             <<" peak="<<peak
             <<" PDC="<<core.latencySamples()<<"\\n";
    return core.latencySamples()==latency && peak>0.02 && peak<2.0
        && vowelWet>0.50f && lateHissWet<0.005f
        && maxHissError<0.0002 && maxSilence<0.0002;
}
}
int main() {
    if(!run(1)||!run(2)) {
        std::cerr<<"FAIL: production HQ40 does not return to dry after "
                    "unvoiced fricative/hiss or silence\\n";
        return 1;
    }
    std::cout<<"TUNE_LIVE_UNVOICED_ARTICULATION_RETURN_PASS"
                " real HQ40 mono+stereo, 1..521 irregular host callbacks\\n";
}
