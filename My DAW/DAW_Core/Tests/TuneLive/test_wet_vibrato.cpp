// Regression for actual WET vocal vibrato, not merely VoiceControl targets.
// Quantify how much 5-Hz ±25-cent natural vibrato survives the real stereo-
// capable Signalsmith shifter with singer-focused Humanize/Flex Tune.
// A clean synthesized vibrato still does not certify human vocal timbre.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

struct Variance {
    double sum=0, squared=0;
    int count=0;
    void push(double cents) {
        sum+=cents;squared+=cents*cents;++count;
    }
    double rmsVariance() const {
        if(count<=1)return 0;
        const double mean=sum/count;
        return std::sqrt(std::max(0.0,squared/count-mean*mean));
    }
};
int main() {
    constexpr double sampleRate=48000;
    constexpr int length=3*48000,block=128;
    constexpr double pi=3.14159265358979323846;
    std::vector<float> original(static_cast<std::size_t>(length));
    double phase=0.0;
    for(int i=0;i<length;++i){
        const double t=i/sampleRate;
        // The +30 cents centre shift forces a genuinely processed WET vocal.\n        // With zero detune the transparency gate produces DRY=100%,\n        // which would invalidate the vibrato preservation experiment.\n        const double cents=30.0+25.0*std::sin(2*pi*5*t);
        const double hz=440.0*std::pow(2.0,cents/1200.0);
        phase+=2*pi*hz/sampleRate;
        original[static_cast<std::size_t>(i)]
           =static_cast<float>(0.38*std::sin(phase));
    }

    auto wet=original;
    APEX::TuneLive::DspCore dsp;
    dsp.prepare(sampleRate,block,1);
    APEX::TuneLive::DspSettings settings;
    settings.correction=0.85f;
    settings.retune=0.20f; // 30.8 ms, natural performance
    settings.humanize=0.90f;
    settings.flexTune=0.10f;
    settings.stability=0.65f;
    settings.wet=1.0f;

    for(int start=0;start<length;start+=block) {
        float* mono[1]={wet.data()+start};
        dsp.process(mono,1,std::min(block,length-start),settings);
    }
    APEX::TuneLive::PitchTracker dryTracker,wetTracker;
    dryTracker.prepare(sampleRate);wetTracker.prepare(sampleRate);
    Variance dryVib,wetVib;
    int dryUnvoiced=0,wetUnvoiced=0;
    const int latency=dsp.latencySamples();
    for(int i=0;i<length;++i) {
        const float dry=original[static_cast<std::size_t>(i)];
        const float processed=wet[static_cast<std::size_t>(i)];
        if(!std::isfinite(processed)) {
            std::cerr<<"FAIL: nonfinite processed vibrato\n";return 1;
        }
        const bool hasDry=dryTracker.push(dry);
        const bool hasWet=wetTracker.push(processed);
        if(i<length/2)continue;
        if(hasDry) {
            const auto p=dryTracker.latest();
            if(p.voiced)dryVib.push(1200.0*std::log2(p.hz/440.0));
            else ++dryUnvoiced;
        }
        if(hasWet) {
            const auto p=wetTracker.latest();
            if(p.voiced)wetVib.push(1200.0*std::log2(p.hz/440.0));
            else ++wetUnvoiced;
        }
    }
    const double a=dryVib.rmsVariance(), b=wetVib.rmsVariance();
    const double ratio=b/std::max(0.01,a);
    std::cout<<"natural_vibrato_5Hz dry_sigma_cents="<<a
             <<" wet_sigma_cents="<<b
             <<" preservation_ratio="<<ratio
             <<" samples="<<wetVib.count
             <<" unvoiced_dry="<<dryUnvoiced
             <<" unvoiced_wet="<<wetUnvoiced
             <<" plugin_PDC="<<latency\n             <<" final_effective_wet="<<dsp.debugWetAmount()<<"\n";
    if(dryVib.count<100 || wetVib.count<100 || a<6
       || ratio<0.50 || ratio>1.40 || wetUnvoiced>dryUnvoiced+3\n       || dsp.debugWetAmount()<0.50f){
        std::cerr<<"FAIL: singer's natural 5-Hz vibrato was destroyed or exaggerated\n";
        return 1;
    }
    std::cout<<"PASS: active wet pitch correction retains natural 5Hz vibrato\n";
}
