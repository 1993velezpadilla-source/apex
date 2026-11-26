#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <array>
#include <numeric>
using namespace APEX::TuneLive;
using Clock=std::chrono::steady_clock;
static bool probe(int rate,int block,ShifterProfile profile,bool split,int divisor=4,int instances=1)
{
    std::array<DspCore,8> cores;
    auto& core=cores[0];
    for(int j=0;j<instances;++j)
        cores[j].prepare(rate,512,2,profile,StudioControlClock::ResearchFixed1ms,split,0.0,divisor);
    DspSettings cfg;
    cfg.correction=cfg.wet=1;cfg.retune=cfg.humanize=cfg.flexTune=cfg.vibratoPreserve=0;
    cfg.key=1;cfg.scale=1;cfg.preserveFormants=true;
    std::vector<float> source(static_cast<std::size_t>(rate*3));
    double phase=0;
    constexpr double tau=6.283185307179586;
    for(int i=0;i<rate*3;++i) {
        const double t=double(i)/rate;
        const double hz=t<1?252.0:(t<2?190.0:339.0);
        phase+=tau*hz*std::exp2(0.14*std::sin(tau*5*t)/12)/rate;
        source[i]=float(0.26*std::sin(phase)+0.09*std::sin(2*phase)+0.04*std::sin(3*phase));
    }
    std::array<float,512> l{},r{};
    std::array<std::array<float,512>,8> extraL{},extraR{};
    std::vector<double> times;
    times.reserve(source.size()/block);
    const double deadline=1000.0*block/rate;
    int misses=0;
    double peak=0,step=0,previous=0,energy=0;
    bool finite=true;
    for(int pos=0;pos+block<=int(source.size());pos+=block) {
        std::copy_n(source.data()+pos,block,l.data());
        std::copy_n(source.data()+pos,block,r.data());
        float* ch[]={l.data(),r.data()};
        for(int j=1;j<instances;++j) {
            std::copy_n(source.data()+pos,block,extraL[j].data());
            std::copy_n(source.data()+pos,block,extraR[j].data());
        }
        const auto begin=Clock::now();
        core.process(ch,2,block,cfg);
        for(int j=1;j<instances;++j) {
            float* extra[]={extraL[j].data(),extraR[j].data()};
            cores[j].process(extra,2,block,cfg);
        }
        const double ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        if(pos>=rate/4){times.push_back(ms);misses+=ms>deadline;}
        for(int i=0;i<block;++i){
            finite &= std::isfinite(l[i])&&std::isfinite(r[i]);
            peak=std::max(peak,std::abs(double(l[i])));
            step=std::max(step,std::abs(l[i]-previous));
            energy+=double(l[i])*l[i];previous=l[i];
        }
    }
    std::sort(times.begin(),times.end());
    const auto p=[&](double q){return times[static_cast<std::size_t>(q*(times.size()-1))];};
    const int latency=core.latencySamples();
    core.reset();cfg.bypass=true;
    bool aligned=true;
    for(int pos=0;pos+block<=int(source.size());pos+=block){
        std::copy_n(source.data()+pos,block,l.data());
        std::copy_n(source.data()+pos,block,r.data());
        float* ch[]={l.data(),r.data()};
        core.process(ch,2,block,cfg);
        for(int i=0;i<block;++i){
            const int at=pos+i-latency;
            const float expected=at>=0?source[at]:0.0f;
            aligned &= l[i]==expected && r[i]==expected;
        }
    }
    std::cout<<"SPECTRAL_BUDGET rate="<<rate<<" block="<<block<<" profile="<<int(profile)
        <<" split="<<split<<" divisor="<<divisor<<" instances="<<instances<<" latency_samples="<<latency<<" deadline_ms="<<deadline
        <<" p50_ms="<<p(.5)<<" p95_ms="<<p(.95)<<" p99_ms="<<p(.99)
        <<" max_ms="<<times.back()<<" deadline_misses="<<misses<<" count="<<times.size()
        <<" peak="<<peak<<" max_step="<<step<<" rms="<<std::sqrt(energy/source.size())
        <<" exact_bypass="<<aligned<<"\n";
    // Host timing is comparative telemetry, not an Android release gate.
    return finite && aligned && peak<2.0 && energy>0;
}
int main(){
    bool ok=true;
    for(int rate:{44100,48000})
        for(int block:{32,64,96,128,192,256})
            for(auto profile:{ShifterProfile::HighQuality40ms,ShifterProfile::Experimental18ms,ShifterProfile::Experimental12ms})
                for(bool split:{false,true})ok &= probe(rate,block,profile,split);
    // A real graph may contain several simultaneous vocal tracks.
    // Synchronized callbacks deliberately exercise a worst-case FFT alignment.
    for(int rate:{44100,48000})
        for(int block:{32,64,96,128})
            for(int instances:{1,4,8})
                for(int divisor:{4,6,8})
                    ok &= probe(rate,block,ShifterProfile::Experimental18ms,true,divisor,instances);
    return ok?0:1;
}
