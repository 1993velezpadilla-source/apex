// Independent spectral frontier. No automatic promotion of a candidate.
#define main prior_feasibility_main
#include "test_12ms_feasibility.cpp"
#undef main
#include <chrono>
#include <iomanip>
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
        <<" cents="<<cents<<" source_leak="<<original/std::max(targetPeak,1e-12)
        <<" peak="<<peak<<" max_step="<<step<<" p99_ms="<<times[size_t(.99*(times.size()-1))]
        <<" max_ms="<<times.back()<<" misses="<<missed<<" safe="<<safe<<" tonal="<<tonal<<"\n";
    return safe;
}
int main(){
    const Candidate candidates[]={{18,4,true},{18,4,false},{16,8,true},{14,8,true},{12,8,true},{12,4,true},{10,8,true},{8,8,true},{6,8,true}};
    bool safe=true;
    for(auto c:candidates)
        for(int rate:{44100,48000})
            for(double target:{110.0,220.0,440.0})
                for(double detune:{-35.0,35.0})safe &= probeMobile(c,rate,target,detune);
    return safe?0:1;
}
