// Validate the actual 18ms candidate phase slots, not just the first insert.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
using namespace APEX::TuneLive;
constexpr double tau=6.283185307179586;
constexpr double desired=261.6255653;
struct Result {
    std::vector<float> l,r;
    int latency=0;
    bool finite=true,bypass=true;
    double silence=0,peak=0,step=0,cents=0,leak=0;
};
static double project(const std::vector<float>& x,int rate,int start,int n,double hz) {
    double re=0,im=0,sum=0;
    for(int i=0;i<n;++i){
        const double w=.5-.5*std::cos(tau*i/(n-1)),a=tau*hz*i/rate;
        re+=w*x[start+i]*std::cos(a);im+=w*x[start+i]*std::sin(a);sum+=w;
    }
    return 2*std::hypot(re,im)/sum;
}
static Result render(int rate,int channels,int slot,bool restart,bool irregular) {
    const int frames=rate*4;
    std::vector<float> dry(frames),other(frames);
    double phase=0;
    for(int i=0;i<frames;++i) {
        const double t=double(i)/rate;
        phase+=tau*252.0/rate;
        const double env=t<1.2?std::min(1.0,t/.004):
            t<1.5?0.0:std::min(1.0,(t-1.5)/.004);
        dry[i]=float(env*(.24*std::sin(phase)+.065*std::sin(2*phase+.2)));
        other[i]=.8f*dry[i];
    }
    DspCore core;
    const int hop=std::max(64,int(std::lround(rate*.018/4)));
    core.prepare(rate,512,channels,ShifterProfile::Experimental18ms,
        StudioControlClock::ResearchFixed1ms,false,0.0,4,slot*hop/8,restart);
    Result r;
    r.l=dry;r.r=other;r.latency=core.latencySamples();
    DspSettings s;
    s.key=1;s.scale=1;s.correction=s.wet=1;
    s.retune=s.humanize=s.flexTune=s.vibratoPreserve=0;s.stability=.24f;
    const std::array<int,7> sizes{{1,32,64,96,128,257,521}};
    auto process=[&](std::vector<float>& l,std::vector<float>& rr){
        for(int i=0,k=0;i<frames;++k) {
            const int n=std::min(irregular?sizes[k%7]:64,frames-i);
            float* ptr[]={l.data()+i,channels==2?rr.data()+i:nullptr};
            core.process(ptr,channels,n,s);
            i+=n;
        }
    };
    process(r.l,r.r);
    for(int i=0;i<frames;++i){
        for(int ch=0;ch<channels;++ch){
            const auto& x=ch?r.r:r.l;
            r.finite &= std::isfinite(x[i]);
            r.peak=std::max(r.peak,std::abs(double(x[i])));
            if(i>0)r.step=std::max(r.step,std::abs(double(x[i])-x[i-1]));
            if(i>=int(rate*1.4)&&i<int(rate*1.48))
                r.silence=std::max(r.silence,std::abs(double(x[i])));
        }
    }
    double best=0;
    for(int c=-35;c<=35;++c){
        const auto a=project(r.l,rate,rate*2,rate,desired*std::exp2(c/1200.0));
        if(a>best){best=a;r.cents=c;}
    }
    r.leak=project(r.l,rate,rate*2,rate,252.0)/std::max(1e-12,best);
    core.reset();s.bypass=true;
    auto l=dry,rr=other;process(l,rr);
    for(int i=0;i<frames;++i){
        const int j=i-r.latency;
        r.bypass &= l[i]==(j>=0?dry[j]:0.f);
        if(channels==2)r.bypass &= rr[i]==(j>=0?other[j]:0.f);
    }
    return r;
}
int main(){
    bool ok=true;
    for(int rate:{44100,48000})
        for(int ch:{1,2}){
            const auto base=render(rate,ch,0,false,false);
            for(int slot=0;slot<8;++slot){
                const auto a=render(rate,ch,slot,true,true);
                const auto b=render(rate,ch,slot,true,false);
                double diff=0;
                for(size_t i=0;i<a.l.size();++i)
                    diff=std::max(diff,std::abs(double(a.l[i])-b.l[i]));
                const bool pass=a.finite&&b.finite&&a.bypass&&b.bypass
                    && std::abs(a.latency-int(std::lround(rate*.018)))<=4
                    && a.latency==b.latency&&a.peak<1&&b.peak<1
                    && a.silence<1e-5&&b.silence<1e-5
                    && a.step<.35&&b.step<.35
                    && std::abs(a.cents)<=18&&std::abs(b.cents)<=18
                    && a.leak<.15&&b.leak<.15;
                std::cout<<"RESUME_QUALITY rate="<<rate<<" channels="<<ch<<" slot="<<slot
                    <<" pdc="<<a.latency<<" cents="<<a.cents<<" leak="<<a.leak
                    <<" silence="<<a.silence<<" peak="<<a.peak<<" max_step="<<a.step
                    <<" baseline_step="<<base.step<<" partition_peak_diff="<<diff
                    <<" exact_bypass="<<a.bypass<<" pass="<<pass<<"\n";
                ok &= pass;
            }
        }
    return ok?0:1;
}
