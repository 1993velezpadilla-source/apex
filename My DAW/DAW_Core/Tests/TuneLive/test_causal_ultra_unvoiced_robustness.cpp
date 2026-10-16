// Research-only hostile-audio hardening for the opt-in 80-frame high F0
// predictor. A fixed seed makes noisy consonants, pure sibilance and
// near-threshold male-range harmonics reproducible. This demands NO
// fake pitch/early-wet activation on unvoiced noise, NO false octave
// leap on 220Hz + an injected plosive, and restored output F0 after it.
// Passing the tests does NOT establish real-singer perceptual quality,
// ASIO monitoring, first-note pitch in 1ms, or all possible vocal sounds.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr double kPi=3.14159265358979323846;
float deterministicNoise(std::uint32_t& state) {
    state=state*1664525u+1013904223u;
    const int value=static_cast<int>((state>>8u)&0xffffu);
    return static_cast<float>(value-32768)/32768.0f;
}
APEX::TuneLive::VoiceSettings makeSettings() {
    APEX::TuneLive::VoiceSettings s;
    s.root=9;s.scale=0;s.correction=1.0f;s.referenceHz=440.0f;
    s.humanize=0.0f;s.flexTune=0.0f;s.noteHysteresis=0.10f;
    return s;
}
float harmonic(double phase) {
    return static_cast<float>(
        0.24*std::sin(phase)+
        0.05*std::sin(2.0*phase+0.21)+
        0.019*std::sin(3.0*phase+0.39));
}
struct Result {
    int fastJumps=0,noteJumps=0;
    int64_t firstVoice=-1,firstUltra=-1;
    double finalCents=9999.0,maxStep=0.0;
    bool finite=true;
};
Result run(double rate, int type, int chunks, std::uint32_t seed,
           bool guarded=false) {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    const int size=static_cast<int>(0.70*rate);
    std::vector<float> audio(static_cast<std::size_t>(size),0.0f);
    double phase=0.0,noiseHigh=0.0,noiseLow=0.0;
    std::uint32_t state=seed;
    const double hz=220.0*std::pow(2.0,30.0/1200.0);
    for(int i=0;i<size;++i) {
        const double t=i/rate;
        phase+=2.0*kPi*hz/rate;
        const float raw=deterministicNoise(state);
        noiseLow+=0.15*(raw-noiseLow);
        noiseHigh+=0.40*(raw-noiseHigh);
        // type 0: 700ms wideband unvoiced sibilant-like colored noise.
        // type 1: 220Hz singer plus 8ms strong broadband consonant.
        // type 2: 200ms noise then genuine 220Hz voiced onset.
        float output=0.0f;
        if(type==0)
            output=static_cast<float>(0.28*(raw-noiseLow));
        else if(type==1) {
            output=harmonic(phase);
            if(t>=0.340 && t<0.348)
                output+=static_cast<float>(0.17*(raw-noiseHigh));
        } else {
            output=t<0.200?
                static_cast<float>(0.28*(raw-noiseLow)):
                harmonic(phase);
        }
        audio[static_cast<std::size_t>(i)]=output;
    }
    HarmonicLivePrototype engine;
    engine.prepare(rate);
    engine.setVoiceSettings(makeSettings());
    engine.enableEarlyOnset(true);
    engine.enableMidRegisterOnset(true);
    engine.enableHighRegisterOnset(true);
    engine.enableUltraHighRegisterOnset(true);
    if(guarded) {
        engine.enableUltraHighFastHopResearch(true);
        engine.enableUltraLeapConsensusResearch(true);
    }
    engine.enableExperimentalEarlyTransitions(true);
    engine.enableFastEngagement(true);
    engine.enableResidualPreservation(true);
    constexpr std::array<int,7> sizes{{1,48,128,257,17,2,64}};
    for(int pos=0,block=0;pos<size;++block) {
        const int n=std::min(chunks==0?128:sizes[static_cast<std::size_t>(
                            block%static_cast<int>(sizes.size()))],size-pos);
        float* mono[1]={audio.data()+pos};
        engine.process(mono,1,n);
        pos+=n;
    }
    Result r;
    r.fastJumps=engine.fastTransitionCount();
    r.noteJumps=engine.largeNoteJumpCount();
    r.firstVoice=engine.firstVoiceSample();
    r.firstUltra=engine.firstUltraSample();
    APEX::TuneLive::PitchTracker measured;
    measured.prepare(rate);
    for(int i=0;i<size;++i) {
        const float value=audio[static_cast<std::size_t>(i)];
        r.finite &=std::isfinite(value);
        if(i>0)r.maxStep=std::max(r.maxStep,std::abs(static_cast<double>(
            value-audio[static_cast<std::size_t>(i-1)])));
        if(i>=size-static_cast<int>(0.24*rate))measured.push(value);
    }
    const auto pitch=measured.latest();
    if(pitch.voiced && pitch.hz>0.0f)
        r.finalCents=1200.0*std::log2(pitch.hz/220.0);
    return r;
}
}
int main() {
    bool success=true;int passCount=0;
    for(double rate:std::array<double,3>{{44100.,48000.,96000.}}) {
        for(int type=0;type<3;++type) {
            for(std::uint32_t seed:std::array<std::uint32_t,2>{{0x5A17C81u,0xBEEFAB11u}}) {
                const auto fixed=run(rate,type,0,seed);
                const auto varied=run(rate,type,1,seed);
                const auto consensus=run(rate,type,0,seed,true);
                const auto guardedChunks=run(rate,type,1,seed,true);
                const bool exactlySame=fixed.firstVoice==varied.firstVoice &&
                    fixed.firstUltra==varied.firstUltra &&
                    fixed.fastJumps==varied.fastJumps &&
                    fixed.noteJumps==varied.noteJumps &&
                    std::abs(fixed.finalCents-varied.finalCents)<0.0001;
                const bool noiseSafe=type==0?
                    fixed.firstUltra<0 && fixed.fastJumps==0 &&
                    fixed.firstVoice<0 :
                    fixed.firstUltra<0 && fixed.fastJumps==0 &&
                    std::abs(fixed.finalCents)<18.0 &&
                    (type!=1 || fixed.noteJumps==0);
                const bool protectedPartition=
                    consensus.firstVoice==guardedChunks.firstVoice &&
                    consensus.firstUltra==guardedChunks.firstUltra &&
                    consensus.fastJumps==guardedChunks.fastJumps &&
                    consensus.noteJumps==guardedChunks.noteJumps &&
                    std::abs(consensus.finalCents-guardedChunks.finalCents)<0.0001;
                const bool consensusNoiseSafe=type==0?
                    consensus.firstUltra<0 && consensus.fastJumps==0 &&
                    consensus.firstVoice<0:
                    consensus.firstUltra<0 && consensus.fastJumps==0 &&
                    std::abs(consensus.finalCents)<18.0 &&
                    (type!=1 || consensus.noteJumps==0);
                const bool okay=exactlySame && noiseSafe &&
                    protectedPartition && consensusNoiseSafe &&
                    fixed.finite && varied.finite &&
                    consensus.finite && guardedChunks.finite &&
                    fixed.maxStep<0.85 && varied.maxStep<0.85 &&
                    consensus.maxStep<0.85 && guardedChunks.maxStep<0.85;
                success &=okay;passCount+=okay;
                std::cout<<"ULTRA_ADVERSARIAL rate="<<rate
                    <<" type="<<type<<" seed="<<seed
                    <<" first_voice="<<fixed.firstVoice
                    <<" first_ultra="<<fixed.firstUltra
                    <<" false_up_leaps="<<fixed.fastJumps
                    <<" octave_jumps="<<fixed.noteJumps
                    <<" final_output_cents="<<fixed.finalCents
                    <<" same_any_block_partition="<<exactlySame
                    <<" guarded_any_block_partition="<<protectedPartition
                    <<" guarded_false_up_leaps="<<consensus.fastJumps
                    <<" guarded_octave_jumps="<<consensus.noteJumps
                    <<" guarded_safe="<<consensusNoiseSafe
                    <<" safe="<<okay<<"\n";
            }
        }
    }
    std::cout<<"ULTRA_ADVERSARIAL_GATE stable_and_consensus_cases="<<passCount
             <<"/18 verdict="<<(success?"PASS":"FAIL")
             <<" NOT_1MS_ACOUSTIC_PITCH_DECISION=1\n";
    return success?0:1;
}
