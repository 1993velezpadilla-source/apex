// Research-only, independently measured <1ms signal-through budget.
// IMPORTANT: this tests the direct residual/transient component in a
// PRE-ACQUIRED, already corrected wet voice. It does NOT demonstrate
// <1ms NEW-PITCH detection, total wet-voice turnaround, host reported
// plugin PDC, acoustic formant preservation, or audio interface roundtrip.
// Target reference: modern tracking plugins report <1ms algorithmic delay
// in live modes. That is different from first sung note acquisition.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
APEX::TuneLive::VoiceSettings config() {
    APEX::TuneLive::VoiceSettings s;
    s.root=9;s.scale=0;s.correction=1.0f;s.referenceHz=440.0f;
    s.humanize=0.0f;s.flexTune=0.0f;s.noteHysteresis=0.10f;
    return s;
}
float sample(double rate,double hz,int pos) {
    const double angle=2.0*pi*hz*static_cast<double>(pos)/rate;
    return static_cast<float>(0.26*std::sin(angle)
        +0.05*std::sin(2.0*angle+0.31)
        +0.018*std::sin(3.0*angle+0.13));
}
struct Render {
    std::vector<float> audio;
    double wetAtInjection=0.0;
    int64_t firstPitchSample=-1;
    int64_t firstHalfWetSample=-1;
};
Render run(double rate,double note,bool guarded,bool addAttack) {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    const int n=static_cast<int>(rate*0.5);
    const int impulseAt=static_cast<int>(rate*0.30);
    Render r;
    r.audio.resize(static_cast<std::size_t>(n));
    const double sung=note*std::pow(2.0,30.0/1200.0);
    for(int i=0;i<n;++i)
        r.audio[static_cast<std::size_t>(i)]=sample(rate,sung,i);
    if(addAttack)r.audio[static_cast<std::size_t>(impulseAt)]+=0.25f;
    HarmonicLivePrototype engine;
    engine.prepare(rate);
    engine.setVoiceSettings(config());
    engine.enableEarlyOnset(true);
    engine.enableResidualPreservation(true);
    engine.enableFastEngagement(true);
    if(guarded) {
        engine.enableMidRegisterOnset(true);
        engine.enableHighRegisterOnset(true);
        engine.enableUltraHighRegisterOnset(true);
        engine.enableUltraHighFastHopResearch(true);
        engine.enableUltraLeapConsensusResearch(true);
        engine.enableExperimentalEarlyTransitions(true);
    }
    constexpr std::array<int,7> hops{{1,17,48,64,128,257,2}};
    // Track exactly at sample N, even with arbitrary host callback splits.
    int offset=0,block=0;
    while(offset<n) {
        int size=std::min(hops[static_cast<std::size_t>(block%hops.size())],
                          n-offset);
        if(offset<impulseAt && offset+size>impulseAt)
            size=impulseAt-offset;
        if(offset==impulseAt)size=1;
        float* ptr[1]={r.audio.data()+offset};
        engine.process(ptr,1,size);
        if(offset==impulseAt)
            r.wetAtInjection=engine.researchWetMix();
        offset+=size;
        ++block;
    }
    r.firstPitchSample=engine.firstVoiceSample();
    r.firstHalfWetSample=engine.firstHalfWetSample();
    return r;
}
}

int main() {
    using APEX::TuneLive::PitchTracker;
    bool all=true;
    int passed=0;
    for(const double rate:std::array<double,4>{{44100.,48000.,96000.,192000.}})
      for(const double note:std::array<double,2>{{220.,440.}})
        for(const bool guarded:std::array<bool,2>{{false,true}}) {
            const auto base=run(rate,note,guarded,false);
            const auto attack=run(rate,note,guarded,true);
            const int injection=static_cast<int>(rate*0.30);
            const int budget=static_cast<int>(std::ceil(rate*0.001));
            const float immediate=attack.audio[static_cast<std::size_t>(injection)]
                                 -base.audio[static_cast<std::size_t>(injection)];
            const double impulseGain=immediate/0.25;
            int firstDelta=-1;
            double earlyDifference=0.0;
            bool finite=true;
            for(int i=0;i<static_cast<int>(base.audio.size());++i) {
                const float x=base.audio[static_cast<std::size_t>(i)];
                const float y=attack.audio[static_cast<std::size_t>(i)];
                finite &=std::isfinite(x)&&std::isfinite(y);
                const double delta=std::abs(static_cast<double>(x)-y);
                if(i<injection)earlyDifference=std::max(earlyDifference,delta);
                if(firstDelta<0 && delta>0.25*0.40)firstDelta=i;
            }
            PitchTracker independent;
            independent.prepare(rate);
            const int tail=static_cast<int>(rate*0.16);
            for(int i=static_cast<int>(base.audio.size())-tail;
                i<static_cast<int>(base.audio.size());++i)
                independent.push(base.audio[static_cast<std::size_t>(i)]);
            const auto pitch=independent.latest();
            const double cents=pitch.voiced && pitch.hz>0
                ?1200.0*std::log2(pitch.hz/note):9999.0;
            const bool corrected=std::abs(cents)<18.0;
            const bool noFixedLookahead=firstDelta==injection &&
                firstDelta-injection<budget &&
                earlyDifference<1.0e-7;
            const bool pass=finite && noFixedLookahead && corrected &&
                base.wetAtInjection>=0.95 && attack.wetAtInjection>=0.95 &&
                std::isfinite(impulseGain) && impulseGain>=0.70 &&
                impulseGain<=1.30 && base.firstPitchSample>=0 &&
                base.firstHalfWetSample>=0;
            passed+=pass;all &=pass;
            std::cout<<"LIVE_LT1MS_COMPONENT sr="<<rate
                     <<" note="<<note<<" guarded="<<guarded
                     <<" budget_samples="<<budget
                     <<" transient_extra_samples="<<(firstDelta-injection)
                     <<" immediate_transient_gain="<<impulseGain
                     <<" corrected_output_cents="<<cents
                     <<" wet_at_attack="<<base.wetAtInjection
                     <<" first_pitch_ms="<<1000.0*base.firstPitchSample/rate
                     <<" first_halfwet_ms="<<1000.0*base.firstHalfWetSample/rate
                     <<" no_fixed_lookahead="<<noFixedLookahead
                     <<" pass="<<pass
                     <<" NOT_TOTAL_WET_OR_HOST_PDC=1\n";
        }
    std::cout<<"LIVE_LT1MS_COMPONENT_GATE passed="<<passed
             <<"/16 verdict="<<(all?"PASS":"FAIL")
             <<" fixed_transient_component_only=1"
             <<" new_note_under_1ms=NOT_PROVEN"
             <<" shipping_host_PDC_under_1ms=NOT_PROVEN\n";
    return all?0:1;
}
