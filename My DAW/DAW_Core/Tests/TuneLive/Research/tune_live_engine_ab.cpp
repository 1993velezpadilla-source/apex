// Non-shipping offline A/B. AGPL reference builds as a separate executable.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef APEX_FLAYR
#include "TuneEngine.h"
#else
#include "../../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include "../../../Source/TuneLiveCore/ApexTuneLiveRealtimeCore.h"
#endif

namespace {
constexpr int sr=48000,block=128,samples=sr*4;
constexpr double tau=6.283185307179586;
void le16(std::ofstream& f,uint16_t v) {
    for(int i=0;i<2;++i){const char c=static_cast<char>(v>>(8*i));f.write(&c,1);}
}
void le32(std::ofstream& f,uint32_t v) {
    for(int i=0;i<4;++i){const char c=static_cast<char>(v>>(8*i));f.write(&c,1);}
}
void writeWav(const std::filesystem::path& path,const std::vector<float>& a) {
    std::ofstream f(path,std::ios::binary);
    if(!f)throw std::runtime_error("cannot create WAV");
    f.write("RIFF",4);le32(f,36+static_cast<uint32_t>(a.size()*4));
    f.write("WAVEfmt ",8);le32(f,16);le16(f,3);le16(f,1);
    le32(f,sr);le32(f,sr*4);le16(f,4);le16(f,32);
    f.write("data",4);le32(f,static_cast<uint32_t>(a.size()*4));
    for(float x:a){uint32_t n=0;std::memcpy(&n,&x,4);le32(f,n);}
}
double fade(double u) {
    u=std::clamp(u,0.0,1.0);return u*u*(3-2*u);
}
double gap(double t,double a,double b) {
    constexpr double r=.003;
    if(t<a-r||t>b+r)return 1;
    if(t<a)return 1-fade((t-a+r)/r);
    if(t<b)return 0;
    return fade((t-b)/r);
}
std::vector<float> makeInput() {
    std::vector<float> a(samples);
    double phase=0;
    for(int i=0;i<samples;++i){
        const double t=static_cast<double>(i)/sr;
        const double f=(t<.7?220:t<1.4?440:t<2.2?196:440)*
             std::exp2((28+12*std::sin(tau*5.1*t))/1200);
        phase+=tau*f/sr;
        const double env=std::min(1.0,t/.01)*gap(t,.37,.378)*
             gap(t,1.03,1.037)*gap(t,1.69,1.83);
        a[i]=static_cast<float>(env*(.26*std::sin(phase)+
             .078*std::sin(2*phase+.25)+.042*std::sin(3*phase-.5)));
    }
    return a;
}
double percentile(std::vector<double> v,double p) {
    if(v.empty())return 0;
    const size_t i=static_cast<size_t>((v.size()-1)*p);
    std::nth_element(v.begin(),v.begin()+static_cast<std::ptrdiff_t>(i),v.end());
    return v[i];
}
template<class Process>
void evaluate(const std::string& engine,const std::string& mode,
              int latency,const std::vector<float>& input,Process&& process,
              const std::filesystem::path& out) {
    std::vector<float> audio=input;
    std::vector<double> elapsed,steps;
    elapsed.reserve(samples/block);steps.reserve(samples);
    int deadlineMiss=0,nonfinite=0,clipped=0;
    for(int i=0;i<samples;i+=block){
        const int n=std::min(block,samples-i);
        float* channels[]={audio.data()+i};
        const auto start=std::chrono::steady_clock::now();
        process(channels,n);
        const double ms=std::chrono::duration<double,std::milli>(
           std::chrono::steady_clock::now()-start).count();
        elapsed.push_back(ms);
        if(ms>1000.0*n/sr)++deadlineMiss;
    }
    double peak=0;
    for(int i=0;i<samples;++i){
        const double x=audio[i];
        if(!std::isfinite(x)){++nonfinite;continue;}
        peak=std::max(peak,std::abs(x));
        if(std::abs(x)>1)++clipped;
        if(i>0&&std::isfinite(audio[i-1]))steps.push_back(std::abs(x-audio[i-1]));
    }
    const double elapsedTotal=std::accumulate(elapsed.begin(),elapsed.end(),0.0);
    std::cout<<std::fixed<<std::setprecision(6)
      <<"{\"engine\":\""<<engine<<"\",\"mode\":\""<<mode
      <<"\",\"rate\":"<<sr<<",\"frames\":"<<samples<<",\"block\":"<<block
      <<",\"reported_latency\":"<<latency
      <<",\"cpu_realtime_ratio\":"<<elapsedTotal/(1000.0*samples/sr)
      <<",\"callback_p95_ms\":"<<percentile(elapsed,.95)
      <<",\"callback_p99_ms\":"<<percentile(elapsed,.99)
      <<",\"callback_max_ms\":"<<*std::max_element(elapsed.begin(),elapsed.end())
      <<",\"nominal_deadline_exceed_count\":"<<deadlineMiss
      <<",\"sample_max_step\":"
      <<*std::max_element(steps.begin(),steps.end())
      <<",\"sample_p999_step\":"<<percentile(steps,.999)
      <<",\"peak\":"<<peak<<",\"samples_over_0dbfs\":"<<clipped
      <<",\"nonfinite\":"<<nonfinite<<"}\n";
    writeWav(out/(engine+"_"+mode+".wav"),audio);
    if(nonfinite>0||peak>=4)throw std::runtime_error("nonfinite or runaway reference");
}
}
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"usage: engine-ab <output-dir>\n";return 2;}
    const std::filesystem::path out=argv[1];
    std::filesystem::create_directories(out);
    const auto dry=makeInput();
    writeWav(out/"input.wav",dry);
#ifdef APEX_FLAYR
    for(bool live:{true,false}){
        auto e=std::make_unique<tune::Engine>();e->prepare(sr,1);
        tune::Params p;p.liveMode=live;p.key=0;p.scale=0;p.retuneMs=0;
        p.humanize=0;p.expression=0;p.mix=1;p.formantPreserve=true;
        e->setParams(p);
        evaluate("flayr",live?"live":"studio",e->getLatencySamples(live),
                 dry,[&](float* const* data,int n){e->process(data,1,n);},out);
    }
#else
    {
        auto e=std::make_unique<APEX::TuneLive::RealtimeCore>();
        e->prepare(sr,1);
        APEX::TuneLive::DspSettings p;
        p.correction=p.wet=1;p.retune=p.humanize=p.flexTune=0;
        evaluate("apex","tracking",e->latencySamples(),dry,
                 [&](float* const* data,int n){e->process(data,1,n,p);},out);
    }
    {
        auto e=std::make_unique<APEX::TuneLive::DspCore>();
        e->prepare(sr,block,1);
        APEX::TuneLive::DspSettings p;
        p.correction=p.wet=1;p.retune=p.humanize=p.flexTune=0;
        evaluate("apex","studio",e->latencySamples(),dry,
                 [&](float* const* data,int n){e->process(data,1,n,p);},out);
    }
#endif
}
