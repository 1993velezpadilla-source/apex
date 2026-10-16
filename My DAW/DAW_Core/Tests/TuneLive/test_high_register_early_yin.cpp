// Research-only high-register early YIN hypothesis: a shorter observation
// window for 440+ Hz voices, independently validating 110/220 false-octaves.
// Two stable analyses, fractional-lag octave veto, no known-F0 oracle into DUT.
// NOT a 1ms pitch onset, wet correction, formant or ASIO latency certificate.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveEarlyOnsetProbe.h"
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHighRegisterProbe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
namespace {
constexpr double pi=3.14159265358979323846;
float voice(double sr,double hz,int sample) {
    const double phase=2.0*pi*hz*sample/sr;
    return static_cast<float>(0.24*std::sin(phase)
        +0.08*std::sin(2.0*phase+0.22)
        +0.024*std::sin(3.0*phase+0.41)
        +0.012*std::sin(4.0*phase+0.13));
}
}
int main() {
    using namespace APEX::TuneLive::Research;
    bool pass=true;
    int accelerated=0, octaveRejected=0;
    for(double sr:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        for(double hz:std::array<double,3>{{447.691,660.0,880.0}}) {
            HighRegisterOnsetProbe high;
            EarlyOnsetProbe normal;
            high.prepare(sr);
            normal.prepare(sr);
            const int count=static_cast<int>(sr*0.065);
            for(int i=0;i<count;++i) {
                const auto sample=voice(sr,hz,i);
                high.push(sample);
                normal.push(sample);
            }
            const auto a=high.latest(),b=normal.latest();
            const double fast=1000.0*high.firstVoicedSample()/sr;
            const double baseline=1000.0*normal.firstVoicedSample()/sr;
            const bool accurate=a.voiced && a.confidence>=0.70f &&
                std::abs(a.hz-hz)/hz<0.04 &&
                b.voiced && std::abs(b.hz-hz)/hz<0.04;
            const bool quicker=accurate && fast>0.0 && baseline>fast+1.0;
            accelerated+=quicker;
            pass &=quicker;
            std::cout<<"HIGH_YIN_EARLY sr="<<sr<<" hz="<<hz
                     <<" base_first_ms="<<baseline<<" high_first_ms="<<fast
                     <<" improvement_ms="<<baseline-fast
                     <<" high_hz="<<a.hz<<" high_confidence="<<a.confidence
                     <<" faster_accurate="<<quicker<<"\n";
        }
        // A high-register estimator must not invent A4 over a genuine
        // 220Hz/110Hz singer with lower partials, including strong upper
        // harmonics. We intentionally feed both octave traps.
        for(double hz:std::array<double,2>{{110.0,220.0}}) {
            HighRegisterOnsetProbe high;
            high.prepare(sr);
            bool falselyVoiced=false;
            float falseHz=0.0f;
            const int count=static_cast<int>(sr*0.085);
            for(int i=0;i<count;++i) {
                const auto sample=voice(sr,hz,i);
                if(high.push(sample) && high.latest().voiced) {
                    falselyVoiced=true;
                    falseHz=high.latest().hz;
                }
            }
            // Overly short high-frequency harmonics can be ambiguous;
            // if they cause false notes, do not deploy this proposal.
            const bool veto=!falselyVoiced;
            octaveRejected+=veto;
            pass &=veto;
            std::cout<<"HIGH_YIN_OCTAVE_TRAP sr="<<sr<<" actual_f0="<<hz
                     <<" false_high_locked="<<falselyVoiced
                     <<" mistaken_hz="<<falseHz<<" protected="<<veto<<"\n";
        }
    }
    std::cout<<"HIGH_YIN_SUMMARY high_accurate_faster="<<accelerated
             <<"/9 lower_register_octaves_guarded="<<octaveRejected
             <<"/6 verdict="<<(pass?"PASS":"FAIL")
             <<" NO_1MS_F0_CLAIM=1\n";
    return pass?0:1;
}
