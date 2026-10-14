// Measure a short-window onset candidate against the production full YIN
// tracker. Research only: a correct synthetic F0 is NOT sufficient evidence
// for early locking onto breathy, polyphonic or human sung notes.
#include "../../Source/TuneLiveCore/ApexTuneLivePitchTracker.h"
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveEarlyOnsetProbe.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
namespace {
constexpr double pi=3.14159265358979323846;
float waveform(double sampleRate,double frequency,int sample) {
    const double angle=2.0*pi*frequency*sample/sampleRate;
    return static_cast<float>(0.29*std::sin(angle)
                            +0.08*std::sin(2.0*angle+0.27)
                            +0.035*std::sin(3.0*angle+0.16));
}
}
int main(){
    using APEX::TuneLive::PitchTracker;
    using APEX::TuneLive::Research::EarlyOnsetProbe;
    bool safe=true;
    int accurate=0;
    for(double sr:std::array<double,3>{44100.0,48000.0,96000.0})
      for(double target:std::array<double,2>{220.0,440.0})
        for(double cents:std::array<double,2>{-30.0,30.0}) {
        const double hz=target*std::pow(2.0,cents/1200.0);
        EarlyOnsetProbe early;
        PitchTracker full;
        early.prepare(sr);full.prepare(sr);
        int64_t firstFull=-1;
        const int frames=static_cast<int>(sr*0.45);
        int falseEarly=0;
        for(int i=0;i<frames;++i){
            const float sample=waveform(sr,hz,i);
            if(early.push(sample) && early.latest().voiced) {
                if(std::abs(1200.0*std::log2(early.latest().hz/hz))>35.0)
                    ++falseEarly;
            }
            if(full.push(sample) && full.latest().voiced && firstFull<0)
                firstFull=i+1;
        }
        const auto pitch=early.latest();
        const double centsError=pitch.voiced
                    ?1200.0*std::log2(pitch.hz/hz):9999.0;
        const double earlyMs=1000.0*early.firstVoicedSample()/sr;
        const double fullMs=1000.0*firstFull/sr;
        const bool passed=pitch.voiced && firstFull>0
            && early.firstVoicedSample()>0
            && early.firstVoicedSample()<firstFull
            && falseEarly==0 && std::abs(centsError)<12.0;
        safe &= passed;
        accurate+=passed;
        std::cout<<"EARLY_ONSET sr="<<sr<<" source_hz="<<hz
                 <<" first_early_ms="<<earlyMs<<" first_full_ms="<<fullMs
                 <<" error_cents="<<centsError
                 <<" false_early_frames="<<falseEarly
                 <<" faster_than_full="<<(early.firstVoicedSample()<firstFull)
                 <<" candidate_pass="<<passed<<"\n";
    }
    // Intentional hard-negative set: silence, white noise and transients
    // must not get falsely promoted to a stable voiced estimate.
    for(int scenario=0;scenario<3;++scenario) {
        EarlyOnsetProbe onset;
        onset.prepare(48000.0);
        uint32_t state=0x41504558u;
        int falseVoiced=0;
        for(int i=0;i<48000;++i){
            state=state*1664525u+1013904223u;
            const double noise=static_cast<int32_t>(state)/2147483648.0;
            float sample=0.0f;
            if(scenario==1) sample=static_cast<float>(0.10*noise);
            if(scenario==2) {
                const double impulse=i%4800==0?0.65:0.0;
                sample=static_cast<float>(impulse+0.035*noise);
            }
            if(onset.push(sample)&&onset.latest().voiced)++falseVoiced;
        }
        const bool passed=falseVoiced==0 && !onset.latest().voiced;
        safe &= passed;
        std::cout<<"EARLY_ONSET_NEGATIVE scenario="<<scenario
                 <<" false_voiced_frames="<<falseVoiced
                 <<" passed="<<passed<<"\n";
    }
    std::cout<<"EARLY_ONSET_RESEARCH accepted="<<accurate<<"/12"
             <<" hard_negative_accepted="<<safe
             <<" (no host mode, no new live PDC, no human vowel certification)\n";
    return safe && accurate==12?0:1;
}
