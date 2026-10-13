// Offline oracle-F0 feasibility test. This is NOT a real-singer quality gate.
// External exact F0 isolates whether the causal shifter CAN shift pitch;
// tracker latency and real human F0 uncertainty remain separate blockers.
#include "../../Source/TuneLiveCore/Research/ApexTuneLivePeriodGuidedPrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
constexpr int block=128;
double autocorrHz(const std::vector<float>& wave,double sr,double expectedHz) {
    const int minLag=std::max(2,static_cast<int>(std::floor(sr/(expectedHz*1.06))));
    const int maxLag=static_cast<int>(std::ceil(sr/(expectedHz*0.94)));
    const int begin=static_cast<int>(wave.size())-8192-maxLag;
    if(begin<0) return 0.0;
    auto score=[&](int lag) {
        double ab=0.0,aa=0.0,bb=0.0;
        for(int i=0;i<8192;++i) {
            const double a=wave[static_cast<std::size_t>(begin+i)];
            const double b=wave[static_cast<std::size_t>(begin+i+lag)];
            ab+=a*b;aa+=a*a;bb+=b*b;
        }
        return ab/std::sqrt(std::max(1.0e-24,aa*bb));
    };
    int best=minLag;
    double peak=-2.0;
    for(int lag=minLag;lag<=maxLag;++lag) {
        const double value=score(lag);
        if(value>peak){best=lag;peak=value;}
    }
    double lag=best;
    if(best>minLag && best<maxLag) {
        const double a=score(best-1),b=score(best),c=score(best+1);
        const double denominator=a-2.0*b+c;
        if(std::abs(denominator)>1.e-12)
            lag+=std::clamp(0.5*(a-c)/denominator,-0.5,0.5);
    }
    return peak>0.5?sr/lag:0.0;
}
}
int main() {
    using APEX::TuneLive::Research::PeriodGuidedPrototype;
    int accurate=0,valid=0;
    for(double sr:std::array<double,2>{44100.,48000.})
      for(double target:std::array<double,2>{220.,440.})
        for(double cents:std::array<double,2>{-30.,30.}) {
        const double inputHz=target*std::pow(2.0,cents/1200.0);
        const int frames=static_cast<int>(sr*2.0);
        std::vector<float> wave(static_cast<std::size_t>(frames));
        for(int i=0;i<frames;++i){
            double v=0.29*std::sin(2.0*pi*inputHz*i/sr);
            for(int h=2;h<=13;++h){
                const double frequency=inputHz*h;
                const double envelope=0.025
                   +0.20*std::exp(-0.5*std::pow((frequency-750.0)/210.0,2))
                   +0.12*std::exp(-0.5*std::pow((frequency-1250.0)/280.0,2));
                v+=envelope/h*std::sin(2*pi*frequency*i/sr+0.13*h);
            }
            wave[static_cast<std::size_t>(i)]=static_cast<float>(v);
        }
        PeriodGuidedPrototype shifter;
        shifter.prepare(sr,1);
        shifter.setPeriodAndRatio(inputHz,target/inputHz);
        const int history=shifter.requiredHistorySamples();
        for(int i=0;i<frames;i+=block){
            float* mono[1]={wave.data()+i};
            shifter.process(mono,1,std::min(block,frames-i));
        }
        bool finite=true;
        double peak=0.0;
        for(int i=frames-static_cast<int>(sr/2);i<frames;++i){
            const float value=wave[static_cast<std::size_t>(i)];
            finite&=std::isfinite(value);
            peak=std::max(peak,std::abs(static_cast<double>(value)));
        }
        const double hz=autocorrHz(wave,sr,target);
        const double error=hz>0.0?1200.0*std::log2(hz/target):9999.0;
        const bool safe=finite && peak>0.005 && peak<2.0 &&
             history<PeriodGuidedPrototype::kCapacity;
        const bool precise=safe && std::abs(error)<18.0;
        valid+=safe;
        accurate+=precise;
        std::cout<<"CAUSAL_RESEARCH sr="<<sr<<" target="<<target
          <<" source_detune_cents="<<cents<<" history_samples_bound="<<history
          <<" history_ms_bound="<<1000.0*history/sr
          <<" wet_f0="<<hz<<" wet_error_cents="<<error
          <<" safe="<<safe<<" synthetic_pitch_eligible="<<precise<<"\n";
        if(!safe) return 1;
    }
    std::cout<<"CAUSAL_RESEARCH safety="<<valid<<"/8"
             <<" synthetic_f0_eligible="<<accurate<<"/8"
             <<" (exact source F0 oracle; NOT live singer/perceptual/PDC pass)\n";
}
