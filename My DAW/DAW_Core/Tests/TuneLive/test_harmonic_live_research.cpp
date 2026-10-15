// Research ONLY: test the first causal harmonic prediction hypothesis
// with ORIGINAL waveform F0 analysis (no frequency oracle into DUT).
// Tests 44.1/48/96k, variable callbacks, right-only stereo, automation
// note-change resilience, dry-path immediacy, finite output and F0 telemetry.
// A passed run does NOT mean 1ms audible correction latency or voice fidelity.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
APEX::TuneLive::VoiceSettings hard() {
    APEX::TuneLive::VoiceSettings p;
    p.root=9;p.scale=0;p.correction=1.0f;p.referenceHz=440.0f;
    p.humanize=0.0f;p.flexTune=0.0f;p.noteHysteresis=0.10f;
    return p;
}
float voiced(double rate,double hz,int frame) {
    const double t=static_cast<double>(frame)/rate;
    double sum=0.28*std::sin(2*pi*hz*t);
    for(int h=2;h<=10;++h)
        sum+=(0.13/(h*h))*std::sin(2*pi*hz*h*t+0.17*h);
    return static_cast<float>(sum);
}
double autocorrHz(const std::vector<float>& samples,double rate,double target) {
    constexpr int count=8192;
    const int from=static_cast<int>(samples.size())-count-1;
    if(from<0)return 0.0;
    const int minLag=std::max(2,static_cast<int>(rate/(target*1.055)));
    const int maxLag=static_cast<int>(rate/(target*0.945));
    if(from<=maxLag)return 0.0;
    auto score=[&](int lag) {
        double xx=0.0,yy=0.0,xy=0.0;
        for(int j=0;j<count;++j) {
            const double a=samples[static_cast<std::size_t>(from-j)];
            const double b=samples[static_cast<std::size_t>(from-j-lag)];
            xy+=a*b;xx+=a*a;yy+=b*b;
        }
        return xy/std::sqrt(std::max(1.0e-20,xx*yy));
    };
    int best=minLag;double highest=-2.0;
    for(int lag=minLag;lag<=maxLag;++lag) {
        const double c=score(lag);
        if(c>highest) {highest=c;best=lag;}
    }
    if(highest<0.65)return 0.0;
    double period=best;
    if(best>minLag && best<maxLag) {
        const double a=score(best-1),b=score(best),c=score(best+1);
        const double denominator=a-2.0*b+c;
        if(std::abs(denominator)>1.0e-10)
            period+=std::clamp(0.5*(a-c)/denominator,-0.5,0.5);
    }
    return rate/period;
}
}
int main() {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    bool safe=true;
    int acousticallyEligible=0;
    for(double rate:std::array<double,3>{{44100,48000,96000}})
      for(double target:std::array<double,2>{{220,440}})
        for(double cents:std::array<double,2>{{-30,30}}) {
        const double input=target*std::pow(2.0,cents/1200.0);
        const int count=static_cast<int>(rate*1.4);
        std::vector<float> output(static_cast<std::size_t>(count));
        for(int i=0;i<count;++i)
            output[static_cast<std::size_t>(i)]=voiced(rate,input,i);
        HarmonicLivePrototype engine;
        engine.prepare(rate);
        engine.setVoiceSettings(hard());
        // Verify the very first untracked sample is literally dry.
        const float first=output[0];
        float* firstPointer[1]={output.data()};
        engine.process(firstPointer,1,1);
        safe &= output[0]==first;
        const auto start=std::chrono::steady_clock::now();
        constexpr std::array<int,5> partitions{{1,17,64,128,257}};
        for(int startFrame=1,iteration=0;startFrame<count;++iteration) {
            const int n=std::min(partitions[static_cast<std::size_t>(iteration%5)],
                                 count-startFrame);
            float* mono[1]={output.data()+startFrame};
            engine.process(mono,1,n);
            startFrame+=n;
        }
        const auto finished=std::chrono::steady_clock::now();
        const double cpuMs=std::chrono::duration<double,std::milli>(finished-start).count();
        bool finite=true;double peak=0.0,rms=0.0;
        for(int i=count/2;i<count;++i) {
            const auto x=output[static_cast<std::size_t>(i)];
            finite &= std::isfinite(x);
            peak=std::max(peak,std::abs(static_cast<double>(x)));
            rms+=static_cast<double>(x)*x;
        }
        rms=std::sqrt(rms/(count-count/2));
        const double detected=engine.sourceHz();
        const double outputHz=autocorrHz(output,rate,target);
        const double residual=outputHz>0?1200.0*std::log2(outputHz/target):9999.0;
        const bool matchedSource=engine.voiced() &&
                 std::abs(detected-input)/input<0.04 &&
                 engine.firstVoiceSample()>=0 &&
                 engine.firstHalfWetSample()>engine.firstVoiceSample();
        const bool eligible=outputHz>0 && std::abs(residual)<18.0;
        acousticallyEligible+=eligible;
        safe &= finite && peak<2.0 && rms>0.007 &&
                matchedSource && engine.researchWetMix()>0.95;
        std::cout<<"CAUSAL_HARMONIC rate="<<rate<<" input_hz="<<input
                 <<" target_hz="<<target<<" first_f0_ms="
                 <<1000.0*engine.firstVoiceSample()/rate
                 <<" first_half_wet_ms="<<1000.0*engine.firstHalfWetSample()/rate
                 <<" corrected_hz="<<outputHz<<" residual_cents="<<residual
                 <<" synthetic_18c_eligible="<<eligible
                 <<" rms="<<rms<<" peak="<<peak
                 <<" processing_ms="<<cpuMs
                 <<" source_ok="<<matchedSource<<" safe="<<finite<<"\n";
    }
    // Stereo right-only microphones: zero crosstalk and poison guards.
    {
        HarmonicLivePrototype engine;
        constexpr double rate=48000.0;
        engine.prepare(rate,2);
        engine.setVoiceSettings(hard());
        std::array<float,128> left{},right{};
        float* stereo[2]={left.data(),right.data()};
        bool finite=true,noLeak=true;
        for(int block=0;block<400;++block) {
            for(int j=0;j<128;++j) {
                left[static_cast<std::size_t>(j)]=0.0f;
                right[static_cast<std::size_t>(j)]=
                    voiced(rate,440.0*std::pow(2.0,30.0/1200.0),block*128+j);
                if(block==8 && j==10)
                    right[static_cast<std::size_t>(j)]=
                        std::numeric_limits<float>::quiet_NaN();
            }
            engine.process(stereo,2,128);
            for(int j=0;j<128;++j) {
                finite &= std::isfinite(right[static_cast<std::size_t>(j)]);
                noLeak &= left[static_cast<std::size_t>(j)]==0.0f;
            }
        }
        safe &= finite && noLeak && engine.sourceHz()>300.0;
        std::cout<<"CAUSAL_STEREO finite="<<finite<<" no_leak="<<noLeak
                 <<" acquired_source="<<engine.sourceHz()<<"\n";
    }
    std::cout<<"CAUSAL_HARMONIC_EXPERIMENT safety="<<safe
             <<" synthetic_F0_eligible="<<acousticallyEligible
             <<"/12; NOTE: neither a verified 1ms wet-delay bound nor "
               "formant/voice/ASIO certification\n";
    return safe?0:1;
}
