// Non-shipping A/B for experimental note-leap dry reseed. The old
// updatePitch() cleared learned spectral partials and toggled target wet,
// but did not actually ramp wet_ down. This opt-in experiment resets wet_
// at the same causal F0-confirmed leap and relearns partials for one source
// period before smooth wet re-engagement. STRICT test: no dropped/changed
// source samples, independent settled output F0, no added clicks, no
// host-buffer partition dependence. Does NOT certify perceptual quality
// or 1ms completed corrected pitch after a sung leap.
#define main previous_leap_main_not_called
#include "test_causal_live_note_leaps.cpp"
#undef main

namespace {
struct Candidate {
    std::vector<float> output;
    double preWet=0.0,minPostWet=1.0,recoveredMs=-1.0;
    double finalCents=9999.0,maxStep=0.0;
    int leaps=0,fastTransitions=0;
    bool finite=true;
};
Candidate render(double sr,bool dryReset,bool variable) {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    Candidate out;
    out.output=sing(sr); // phase continuous A3->A4->silence->A4
    HarmonicLivePrototype engine;
    engine.prepare(sr);engine.setVoiceSettings(hardSettings());
    engine.enableEarlyOnset(true);
    engine.enableMidRegisterOnset(true);
    engine.enableHighRegisterOnset(true);
    engine.enableUltraHighRegisterOnset(true);
    engine.enableExperimentalEarlyTransitions(true);
    engine.enableResidualPreservation(true);
    engine.enableFastEngagement(true);
    engine.enableNoteLeapDryReset(dryReset);
    constexpr std::array<int,7> chunks{{1,48,128,257,17,2,64}};
    const int leapStart=static_cast<int>(0.45*sr);
    const int minEnd=static_cast<int>(0.475*sr);
    const int recoverEnd=static_cast<int>(0.495*sr);
    // Re-sample the wet envelope only in the 50ms octave-transition
    // period with one-sample callbacks. Outside this interval randomized
    // host callback boundaries verify no scheduling dependence.
    for(int offset=0,calls=0;offset<static_cast<int>(out.output.size());++calls) {
        int n=std::min(variable?chunks[static_cast<std::size_t>(
            calls%static_cast<int>(chunks.size()))]:128,
            static_cast<int>(out.output.size())-offset);
        if(offset<leapStart-20 && offset+n>leapStart-20)
            n=leapStart-20-offset;
        if(offset>=leapStart-20 && offset<recoverEnd)n=1;
        float* mono[1]={out.output.data()+offset};
        engine.process(mono,1,n);
        if(offset<leapStart && offset+n>=leapStart)
            out.preWet=engine.researchWetMix();
        if(offset>=leapStart && offset<=minEnd) {
            out.minPostWet=std::min(out.minPostWet,
                                   engine.researchWetMix());
        }
        if(offset>leapStart && offset<=recoverEnd &&
           out.minPostWet<0.2 && out.recoveredMs<0 &&
           engine.researchWetMix()>0.5)
            out.recoveredMs=1000.0*(offset-leapStart)/sr;
        offset+=n;
    }
    out.leaps=engine.largeNoteJumpCount();
    out.fastTransitions=engine.fastTransitionCount();
    APEX::TuneLive::PitchTracker detector;
    detector.prepare(sr);
    const int last=static_cast<int>(out.output.size());
    for(int i=0;i<last;++i) {
        const float x=out.output[static_cast<std::size_t>(i)];
        out.finite &=std::isfinite(x);
        if(i>0)
            out.maxStep=std::max(out.maxStep,std::abs(static_cast<double>(
                x-out.output[static_cast<std::size_t>(i-1)])));
        if(i>=last-static_cast<int>(sr*.30))detector.push(x);
    }
    const auto f=detector.latest();
    if(f.voiced && f.hz>0.0)
        out.finalCents=1200.0*std::log2(f.hz/440.0);
    return out;
}
}
int main() {
    bool success=true;int passed=0;
    for(double sr:std::array<double,3>{{44100.,48000.,96000.}}) {
        const auto old=render(sr,false,false);
        const auto newMode=render(sr,true,false);
        const auto mixedBlocks=render(sr,true,true);
        double partitionDifference=0.0;
        for(std::size_t i=0;i<newMode.output.size();++i)
            partitionDifference=std::max(partitionDifference,
                std::abs(static_cast<double>(newMode.output[i]-
                                            mixedBlocks.output[i])));
        const bool crossfadeUsed=newMode.preWet>0.90 &&
            newMode.minPostWet<0.20 &&
            newMode.recoveredMs>0.0 &&
            newMode.recoveredMs<45.0;
        // The initial hypothesis of legacy wet staying near 100% was
        // WRONG in this fixture: primary YIN itself briefly returns
        // unvoiced on the octave change, so existing wet_ naturally
        // falls to 5–10%. Do not erase that evidence or claim that
        // dry-reseed fixes a missing fade. Assert only the measurable
        // additional full reset while retaining sound-integrity gates.
        const bool normalUnchanged=old.preWet>0.90 &&
            old.minPostWet<0.20 &&
            old.minPostWet>newMode.minPostWet+0.03;
        const bool integrity=newMode.finite && old.finite &&
            mixedBlocks.finite && newMode.leaps>=1 &&
            newMode.fastTransitions>=1 &&
            newMode.maxStep<0.65 && old.maxStep<0.65 &&
            partitionDifference<1e-5 &&
            std::abs(newMode.finalCents)<18.0 &&
            std::abs(mixedBlocks.finalCents)<18.0;
        const bool ok=crossfadeUsed && normalUnchanged && integrity;
        success &=ok;passed+=ok;
        std::cout<<"ULTRA_LEAP_DRY_RESEED sr="<<sr
                 <<" old_min_wet="<<old.minPostWet
                 <<" new_min_wet="<<newMode.minPostWet
                 <<" new_wet50_recovery_ms="<<newMode.recoveredMs
                 <<" eventual_corrected_cents="<<newMode.finalCents
                 <<" max_step="<<newMode.maxStep
                 <<" partition_max_difference="<<partitionDifference
                 <<" pass="<<ok<<" 1ms_note_latency_not_claimed=1\n";
    }
    std::cout<<"ULTRA_LEAP_DRY_RESEED_GATE passed="<<passed<<"/3"
             <<" verdict="<<(success?"PASS":"FAIL")<<"\n";
    return success?0:1;
}
