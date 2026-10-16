// Strict non-shipping A/B: real streaming F0 from 96-sample high-register
// tracker must reduce ACTUAL processed wet-control onset, independently
// measure corrected emitted audio, and NEVER seed low A2/A3 harmonics.
// Source F0 is NOT injected via an oracle. All 44.1/48/96k results are
// compared against the unchanged 160-sample early YIN research baseline.
// 1ms TOTAL pitch acquisition and Apollo ASIO have NOT been achieved.
#define main previous_fast_wet_gate_unused
#include "test_causal_fast_wet_engagement.cpp"
#undef main
int main() {
    bool safe=true;
    int highPass=0,lowPass=0;
    for(double sr:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        for(double note:std::array<double,2>{{440.0,660.0}}) {
            const auto prior=run(sr,note,true,false);
            const auto candidate=run(sr,note,true,true);
            const double advance=prior.halfWetMs-candidate.halfWetMs;
            const bool qualified=candidate.highMs>0.0 &&
                candidate.voiceMs+2.0<prior.voiceMs &&
                candidate.voiceMs>7.0 &&
                candidate.halfWetMs>candidate.voiceMs &&
                candidate.halfWetMs+2.0<prior.halfWetMs &&
                advance>=2.0;
            const bool outputSafe=prior.finite && candidate.finite &&
                std::abs(candidate.wetCents)<18.0 &&
                std::abs(prior.wetCents)<18.0 &&
                candidate.maxOutputStep<0.65 &&
                candidate.wetMix>0.95;
            const bool pass=qualified && outputSafe;
            safe &=pass;highPass+=pass;
            std::cout<<"ACTUAL_HIGH_WET sr="<<sr<<" target="<<note
                     <<" baseline_f0_ms="<<prior.voiceMs
                     <<" high_seed_f0_ms="<<candidate.voiceMs
                     <<" 96frame_ms="<<candidate.highMs
                     <<" baseline_wet50_ms="<<prior.halfWetMs
                     <<" accelerated_wet50_ms="<<candidate.halfWetMs
                     <<" saved_wet50_ms="<<advance
                     <<" independent_cents="<<candidate.wetCents
                     <<" pass="<<pass<<" NOT_1MS_TONAL_DELAY=1\n";
        }
        const auto regularLow=run(sr,220.0,true,false);
        const auto testLow=run(sr,220.0,true,true);
        const bool lowSafe=testLow.highMs<0.0 && testLow.finite &&
             testLow.voiceMs==regularLow.voiceMs &&
             std::abs(testLow.wetCents)<18.0 &&
             std::abs(testLow.halfWetMs-regularLow.halfWetMs)<0.0001;
        lowPass+=lowSafe;safe &=lowSafe;
        std::cout<<"LOW_REGISTER_F0_VETO sr="<<sr
                 <<" original_ms="<<regularLow.voiceMs
                 <<" high_probe_first_ms="<<testLow.highMs
                 <<" first_wet50_ms="<<testLow.halfWetMs
                 <<" safe="<<lowSafe<<"\n";
    }
    std::cout<<"ACTUAL_HIGH_WET_SUMMARY accelerated="<<highPass
             <<"/6 low_harmonic_veto="<<lowPass
             <<"/3 verdict="<<(safe?"PASS":"FAIL")
             <<" physical_1ms_pitch_response=NOT_PROVEN\n";
    return safe?0:1;
}
