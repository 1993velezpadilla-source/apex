// Non-shipping LIVE voiced-tone note leap, silence and host callback gate.
// Replays EXACT SAME stream in 1/48/128/257-sized partitions; the research
// engine must react consistently, re-acquire notes and stay finite without
// introducing a large amplitude click on an octave jump.
// Neither buffer-invariance nor no-click means first-note response <=1ms.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
APEX::TuneLive::VoiceSettings hardSettings() {
    APEX::TuneLive::VoiceSettings s;
    s.root=9;s.scale=0;s.correction=1.0f;s.referenceHz=440.0f;
    s.humanize=0.0f;s.flexTune=0.0f;s.noteHysteresis=0.10f;
    return s;
}
std::vector<float> sing(double sr) {
    // A3+30c -> A4-30c -> silence -> A4+30c.
    // Integrate source phase (no synthetic waveform discontinuity).
    const int samples=static_cast<int>(1.5*sr);
    std::vector<float> data(static_cast<std::size_t>(samples));
    double phase=0.0;
    for(int i=0;i<samples;++i) {
        const double t=i/sr;
        const double base=t<0.45?220.0:440.0;
        const double cents=t<0.45?30.0:t<1.05?-30.0:30.0;
        const double hz=base*std::pow(2.0,cents/1200.0);
        phase+=2.0*pi*hz/sr;
        // 5ms source envelope transitions prevent artificial splice pops.
        double env=1.0;
        if(t>=0.85 && t<0.855)env=1.0-(t-0.85)/0.005;
        else if(t>=0.855 && t<1.05)env=0.0;
        else if(t>=1.05 && t<1.055)env=(t-1.05)/0.005;
        data[static_cast<std::size_t>(i)]=static_cast<float>(env*(
           0.24*std::sin(phase)+0.045*std::sin(2*phase+0.18)
           +0.018*std::sin(3*phase+0.24)));
    }
    return data;
}
struct Result {
    std::vector<float> output;
    int noteJumps=0;
    int fastTransitions=0;
    int64_t lastFastTransition=-1;
    int64_t lastJump=-1;
    double finalHz=0.0;
    double maxStep=0.0;
    double wet=0.0;
    bool finite=true;
};
Result test(double sr, bool oddBlocks, bool fast=false) {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    Result result;
    result.output=sing(sr);
    HarmonicLivePrototype engine;
    engine.prepare(sr);
    engine.setVoiceSettings(hardSettings());
    engine.enableEarlyOnset(true);
    engine.enableExperimentalEarlyTransitions(fast);
    engine.enableResidualPreservation(true);
    constexpr std::array<int,7> hops{{1,48,128,257,17,2,64}};
    int cursor=0,iterations=0;
    while(cursor<static_cast<int>(result.output.size())) {
        const int count=std::min(oddBlocks?hops[static_cast<std::size_t>(
                 iterations%static_cast<int>(hops.size()))]:128,
                 static_cast<int>(result.output.size())-cursor);
        float* mono[1]={result.output.data()+cursor};
        engine.process(mono,1,count);
        cursor+=count;
        ++iterations;
    }
    for(std::size_t i=0;i<result.output.size();++i) {
        const auto value=result.output[i];
        result.finite &=std::isfinite(value);
        if(i>0 && i<result.output.size()-1)
            result.maxStep=std::max(result.maxStep,
                 std::abs(static_cast<double>(value-result.output[i-1])));
    }
    result.noteJumps=engine.largeNoteJumpCount();
    result.fastTransitions=engine.fastTransitionCount();
    result.lastFastTransition=engine.lastFastTransitionSample();
    result.lastJump=engine.lastLargeNoteJumpSample();
    result.wet=engine.researchWetMix();
    APEX::TuneLive::PitchTracker measured;
    measured.prepare(sr);
    for(std::size_t i=result.output.size()-static_cast<std::size_t>(sr*0.3);
        i<result.output.size();++i)
        measured.push(result.output[i]);
    if(measured.latest().voiced)result.finalHz=measured.latest().hz;
    return result;
}
}
int main() {
    bool safe=true;
    for(double sr:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        const auto fixed=test(sr,false);
        const auto variable=test(sr,true);
        double maxDifference=0.0;
        for(std::size_t i=0;i<fixed.output.size();++i)
            maxDifference=std::max(maxDifference,std::abs(
                  static_cast<double>(fixed.output[i]-variable.output[i])));
        const double cents=fixed.finalHz>0?
            1200.0*std::log2(fixed.finalHz/440.0):9999.0;
        const bool correct=std::abs(cents)<18.0;
        const bool passed=fixed.finite && variable.finite &&
           fixed.noteJumps>=1 && variable.noteJumps>=1 &&
           fixed.lastJump>=0 && variable.lastJump>=0 &&
           maxDifference<1e-5 &&
           fixed.maxStep<0.65 && variable.maxStep<0.65 &&
           fixed.wet>0.95 && variable.wet>0.95 && correct;
        safe &= passed;
        std::cout<<"CAUSAL_NOTE_LEAPS sr="<<sr
             <<" large_jump_count="<<fixed.noteJumps
             <<" last_jump_ms="<<1000.0*fixed.lastJump/sr
             <<" max_output_step="<<fixed.maxStep
             <<" varying_block_max_diff="<<maxDifference
             <<" final_output_hz="<<fixed.finalHz
             <<" final_cents="<<cents
             <<" passed="<<passed
             <<" no_1ms_note_response_claim=1\n";
    }
    std::cout<<"CAUSAL_NOTE_LEAPS "<<(safe?"PASS":"FAIL")<<"\n";
    return safe?0:1;
}
