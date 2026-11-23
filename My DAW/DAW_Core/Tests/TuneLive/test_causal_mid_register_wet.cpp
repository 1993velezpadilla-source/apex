// Research-only: verify the 128-frame 260–420Hz YIN midpoint seed
// actually accelerates the WET pitch-corrected audio, not only detector
// metadata, using identical phase-continuous synthetic sung harmonics.
// A3 220Hz must NOT trigger this midrange option; HQ40 is protected.
// Use CHROMATIC target notes (C#4/E4/G4), not an arbitrary 270Hz target.
// These timings are not one-millisecond F0 decisions or ASIO measurements.
#define main old_fast_engagement_test_main_disabled
#include "test_causal_fast_wet_engagement.cpp"
#undef main
int main() {
    bool all=true;
    int faster=0,lowSafe=0;
    for(double rate:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        for(double target:std::array<double,3>{{277.18263,330.0,392.0}}) {
            const auto old=run(rate,target,true,false,false);
            const auto mid=run(rate,target,true,false,true);
            const double saved=old.halfWetMs-mid.halfWetMs;
            const bool pass=old.finite && mid.finite &&
                mid.midMs>=9.0 && mid.midMs+1.0<old.voiceMs &&
                mid.voiceMs<old.voiceMs-1.0 &&
                mid.halfWetMs<old.halfWetMs-1.0 &&
                mid.halfWetMs>mid.voiceMs && saved>=1.0 &&
                std::abs(mid.wetCents)<18.0 && mid.wetMix>0.95 &&
                mid.maxOutputStep<0.65;
            all &=pass;faster+=pass;
            std::cout<<"CAUSAL_MID_WET sr="<<rate
                     <<" note_hz="<<target
                     <<" old_f0_ms="<<old.voiceMs
                     <<" mid_f0_ms="<<mid.voiceMs
                     <<" old_wet50_ms="<<old.halfWetMs
                     <<" mid_wet50_ms="<<mid.halfWetMs
                     <<" saved_wet_ms="<<saved
                     <<" wet_cents="<<mid.wetCents
                     <<" passed="<<pass
                     <<" NO_1MS_NOTE_RESPONSE=1\n";
        }
        const auto old=run(rate,220.0,true,false,false);
        const auto mid=run(rate,220.0,true,false,true);
        const bool protectedLow=mid.midMs<0.0 &&
            mid.voiceMs==old.voiceMs &&
            mid.halfWetMs==old.halfWetMs &&
            std::abs(mid.wetCents-old.wetCents)<0.01;
        all &=protectedLow;lowSafe+=protectedLow;
        std::cout<<"CAUSAL_MID_LOW_VETO sr="<<rate
                 <<" genuine_220_hz=1 protected="<<protectedLow
                 <<" mid_first_ms="<<mid.midMs
                 <<" wet50_ms="<<mid.halfWetMs<<"\n";
    }
    std::cout<<"CAUSAL_MID_WET_GATE earlier_and_correct="<<faster<<"/9"
             <<" low_A3_veto="<<lowSafe<<"/3"
             <<" verdict="<<(all?"PASS":"FAIL")
             <<" no_1ms_F0_claim=1\n";
    return all?0:1;
}
