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
#include <memory>
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
    // Benchmark the SAME phase-aligned audio path enabled in the native host.
    conversion.prepare(factor,hostBlock,factor>1?57:0);
    Core dsp;
    dsp.prepare(sampleRate*factor,factor>1);
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
    // Collect hard-deadline per-callback timing, not merely average CPU.
    // Scratch storage belongs to the benchmark, never AudioRateConverter.
    std::vector<double> blockMs((totalFrames+hostBlock-1)/hostBlock,0.0);
    size_t blockIndex=0;
    const auto start=std::chrono::steady_clock::now();
    for(int offset=0;offset<totalFrames;offset+=hostBlock)
    {
        const int count=std::min(hostBlock,totalFrames-offset);
        const auto startBlock=std::chrono::steady_clock::now();
        conversion.process(l.data()+offset,r.data()+offset,count,
                           &dsp,highRateCallback);
        const auto endBlock=std::chrono::steady_clock::now();
        blockMs[blockIndex++]=std::chrono::duration<double,std::milli>(
            endBlock-startBlock).count();
    }
    const auto end=std::chrono::steady_clock::now();
    double sink=0;
    for(int i=0;i<totalFrames;i+=113)
        sink+=l[static_cast<size_t>(i)]+r[static_cast<size_t>(i)];
    checksum+=sink;
    const double seconds=std::chrono::duration<double>(end-start).count();
    if(!warmup)
    {
        std::printf("APEX_OS_CPU,%d,%d,%d,%.8f,%.6f,%.6f\n",
                    factor,sampleRate,hostBlock,seconds,
                    seconds/(static_cast<double>(totalFrames)/sampleRate),
                    (static_cast<double>(totalFrames)/sampleRate)/seconds);
        std::sort(blockMs.begin(),blockMs.begin()+static_cast<std::ptrdiff_t>(blockIndex));
        const auto percentile=[&](double fraction) noexcept {
            const auto index=static_cast<size_t>(
                std::ceil(fraction*static_cast<double>(blockIndex-1)));
            return blockMs[index];
        };
        const double deadline=1000.0*hostBlock/sampleRate;
        const double worst=blockMs[blockIndex-1];
        std::printf("APEX_OS_BLOCK,%d,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                    factor,percentile(0.95),percentile(0.99),worst,
                    deadline,worst/deadline);
    }
    return seconds;
}

// Adversarial single realtime thread: multiple *full* native plugin DSP
// instances compete for the very SAME 128-frame audio deadline.
// All state and buffers are allocated/prepared before entering the timer.
void benchMulti(int factor,int instances,const std::vector<float>& left,
                const std::vector<float>& right)
{
    struct Instance
    {
        AudioRateConverter conversion;
        Core dsp;
        std::vector<float> left,right;
        Instance(int factor,const std::vector<float>& l,
                 const std::vector<float>& r):left(l),right(r)
        {
            conversion.prepare(factor,hostBlock,factor>1?57:0);
            dsp.prepare(sampleRate*factor);
            Settings p;
            p.truePeak=false;
            p.detectorQuality=0;
            p.gainDb=6;
            p.ceilingDb=-1;
            p.attackMs=0.25;
            p.releaseMs=120;
            p.lookaheadMs=5;
            dsp.setSettings(p);
        }
    };
    std::vector<std::unique_ptr<Instance>> paths;
    paths.reserve(static_cast<size_t>(instances));
    for(int i=0;i<instances;++i)
        paths.emplace_back(std::make_unique<Instance>(factor,left,right));
    std::vector<double> blockMs;
    blockMs.reserve(static_cast<size_t>((totalFrames+hostBlock-1)/hostBlock));
    const double deadline=1000.0*hostBlock/sampleRate;
    int deadlineMisses=0;
    for(int offset=0;offset<totalFrames;offset+=hostBlock)
    {
        const int count=std::min(hostBlock,totalFrames-offset);
        const auto begin=std::chrono::steady_clock::now();
        for(auto& path:paths)
            path->conversion.process(path->left.data()+offset,
                                     path->right.data()+offset,count,
                                     &path->dsp,highRateCallback);
        const auto end=std::chrono::steady_clock::now();
        const double elapsed=std::chrono::duration<double,std::milli>(
            end-begin).count();
        if(elapsed>deadline)++deadlineMisses;
        blockMs.push_back(elapsed);
    }
    std::sort(blockMs.begin(),blockMs.end());
    const size_t n=blockMs.size();
    const double p99=blockMs[static_cast<size_t>(
        std::ceil(0.99*static_cast<double>(n-1)))];
    const double worst=blockMs.back();
    for(const auto& path:paths)
        checksum+=path->left[static_cast<size_t>(totalFrames-113)]
                 +path->right[static_cast<size_t>(totalFrames-113)];
    std::printf("APEX_OS_MULTI,%d,%d,%d,%.6f,%.6f,%d,%zu,%.6f\n",
                factor,instances,hostBlock,p99,worst,deadlineMisses,n,deadline);
}
}
int main()
{
    const auto left=createSignal(1.0,0.18);
    const auto right=createSignal(0.81,0.92);
    std::puts("label,factor,rate_hz,host_block,processing_seconds,core_fraction,real_time_factor");
    std::puts("APEX_OS_BLOCK columns: factor,p95_ms,p99_ms,worst_ms,128frame_deadline_ms,worst_divided_by_deadline");
    for(const int factor:{1,2,4,8,16,32})
    {
        (void)bench(factor,left,right,true);
        const double a=bench(factor,left,right,false);
        const double b=bench(factor,left,right,false);
        if(!std::isfinite(a)||!std::isfinite(b)||a<=0.0||b<=0.0)return EXIT_FAILURE;
    }
    // Multi-instance GAUNTLET deliberately does NOT hard-fail: this is
    // the evidence that should determine default 32x behavior and safety
    // warnings on the user's actual Windows/Lenovo hardware.
    std::puts("APEX_OS_MULTI columns: factor,instances,block,p99_ms,worst_ms,deadline_misses,blocks,deadline_ms");
    for(const int factor:{8,16,32})
        for(const int instances:{2,4})
            benchMulti(factor,instances,left,right);
    // Volatile checksum makes the actual completed signal observable.
    std::printf("APEX_OS_CHECKSUM,%.6f\n",checksum);
}
