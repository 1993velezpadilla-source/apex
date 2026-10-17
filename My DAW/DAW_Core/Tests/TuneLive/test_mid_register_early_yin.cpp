// Research-only 128-frame midrange 260–420Hz F0 hypothesis.
// A pass proves ONLY sample-streamed, two-frame detector performance;
// it is NOT 1ms musical note detection, real singer perceptual quality,
// native VST latency, or actual Apollo Solo monitoring.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveEarlyOnsetProbe.h"
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveMidRegisterProbe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
namespace {
constexpr double pi=3.14159265358979323846;
float sample(double sr,double hz,int index,bool strongSecond=false) {
    const double phase=2.0*pi*hz*index/sr;
    return static_cast<float>(0.24*std::sin(phase)
        +(strongSecond?0.18:0.08)*std::sin(2.0*phase+0.22)
        +0.024*std::sin(3.0*phase+0.41)
        +0.012*std::sin(4.0*phase+0.13));
}
}
int main() {
    using namespace APEX::TuneLive::Research;
    bool all=true;
    int faster=0,protectedOctaves=0;
    for(double sr:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        for(double hz:std::array<double,3>{{270.0,330.0,392.0}}) {
            MidRegisterOnsetProbe mid;
            EarlyOnsetProbe general;
            mid.prepare(sr); general.prepare(sr);
            for(int i=0;i<static_cast<int>(sr*0.068);++i) {
                const auto input=sample(sr,hz,i);
                mid.push(input);general.push(input);
            }
            const auto m=mid.latest();
            const auto g=general.latest();
            const double midTime=1000.0*mid.firstVoicedSample()/sr;
            const double originalTime=1000.0*general.firstVoicedSample()/sr;
            const bool pass=m.voiced && m.confidence>0.70f &&
                g.voiced && std::abs(m.hz-hz)/hz<0.04 &&
                std::abs(g.hz-hz)/hz<0.04 &&
                midTime>=9.0 && midTime+1.0<originalTime;
            all &=pass;faster+=pass;
            std::cout<<"MID_F0_REALTIME sr="<<sr<<" source_hz="<<hz
                     <<" mid_ms="<<midTime<<" reference_ms="<<originalTime
                     <<" saved_ms="<<originalTime-midTime
                     <<" mid_hz="<<m.hz
                     <<" pass="<<pass<<"\n";
        }
        // Lower octave traps must NEVER become premature midrange notes,
        // especially 130Hz->260Hz and 196Hz->392Hz.
        for(double hz:std::array<double,5>{{110.0,130.0,196.0,220.0,245.0}}) {
            for(bool strongSecond:std::array<bool,2>{{false,true}}) {
                MidRegisterOnsetProbe mid;
                mid.prepare(sr);
                bool falseOctave=false;
                float falseHz=0.0f;
                for(int i=0;i<static_cast<int>(sr*0.086);++i) {
                    const auto input=sample(sr,hz,i,strongSecond);
                    if(mid.push(input)&&mid.latest().voiced) {
                        falseOctave=true;falseHz=mid.latest().hz;
                    }
                }
                const bool protectedNote=!falseOctave;
                all &=protectedNote; protectedOctaves+=protectedNote;
                std::cout<<"MID_F0_OCTAVE_VETO sr="<<sr
                         <<" actual_hz="<<hz
                         <<" strong_second="<<strongSecond
                         <<" false_high_hz="<<falseHz
                         <<" protected="<<protectedNote<<"\n";
            }
        }
    }
    std::cout<<"MID_F0_SUMMARY faster_accurate="<<faster<<"/9"
             <<" low_octaves_protected="<<protectedOctaves<<"/30"
             <<" pass="<<all
             <<" NOT_1MS_PITCH_DECISION=1\n";
    return all?0:1;
}
