// Non-shipping actual streaming DSP with independent *OUTPUT* F0 check.
// Ultra 80-frame hypothesis must advance both F0 and wet mix over existing
// guarded high-register 96-frame path at 44.1/48/96k, with real source
// samples (NO known F0 oracle sent into the plugin).
// For 220/330/392Hz it must never seed a false high note; those voices
// stay on their previous safe high/mid/general fallback.
#define main previous_causal_fast_wet_main_not_called
#include "test_causal_fast_wet_engagement.cpp"
#undef main
int main() {
    bool success=true;
    int highPass=0,lowerPass=0;
    for(double rate:std::array<double,3>{{44100.,48000.,96000.}}) {
        for(double target:std::array<double,2>{{440.,660.}}) {
            const auto old=run(rate,target,true,true,false,false);
            const auto ultra=run(rate,target,true,true,false,true);
            const double saved=old.halfWetMs-ultra.halfWetMs;
            const bool ahead=ultra.ultraMs>=6.0 &&
                ultra.ultraMs+0.7<old.highMs &&
                ultra.voiceMs<old.voiceMs-0.7 &&
                ultra.halfWetMs<old.halfWetMs-0.7 &&
                ultra.halfWetMs>ultra.voiceMs && saved>=0.7;
            const bool safe=ultra.finite && old.finite &&
                std::abs(ultra.wetCents)<18.0 &&
                std::abs(old.wetCents)<18.0 &&
                ultra.maxOutputStep<0.65 && ultra.wetMix>0.95;
            const bool pass=ahead&&safe;
            highPass+=pass;success &=pass;
            std::cout<<"ULTRA_HIGH_WET sr="<<rate<<" target="<<target
                     <<" previous_f0_ms="<<old.voiceMs
                     <<" ultra_f0_ms="<<ultra.voiceMs
                     <<" previous_wet50_ms="<<old.halfWetMs
                     <<" ultra_wet50_ms="<<ultra.halfWetMs
                     <<" saved_wet_ms="<<saved
                     <<" corrected_output_cents="<<ultra.wetCents
                     <<" pass="<<pass<<" 1ms_pitch_claim=0\n";
        }
        for(double lower:std::array<double,3>{{220.,330.,392.}}) {
            const auto old=run(rate,lower,true,true,false,false);
            const auto ultra=run(rate,lower,true,true,false,true);
            const bool safe=ultra.ultraMs<0.0 &&
                old.voiceMs==ultra.voiceMs &&
                old.halfWetMs==ultra.halfWetMs &&
                std::abs(old.wetCents-ultra.wetCents)<0.01 &&
                ultra.finite && std::abs(ultra.wetCents)<18.0;
            lowerPass+=safe;success &=safe;
            std::cout<<"ULTRA_HIGH_WET_LOWER_PROTECTION sr="<<rate
                     <<" input_target="<<lower
                     <<" ultra_ms="<<ultra.ultraMs
                     <<" untouched="<<safe<<"\n";
        }
    }
    std::cout<<"ULTRA_HIGH_REAL_WET_GATE accelerated="<<highPass<<"/6"
             <<" lower_protected="<<lowerPass<<"/9"
             <<" verdict="<<(success?"PASS":"FAIL")
             <<" NO_1MS_NEW_NOTE_RESPONSE=1\n";
    return success?0:1;
}
