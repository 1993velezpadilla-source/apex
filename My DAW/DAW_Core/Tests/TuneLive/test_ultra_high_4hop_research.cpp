// APEX Tune Live RESEARCH ONLY. Controlled 4-hop vs 8-hop YIN cadence.
// The detector retains 80 decimated samples AND two agreeing candidates.
// Actual wet signal from the causal prototype, no input-frequency oracle.
// Independently measure settled output cents, glitches and false octaves.
// Faster F0 observation != 1ms acoustic correction / ASIO round-trip.
// This optional mode is NOT enabled in the DAW or production HQ40.
#define main earlier_wet_test_entry_not_called
#include "test_causal_fast_wet_engagement.cpp"
#undef main
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {
constexpr double twoPi=6.283185307179586476925286766559;

float harmonics(double sr, double hz, int i, bool strongSecond) {
    const double p=twoPi*hz*i/sr;
    return static_cast<float>(0.24*std::sin(p)
         +(strongSecond?0.18:0.08)*std::sin(2.0*p+0.22)
         +0.024*std::sin(3.0*p+0.41)
         +0.012*std::sin(4.0*p+0.13));
}

bool falseHighLock(double sr, double hz, bool strongSecond, bool shorter) {
    APEX::TuneLive::Research::UltraHighRegisterProbe detector;
    detector.prepare(sr);
    if(shorter)detector.enableFastHopResearch(true);
    bool falseLock=false;
    for(int i=0;i<static_cast<int>(0.09*sr);++i) {
        if(detector.push(harmonics(sr,hz,i,strongSecond)) &&
           detector.latest().voiced)falseLock=true;
    }
    return falseLock;
}
}
int main() {
    bool valid=true;
    int fastAndAccurate=0, protectedLower=0, vetoed=0;
    for(double sr:std::array<double,3>{{44100.,48000.,96000.}}) {
        for(double target:std::array<double,2>{{440.,660.}}) {
            // Same actual DSP, waveform and 80-frame analysis history.
            const auto previous=run(sr,target,true,true,false,true,false);
            const auto fourHop=run(sr,target,true,true,false,true,true);
            const double savedF0=previous.voiceMs-fourHop.voiceMs;
            const double savedWet=previous.halfWetMs-fourHop.halfWetMs;
            const bool earlier=savedF0>0.15 && savedWet>0.15 &&
                fourHop.voiceMs>=5.0 && fourHop.halfWetMs>fourHop.voiceMs;
            const bool audio=fourHop.finite && previous.finite &&
                fourHop.wetMix>0.95 && fourHop.maxOutputStep<0.65 &&
                std::abs(fourHop.wetCents)<18.0 &&
                std::abs(previous.wetCents)<18.0;
            const bool passed=earlier && audio;
            valid &=passed;
            fastAndAccurate+=passed;
            std::cout<<"ULTRA_4HOP_WET sr="<<sr
                     <<" target="<<target
                     <<" baseline_first_f0_ms="<<previous.voiceMs
                     <<" fast_first_f0_ms="<<fourHop.voiceMs
                     <<" f0_saved_ms="<<savedF0
                     <<" baseline_control_wet50_ms="<<previous.halfWetMs
                     <<" fast_control_wet50_ms="<<fourHop.halfWetMs
                     <<" control_wet_saved_ms="<<savedWet
                     <<" final_output_cents="<<fourHop.wetCents
                     <<" passed="<<passed
                     <<" NO_1MS_ACOUSTIC_CLAIM=1\n";
        }
        for(double low:std::array<double,3>{{220.,330.,392.}}) {
            const auto baseline=run(sr,low,true,true,false,true,false);
            const auto faster=run(sr,low,true,true,false,true,true);
            const bool unchanged=faster.finite && baseline.finite &&
                 std::abs(faster.voiceMs-baseline.voiceMs)<0.001 &&
                 std::abs(faster.halfWetMs-baseline.halfWetMs)<0.001 &&
                 faster.ultraMs<0 && baseline.ultraMs<0 &&
                 std::abs(faster.wetCents-baseline.wetCents)<0.01;
            valid &=unchanged;
            protectedLower+=unchanged;
            std::cout<<"ULTRA_4HOP_LOWER_FALLBACK sr="<<sr
                     <<" lower="<<low
                     <<" unchanged="<<unchanged<<"\n";
        }
        for(double lower:std::array<double,4>{{110.,220.,330.,392.}}) {
            for(bool strong:std::array<bool,2>{{false,true}}) {
                const bool before=falseHighLock(sr,lower,strong,false);
                const bool after=falseHighLock(sr,lower,strong,true);
                const bool safe=!before && !after;
                valid &=safe;
                vetoed+=safe;
                std::cout<<"ULTRA_4HOP_OCTAVE_GUARD sr="<<sr
                         <<" lower="<<lower
                         <<" strong_second="<<strong
                         <<" false_lock_before="<<before
                         <<" false_lock_after="<<after
                         <<" protected="<<safe<<"\n";
            }
        }
    }
    std::cout<<"ULTRA_4HOP_RESEARCH_GATE accelerated="
             <<fastAndAccurate<<"/6 lower_unchanged="
             <<protectedLower<<"/9 octave_safe="<<vetoed<<"/24 verdict="
             <<(valid?"PASS":"FAIL")
             <<" fast_hop_not_shipping=1"
             <<" 1ms_full_pitch_latency=NOT_PROVEN\n";
    return valid?0:1;
}
