// Research ONLY: attempts sub-8ms HIGH register F0 estimation at 48k.
// 80-decimated-sample window, two agreeing analysis frames; NOT 1ms.
// Strict 220/330/392Hz half/double pitch misdetection stress.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveUltraHighProbe.h"
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHighRegisterProbe.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
namespace {
constexpr double kPi=3.14159265358979323846;
float signal(double sr,double hz,int i,bool strongSecond=false) {
    const double p=2.0*kPi*hz*i/sr;
    return static_cast<float>(0.24*std::sin(p)
         +(strongSecond?0.18:0.08)*std::sin(2.0*p+0.22)
         +0.024*std::sin(3.0*p+0.41)
         +0.012*std::sin(4.0*p+0.13));
}
}
int main() {
    using namespace APEX::TuneLive::Research;
    bool valid=true;
    int earlier=0,vetoed=0;
    for(double rate:std::array<double,3>{{44100.,48000.,96000.}}) {
        for(double frequency:std::array<double,3>{{447.691,660.,770.}}) {
            HighRegisterOnsetProbe baseline;
            UltraHighRegisterProbe accelerated;
            baseline.prepare(rate);accelerated.prepare(rate);
            for(int i=0;i<static_cast<int>(0.06*rate);++i) {
                const float x=signal(rate,frequency,i);
                baseline.push(x);accelerated.push(x);
            }
            const auto old=baseline.latest();
            const auto now=accelerated.latest();
            const double before=1000.0*baseline.firstVoicedSample()/rate;
            const double after=1000.0*accelerated.firstVoicedSample()/rate;
            const bool improved=old.voiced && now.voiced &&
                now.confidence>=0.70f &&
                std::abs(now.hz-frequency)/frequency<0.04 &&
                after>=5.0 && after<before-0.65 &&
                before>0.0 && std::abs(old.hz-frequency)/frequency<0.04;
            earlier+=improved;valid &=improved;
            std::cout<<"ULTRA_HIGH_ONSET sr="<<rate
                <<" source="<<frequency<<" previous_ms="<<before
                <<" ultra_ms="<<after<<" saved_ms="<<before-after
                <<" measured_hz="<<now.hz
                <<" okay="<<improved<<" NOT_1MS_F0=1\n";
        }
        for(double lower:std::array<double,4>{{110.,220.,330.,392.}}) {
            for(bool strong:std::array<bool,2>{{false,true}}) {
                UltraHighRegisterProbe accelerated;
                accelerated.prepare(rate);
                bool invented=false;
                double wrongFrequency=0.0;
                for(int i=0;i<static_cast<int>(0.09*rate);++i) {
                    if(accelerated.push(signal(rate,lower,i,strong)) &&
                       accelerated.latest().voiced) {
                        // Candidate MAY produce real 392Hz? It is outside
                        // accepted high range >=400; ANY high lock is bad.
                        invented=true;
                        wrongFrequency=accelerated.latest().hz;
                    }
                }
                const bool protectedPitch=!invented;
                vetoed+=protectedPitch;valid &=protectedPitch;
                std::cout<<"ULTRA_HIGH_OCTAVE_VETO sr="<<rate
                    <<" lower_hz="<<lower
                    <<" strong_harmonic="<<strong
                    <<" false_high="<<wrongFrequency
                    <<" protected="<<protectedPitch<<"\n";
            }
        }
    }
    std::cout<<"ULTRA_HIGH_GATE faster="<<earlier<<"/9"
             <<" octave_veto="<<vetoed<<"/24"
             <<" verdict="<<(valid?"PASS":"FAIL")
             <<" physical_1ms_pitch_response=NOT_PROVEN\n";
    return valid?0:1;
}
