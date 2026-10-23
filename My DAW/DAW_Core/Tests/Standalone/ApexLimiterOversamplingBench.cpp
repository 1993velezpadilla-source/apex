// APEX Bubblegum Limiter — real AUDIO oversampling performance audit.
// Run on the runner that hosts the DAW; timings from shared CI hardware
// are diagnostic measurements, NEVER a realtime-audio guarantee.
// Original C++17, no JUCE or external libraries.
#include "../../Source/LimiterCore/ApexLimiterCore.h"
#include "../../Source/LimiterCore/ApexLimiterOversampling.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

using APEX::Limiter::AudioRateConverter;
using APEX::Limiter::Core;
using APEX::Limiter::Settings;
namespace {
constexpr int sampleRate=48000;
constexpr int totalFrames=48000;
constexpr int hostBlock=128;
constexpr double pi=3.14159265358979323846;
volatile double checksum=0.0;

void highRateCallback(void* context,float* left,float* right,int frames) noexcept
{
    static_cast<Core*>(context)->process(left,right,frames);
}
std::vector<float> createSignal(double gain,double phase)
{
    std::vector<float> audio(static_cast<size_t>(totalFrames));
    for(int i=0;i<totalFrames;++i)
    {
        const double time=static_cast<double>(i)/sampleRate;
        const double wave=0.24*std::sin(2*pi*220*time+phase)
                         +0.18*std::sin(2*pi*1760*time+phase*0.7)
                         +0.16*std::sin(2*pi*7193*time+phase*0.9);
        const double transient=i%4096<9?0.33:0.0;
        audio[static_cast<size_t>(i)]=static_cast<float>((wave+transient)*gain);
    }
    return audio;
}
double bench(int factor,const std::vector<float>& left,
             const std::vector<float>& right,bool warmup)
{
    AudioRateConverter conversion;
    conversion.prepare(factor,hostBlock);
    Core dsp;
    dsp.prepare(sampleRate*factor);
    Settings p;
    p.truePeak=false;      // The actual 32x path detects full-rate samples.
    p.detectorQuality=0;   // No redundant cubic/FIR peak detector.
    p.gainDb=6;
    p.ceilingDb=-1;
    p.attackMs=0.25;
    p.releaseMs=120;
    p.lookaheadMs=5;
    dsp.setSettings(p);
    auto l=left,r=right;  // Copies outside the timing window.
    const auto start=std::chrono::steady_clock::now();
    for(int offset=0;offset<totalFrames;offset+=hostBlock)
    {
        const int count=std::min(hostBlock,totalFrames-offset);
        conversion.process(l.data()+offset,r.data()+offset,count,
                           &dsp,highRateCallback);
    }
    const auto end=std::chrono::steady_clock::now();
    double sink=0;
    for(int i=0;i<totalFrames;i+=113)
        sink+=l[static_cast<size_t>(i)]+r[static_cast<size_t>(i)];
    checksum+=sink;
    const double seconds=std::chrono::duration<double>(end-start).count();
    if(!warmup)
        std::printf("APEX_OS_CPU,%d,%d,%d,%.8f,%.6f,%.6f\n",
                    factor,sampleRate,hostBlock,seconds,
                    seconds/(static_cast<double>(totalFrames)/sampleRate),
                    (static_cast<double>(totalFrames)/sampleRate)/seconds);
    return seconds;
}
}
int main()
{
    const auto left=createSignal(1.0,0.18);
    const auto right=createSignal(0.81,0.92);
    std::puts("label,factor,rate_hz,host_block,processing_seconds,core_fraction,real_time_factor");
    for(const int factor:{1,2,4,8,16,32})
    {
        (void)bench(factor,left,right,true);
        const double a=bench(factor,left,right,false);
        const double b=bench(factor,left,right,false);
        if(!std::isfinite(a)||!std::isfinite(b)||a<=0.0||b<=0.0)return EXIT_FAILURE;
    }
    // Volatile checksum makes the actual completed signal observable.
    std::printf("APEX_OS_CHECKSUM,%.6f\n",checksum);
}
