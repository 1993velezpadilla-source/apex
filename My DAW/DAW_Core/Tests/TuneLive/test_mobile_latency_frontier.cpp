// Independent spectral frontier. No automatic promotion of a candidate.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <chrono>
constexpr double pi=3.14159265358979323846;
double correlationHz(const std::vector<float>& wave,double sr,double expectedHz) {
    const int minLag=std::max(2,static_cast<int>(std::floor(sr/(expectedHz*1.06))));
    const int maxLag=static_cast<int>(std::ceil(sr/(expectedHz*0.94)));
    const int begin=static_cast<int>(wave.size())-8192-maxLag;
    if(begin<0)return 0.0;
    const int count=8192;
    auto score=[&](int lag) {
        double ab=0.0,aa=0.0,bb=0.0;
        for(int i=0;i<count;++i) {
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
        if(value>peak){peak=value;best=lag;}
    }
    double lag=best;
    if(best>minLag && best<maxLag) {
        const double left=score(best-1),mid=score(best),right=score(best+1);
        const double denom=left-2.0*mid+right;
        if(std::abs(denom)>1.0e-12)
            lag+=std::clamp(0.5*(left-right)/denom,-0.5,0.5);
    }
    return (peak>0.5 && lag>0.0)?sr/lag:0.0;
}

struct Candidate { double ms; int divisor; bool split; };
static double projection(const std::vector<float>& a,int rate,double hz) {
    const int n=rate, start=int(a.size())-n;
    double re=0,im=0,wSum=0;
    const double angle=2*pi*hz/rate;
    const double c=std::cos(angle),s=std::sin(angle);
    double x=1,y=0;
    for(int i=0;i<n;++i) {
        const double w=.5-.5*std::cos(2*pi*i/(n-1));
        const double v=a[start+i]*w;
        re+=v*x;im+=v*y;wSum+=w;
        const double nx=x*c-y*s;y=x*s+y*c;x=nx;
    }
    return 2*std::hypot(re,im)/wSum;
}
static bool probeMobile(Candidate c,int rate,double target,double detune) {
    using namespace APEX::TuneLive;
    const double source=target*std::exp2(detune/1200);
    std::vector<float> dry(rate*2);
    for(int i=0;i<rate*2;++i) {
        const double p=2*pi*source*i/rate;
        double v=.26*std::sin(p);
        for(int h=2;h<=10;++h) {
            const double f=source*h;
            v+=(.035+.18*std::exp(-.5*std::pow((f-850)/230,2)))/h*std::sin(h*p+.13*h);
        }
        dry[i]=float(v);
    }
    auto a=dry;
    DspCore dsp;
    dsp.prepare(rate,512,1,ShifterProfile::Experimental18ms,
                StudioControlClock::ResearchFixed1ms,c.split,c.ms,c.divisor);
    DspSettings cfg;
    cfg.key=9;cfg.scale=1;cfg.correction=cfg.wet=1;
    cfg.retune=cfg.humanize=cfg.flexTune=cfg.vibratoPreserve=0;
    const int sizes[]={32,64,96,128,257};
    std::vector<double> times;
    times.reserve(a.size()/32);
    int missed=0;
    auto run=[&](std::vector<float>& b,bool measure){
        for(int i=0,k=0;i<int(b.size());++k) {
            int n=std::min(sizes[k%5],int(b.size())-i);
            float* ch[]={b.data()+i};
            auto begin=std::chrono::steady_clock::now();
            dsp.process(ch,1,n,cfg);
            if(measure && i>=rate/2) {
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                times.push_back(ms); missed+=ms>1000.0*n/rate;
            }
            i+=n;
        }
    };
    run(a,true);
    const double tracked=dsp.frequencyHz();
    const double requested=dsp.debugTargetShiftSemitones();
    const double applied=dsp.debugAppliedShiftSemitones();
    const double wet=dsp.debugWetAmount();
    std::sort(times.begin(),times.end());
    const double hz=correlationHz(a,rate,target);
    const double cents=hz>0?1200*std::log2(hz/target):9999;
    const double original=projection(a,rate,source);
    double targetPeak=0;
    // Search around target rather than equating one exact DFT bin with F0.
    for(int k=-18;k<=18;k+=2)targetPeak=std::max(targetPeak,projection(a,rate,target*std::exp2(k/1200.0)));
    double peak=0,step=0;bool finite=true;
    for(int i=0;i<int(a.size());++i) {
        finite &= std::isfinite(a[i]);
        peak=std::max(peak,std::abs(double(a[i])));
        if(i)step=std::max(step,std::abs(double(a[i])-a[i-1]));
    }
    const int pdc=dsp.latencySamples();
    const int window=std::max(256,int(std::lround(rate*c.ms*.001)));
    const int hop=std::max(64,int(std::lround(double(window)/c.divisor)));
    dsp.reset();cfg.bypass=true;a=dry;run(a,false);
    bool bypass=true;
    for(int i=0;i<int(a.size());++i)bypass &= a[i]==(i>=pdc?dry[i-pdc]:0.0f);
    const bool safe=finite && peak>.01 && peak<1 && bypass && std::abs(pdc-window-(c.split?hop:0))<=4;
    const bool tonal=std::abs(cents)<=18 && targetPeak>.08 && original<targetPeak*.15;
    std::cout<<"LATENCY_FRONTIER window="<<c.ms<<" divisor="<<c.divisor<<" split="<<c.split
        <<" rate="<<rate<<" target="<<target<<" detune="<<detune<<" pdc_ms="<<1000.0*pdc/rate
        <<" tracked="<<tracked<<" requested="<<requested<<" applied="<<applied
        <<" wet="<<wet<<" cents="<<cents<<" source_leak="<<original/std::max(targetPeak,1e-12)
        <<" peak="<<peak<<" max_step="<<step<<" p99_ms="<<times[size_t(.99*(times.size()-1))]
        <<" max_ms="<<times.back()<<" misses="<<missed<<" safe="<<safe<<" tonal="<<tonal<<"\n";
    return safe;
}
// Direct shifter transfer: bypass all APEX note selection, detector,
// and wet gates. An imperfect result here cannot be solved by a faster
// retune knob. These gains are probes, NOT production compensation.
static bool rawTransfer(Candidate c,int rate,double target,double detune,
                        bool formants,double gain) {
    const int frames=rate*2;
    const double source=target*std::exp2(detune/1200);
    std::vector<float> input(frames),out(frames);
    for(int i=0;i<frames;++i) {
        const double p=2*pi*source*i/rate;
        double v=.26*std::sin(p);
        for(int h=2;h<=10;++h) {
            const double f=source*h;
            v+=(.035+.18*std::exp(-.5*std::pow((f-850)/230,2)))/h*std::sin(h*p+.13*h);
        }
        input[i]=float(v);
    }
    const int window=std::max(256,int(std::lround(rate*c.ms*.001)));
    const int hop=std::max(64,int(std::lround(double(window)/c.divisor)));
    signalsmith::stretch::SignalsmithStretch<float> shifter{0x41504558L};
    shifter.configure(1,window,hop,c.split);
    shifter.reset();
    shifter.setTransposeFactor(float(std::exp2(-detune*gain/1200)));
    shifter.setFormantFactor(1.0f,formants);
    shifter.setFormantBase(float(source/rate));
    for(int i=0;i<frames;) {
        const int n=std::min(64,frames-i);
        const float* in[]={input.data()+i};
        float* dst[]={out.data()+i};
        shifter.process(in,n,dst,n);
        i+=n;
    }
    bool finite=true;double peak=0;
    for(float v:out){finite &= std::isfinite(v);peak=std::max(peak,std::abs(double(v)));}
    const double hz=correlationHz(out,rate,target);
    const double cents=hz>0?1200*std::log2(hz/target):9999;
    double best=0;
    for(int k=-18;k<=18;k+=2)best=std::max(best,projection(out,rate,target*std::exp2(k/1200.0)));
    const double leak=projection(out,rate,source)/std::max(best,1e-12);
    std::cout<<"RAW_TRANSFER window="<<c.ms<<" divisor="<<c.divisor<<" split="<<c.split
        <<" rate="<<rate<<" target="<<target<<" detune="<<detune
        <<" formants="<<formants<<" gain="<<gain<<" cents="<<cents
        <<" source_leak="<<leak<<" peak="<<peak
        <<" tonal="<<(std::abs(cents)<=18 && best>.08 && leak<.15)<<"\n";
    return finite && peak>.01 && peak<1;
}
int main(){
    const Candidate candidates[]={{18,4,true},{18,4,false},{16,8,true},{14,8,true},{12,8,true},{12,4,true},{10,8,true},{8,8,true},{6,8,true}};
    bool safe=true;
    for(auto c:candidates)
        for(int rate:{44100,48000})
            for(double target:{110.0,220.0,440.0})
                for(double detune:{-35.0,35.0})safe &= probeMobile(c,rate,target,detune);
    for(Candidate c: {Candidate{18,4,true},Candidate{12,8,true},Candidate{8,8,true}})
        for(int rate:{44100,48000})
            for(double target:{110.0,220.0,440.0})
                for(double detune:{-35.0,35.0})
                    for(bool formants:{false,true})
                        for(double gain:{1.0,1.5,2.0})
                            safe &= rawTransfer(c,rate,target,detune,formants,gain);
    return safe?0:1;
}
