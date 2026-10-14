// Streaming causal research gauntlet: changing pitch, vibrato, consonant
// surrogates and silence. It deliberately uses the actual YIN detector,
// NOT the oracle-F0 source supplied to the original feasibility test.
// All sound-quality metrics are diagnostic; finite / tracking / recovery
// violations are strict failures. Not a singer listening or latency proof.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveTrackedPrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr double kPi=3.14159265358979323846;
constexpr double kRate=48000.0;
using APEX::TuneLive::Research::TrackedPrototype;
APEX::TuneLive::VoiceSettings voice(float humanize=0.0f) {
    APEX::TuneLive::VoiceSettings p;
    p.root=9; p.scale=0; p.correction=1.0f;
    p.referenceHz=440.0f; p.humanize=humanize; p.flexTune=0.0f;
    p.noteHysteresis=0.1f; p.hopMilliseconds=5.333f;
    return p;
}
float synth(double phase,int harmonicCount=8) {
    double v=0.27*std::sin(phase);
    for(int h=2;h<=harmonicCount;++h)
        v+=(0.085/h)*std::sin(h*phase+0.13*h);
    return static_cast<float>(v);
}
struct Stats {bool finite=true;double peak=0.0,rms=0.0,jump=0.0;};
Stats signalStats(const std::vector<float>& samples) {
    Stats s;
    double square=0.0;
    float prior=0.0f;
    for(float f:samples) {
        s.finite &= std::isfinite(f);
        if(!std::isfinite(f))continue;
        s.peak=std::max(s.peak,std::abs(static_cast<double>(f)));
        s.jump=std::max(s.jump,std::abs(static_cast<double>(f-prior)));
        square+=static_cast<double>(f)*f;
        prior=f;
    }
    s.rms=std::sqrt(square/std::max<std::size_t>(1,samples.size()));
    return s;
}
}
int main() {
    bool pass=true;
    // Continuous integrated phase, not concatenated incompatible tone
    // segments. A glide must reacquire its final F0 without corrupting output.
    {
        TrackedPrototype engine;
        engine.prepare(kRate);
        engine.setVoiceSettings(voice());
        constexpr int frames=static_cast<int>(kRate*1.6);
        std::vector<float> output(static_cast<std::size_t>(frames));
        double phase=0.0;
        for(int i=0;i<frames;++i) {
            const double t=i/kRate;
            const double u=std::clamp((t-0.30)/0.90,0.0,1.0);
            const double hz=220.0*std::pow(2.0,u);
            phase+=2.0*kPi*hz/kRate;
            output[static_cast<std::size_t>(i)]=synth(phase);
        }
        int64_t firstWet50=-1;
        double wetAtEnd=0.0;
        for(int offset=0;offset<frames;offset+=128) {
            const int n=std::min(128,frames-offset);
            float* mono[1]={output.data()+offset};
            engine.process(mono,1,n);
            if(firstWet50<0 && engine.researchWetMix()>=0.50)
                firstWet50=offset+n-1;
            wetAtEnd=engine.researchWetMix();
        }
        const auto s=signalStats(output);
        const double hz=engine.sourceHz();
        const bool reacquired=engine.voiced() && std::abs(hz-440.0)<8.0;
        pass &= reacquired && s.finite && s.peak>0.02 && s.peak<2.0
             && firstWet50>=0 && firstWet50<static_cast<int64_t>(kRate*0.14);
        std::cout<<"CAUSAL_GLIDE_RECOVERY final_f0="<<hz
                 <<" voiced="<<engine.voiced()
                 <<" first_f0_ms="<<1000.0*engine.firstVoicedSample()/kRate
                 <<" first_wet50_ms="<<1000.0*firstWet50/kRate
                 <<" final_wet="<<wetAtEnd
                 <<" absolute_max_sample_step="<<s.jump
                 <<" peak="<<s.peak<<" recovered="<<reacquired<<"\n";
    }
    // Natural 5 Hz vibrato +/-25 cents over detuned A4. We measure,
    // but DO NOT certify preservation of vibrato or timbre by automation.
    for(float humanize:std::array<float,2>{0.0f,0.85f}) {
        TrackedPrototype engine;
        engine.prepare(kRate);
        engine.setVoiceSettings(voice(humanize));
        constexpr int frames=static_cast<int>(kRate*1.2);
        std::vector<float> output(static_cast<std::size_t>(frames));
        double phase=0.0;
        for(int i=0;i<frames;++i) {
            const double t=i/kRate;
            const double cents=20.0+25.0*std::sin(2*kPi*5.0*t);
            const double hz=440.0*std::pow(2.0,cents/1200.0);
            phase+=2*kPi*hz/kRate;
            output[static_cast<std::size_t>(i)]=synth(phase);
        }
        for(int offset=0;offset<frames;offset+=128) {
            float* mono[1]={output.data()+offset};
            engine.process(mono,1,std::min(128,frames-offset));
        }
        const auto s=signalStats(output);
        pass &= s.finite && s.peak>0.02 && s.peak<2.0
             && engine.estimates()>10 && engine.voiced()
             && engine.researchWetMix()>0.90;
        std::cout<<"CAUSAL_VIBRATO humanize="<<humanize
                 <<" final_detector_hz="<<engine.sourceHz()
                 <<" requested_shift_st="<<engine.shiftSemitones()
                 <<" effective_wet="<<engine.researchWetMix()
                 <<" sample_step_max="<<s.jump
                 <<" rms="<<s.rms<<" finite="<<s.finite<<"\n";
    }
    // A silence > vowel > deterministic unvoiced fricative/noise >
    // vowel sequence stresses gates and the note reacquisition path.
    {
        TrackedPrototype engine;
        engine.prepare(kRate);
        engine.setVoiceSettings(voice(0.50f));
        constexpr int frames=static_cast<int>(kRate*1.6);
        std::vector<float> output(static_cast<std::size_t>(frames));
        uint32_t seed=0x41504558u;
        double phase=0.0;
        for(int i=0;i<frames;++i) {
            const double t=i/kRate;
            float v=0.0f;
            if((t>=0.25 && t<0.75) || (t>=1.05 && t<1.55)) {
                const double hz=t<0.75?432.5:447.5;
                phase+=2.0*kPi*hz/kRate;
                v=synth(phase);
            } else if(t>=0.75 && t<1.05) {
                seed=seed*1664525u+1013904223u;
                v=static_cast<float>(static_cast<int32_t>(seed)/
                                      2147483648.0*0.08);
            }
            output[static_cast<std::size_t>(i)]=v;
        }
        bool unvoicedNoise=false,voicedSecond=false;
        double wetAtNoise=1.0;
        for(int offset=0;offset<frames;offset+=128) {
            float* mono[1]={output.data()+offset};
            engine.process(mono,1,std::min(128,frames-offset));
            const double t=static_cast<double>(offset+127)/kRate;
            if(t>=0.99 && t<1.04) {
                unvoicedNoise |= !engine.voiced();
                wetAtNoise=std::min(wetAtNoise,engine.researchWetMix());
            }
            if(t>=1.30 && t<1.54)
                voicedSecond |= engine.voiced() && engine.sourceHz()>430.0f;
        }
        const auto s=signalStats(output);
        pass &= unvoicedNoise && voicedSecond && s.finite
             && s.peak>0.02 && s.peak<2.0;
        std::cout<<"CAUSAL_UNVOICED_RECOVERY unvoiced_noise="<<unvoicedNoise
                 <<" min_wet_during_noise="<<wetAtNoise
                 <<" voiced_again="<<voicedSecond
                 <<" output_peak="<<s.peak
                 <<" maximum_step="<<s.jump<<"\n";
    }
    std::cout<<"CAUSAL_DYNAMIC_GATE safety_and_recovery="<<pass
             <<" (vibrato/formants/clicks/dry-PDC quality still unqualified)\n";
    return pass?0:1;
}
