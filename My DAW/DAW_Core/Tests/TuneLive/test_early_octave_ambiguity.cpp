// Strict octave ambiguity regression for isolated fast-onset research.
// A one-period harmonic-rich short window cannot distinguish every missing
// fundamental. Hence this tests detectably present low-register fundamentals,
// and still refuses to promote this candidate to production.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveEarlyOnsetProbe.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
namespace {
constexpr double pi=3.14159265358979323846;
struct Profile {
    const char* name;
    double fundamental,amplitude1,amplitude2,amplitude3;
    bool shouldEarlyLock;
};
}
int main() {
    using APEX::TuneLive::Research::EarlyOnsetProbe;
    constexpr std::array<Profile,8> profiles{{
        {"normal_A3",220.0,0.29,0.08,0.035,true},
        {"normal_A4",440.0,0.29,0.08,0.035,true},
        {"110Hz_second_dominant",110.0,0.040,0.31,0.018,false},
        {"110Hz_second_very_dominant",110.0,0.025,0.31,0.018,false},
        {"110Hz_second_pitched",110.0,0.070,0.31,0.025,false},
        {"85Hz_second_170",85.0,0.04,0.31,0.015,false},
        {"220Hz_second_440_dominant",220.0,0.037,0.29,0.020,false},
        {"220Hz_second_440_mixed",220.0,0.070,0.29,0.017,false}
    }};
    bool safe=true;
    for(double sampleRate:std::array<double,3>{44100.0,48000.0,96000.0})
      for(const auto& p:profiles) {
        EarlyOnsetProbe fast;
        fast.prepare(sampleRate);
        int falseOctave=0,valid=0;
        const int n=static_cast<int>(sampleRate*0.35);
        double maxDeviation=0.0;
        for(int i=0;i<n;++i) {
            const double t=i/sampleRate;
            const double fundamentalPhase=2*pi*p.fundamental*t;
            const float v=static_cast<float>(
                p.amplitude1*std::sin(fundamentalPhase)
                +p.amplitude2*std::sin(2*fundamentalPhase+0.31)
                +p.amplitude3*std::sin(3*fundamentalPhase+0.18));
            if(fast.push(v) && fast.latest().voiced) {
                ++valid;
                const double octaveDeviation=1200.0*
                    std::log2(fast.latest().hz/p.fundamental);
                maxDeviation=std::max(maxDeviation,std::abs(octaveDeviation));
                if(std::abs(octaveDeviation)>35.0)++falseOctave;
            }
        }
        const bool pass=p.shouldEarlyLock
              ? (valid>3 && falseOctave==0 && maxDeviation<12.0)
              : (falseOctave==0);
        safe &= pass;
        std::cout<<"OCTAVE_GUARD scenario="<<p.name
                 <<" sr="<<sampleRate
                 <<" valid_early_frames="<<valid
                 <<" false_octave_frames="<<falseOctave
                 <<" octave_veto_frames="<<fast.octaveVetoCount()
                 <<" first_early_ms="<<1000.0*fast.firstVoicedSample()/sampleRate
                 <<" pass="<<pass<<"\n";
    }
    std::cout<<"OCTAVE_GUARD_TOTAL success="<<safe
             <<" (harmonic ambiguity requires additional real-voice gating)\n";
    return safe?0:1;
}
