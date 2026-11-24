// Offline comparative A/B: same real harmonic SOURCE waveform through
// standard YIN authority vs research-only early-onset seed. Checks ACTUAL
// tracked shifter wet activation and final processed F0, not plugin PDC.
// This does not prove performance on human singers or noisy microphone input.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveTrackedPrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
constexpr int block=128;
APEX::TuneLive::VoiceSettings settings() {
    APEX::TuneLive::VoiceSettings v;
    v.root=9;v.scale=0;v.correction=1.0f;v.referenceHz=440.0f;
    v.humanize=0.0f;v.flexTune=0.0f;v.noteHysteresis=0.1f;
    return v;
}
struct Measurement {
    double voicedMs=-1.0,firstWet50Ms=-1.0,cents=9999.0;
    bool finite=true,voiced=false;
    int64_t seeded=-1;
};
Measurement measure(const std::vector<float>& input,double sr,double target,
                    bool fast) {
    using APEX::TuneLive::Research::TrackedPrototype;
    TrackedPrototype engine;
    engine.prepare(sr);
    engine.setVoiceSettings(settings());
    engine.enableExperimentalEarlyOnset(fast);
    std::vector<float> output=input;
    Measurement m;
    for(int i=0;i<static_cast<int>(output.size());i+=block) {
        const int count=std::min(block,static_cast<int>(output.size())-i);
        float* mono[1]={output.data()+i};
        engine.process(mono,1,count);
        if(m.firstWet50Ms<0.0 && engine.researchWetMix()>=0.5)
            m.firstWet50Ms=1000.0*(i+count-1)/sr;
    }
    m.seeded=engine.firstEarlySample();
    m.voiced=engine.voiced();
    m.voicedMs=1000.0*engine.firstVoicedSample()/sr;
    APEX::TuneLive::PitchTracker outputPitch;
    outputPitch.prepare(sr);
    const int frames=static_cast<int>(output.size());
    for(int i=frames-static_cast<int>(sr*0.4);i<frames;++i) {
        const float s=output[static_cast<std::size_t>(i)];
        m.finite &= std::isfinite(s);
        outputPitch.push(std::isfinite(s)?s:0.0f);
    }
    const auto found=outputPitch.latest();
    if(found.voiced && found.hz>0)
        m.cents=1200.0*std::log2(found.hz/target);
    return m;
}
}
int main(){
    bool safe=true;
    int better=0;
    for(double sr:std::array<double,2>{44100.0,48000.0})
      for(double target:std::array<double,2>{220.0,440.0})
        for(double detune:std::array<double,2>{-30.0,30.0}){
        const double hz=target*std::pow(2.0,detune/1200.0);
        const int frames=static_cast<int>(sr*1.0);
        std::vector<float> voice(static_cast<std::size_t>(frames));
        for(int i=0;i<frames;++i){
            const double t=i/sr;
            voice[static_cast<std::size_t>(i)]=static_cast<float>(
                0.29*std::sin(2*pi*hz*t)
                +0.08*std::sin(2*pi*hz*2*t+0.27)
                +0.035*std::sin(2*pi*hz*3*t+0.16));
        }
        const auto baseline=measure(voice,sr,target,false);
        const auto rapid=measure(voice,sr,target,true);
        const bool pass=baseline.finite && rapid.finite &&
             baseline.voiced && rapid.voiced &&
             rapid.seeded>=0 && baseline.seeded<0 &&
             rapid.voicedMs>0 && rapid.voicedMs<baseline.voicedMs &&
             rapid.firstWet50Ms>0 &&
             rapid.firstWet50Ms<baseline.firstWet50Ms &&
             std::abs(rapid.cents)<=18.0 &&
             std::abs(baseline.cents)<=18.0;
        better+=pass;
        safe &= pass;
        std::cout<<"FAST_TRACKED_AB sr="<<sr<<" target="<<target
                 <<" detune_cents="<<detune
                 <<" baseline_first_f0_ms="<<baseline.voicedMs
                 <<" fast_first_f0_ms="<<rapid.voicedMs
                 <<" baseline_first_wet50_ms="<<baseline.firstWet50Ms
                 <<" fast_first_wet50_ms="<<rapid.firstWet50Ms
                 <<" baseline_final_output_cents="<<baseline.cents
                 <<" fast_final_output_cents="<<rapid.cents
                 <<" faster_and_accurate="<<pass<<"\n";
    }
    std::cout<<"FAST_TRACKED_RESEARCH successful="<<better
             <<"/8 (no shipping mode, human vocal/PDC/CPU quality unproven)\n";
    return safe && better==8?0:1;
}
