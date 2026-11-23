// Strict allocation watchdog for NON-SHIPPING causal Tune Live research.
// Tests the real streaming F0 tracker, early-onset seed, phase synthesis and
// residual preservation under repeated small/large callback partitions.
// 'No callback allocation' does NOT imply 1ms of tonal reaction or zero xrun.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <cstddef>

namespace {
std::atomic<int> gCallbackAllocs{0};
std::atomic<bool> gWatching{false};
constexpr double pi=3.14159265358979323846;
}
void* operator new(std::size_t count) {
    if(gWatching.load(std::memory_order_relaxed))
        gCallbackAllocs.fetch_add(1,std::memory_order_relaxed);
    if(void* result=std::malloc(count?count:1))return result;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t count) {
    if(gWatching.load(std::memory_order_relaxed))
        gCallbackAllocs.fetch_add(1,std::memory_order_relaxed);
    if(void* result=std::malloc(count?count:1))return result;
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int main() {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    bool ok=true;
    for(double rate:std::array<double,3>{{44100.0,48000.0,96000.0}})
        for(int channels:std::array<int,2>{{1,2}}) {
            HarmonicLivePrototype engine;
            engine.prepare(rate,channels);
            APEX::TuneLive::VoiceSettings settings;
            settings.root=9;settings.scale=0;settings.correction=1.0f;
            settings.referenceHz=440.0f;settings.humanize=0.0f;
            settings.flexTune=0.0f;settings.noteHysteresis=0.1f;
            engine.setVoiceSettings(settings);
            engine.enableEarlyOnset(true);
            engine.enableResidualPreservation(true);
            std::array<float,257> left{},right{};
            float* samples[2]={left.data(),channels==2?right.data():nullptr};
            constexpr std::array<int,7> callback{{1,2,17,48,64,128,257}};
            bool finite=true,noStereoLeak=true;
            double sumEnergy=0.0;
            int counter=0;
            gCallbackAllocs.store(0,std::memory_order_relaxed);
            gWatching.store(true,std::memory_order_relaxed);
            for(int b=0;b<500;++b) {
                const int n=callback[static_cast<std::size_t>(b%callback.size())];
                for(int i=0;i<n;++i) {
                    const double t=static_cast<double>(counter+i)/rate;
                    const double f=447.691+12.0*std::sin(2.0*pi*1.2*t);
                    // Deliberate harmonic vocal-like signal (not real voice).
                    const float voice=static_cast<float>(
                        0.24*std::sin(2.0*pi*f*t)
                        +0.06*std::sin(2.0*pi*f*2.0*t+0.2));
                    left[static_cast<std::size_t>(i)]=channels==2?0.0f:voice;
                    right[static_cast<std::size_t>(i)]=channels==2?voice:0.0f;
                }
                engine.process(samples,channels,n);
                for(int i=0;i<n;++i) {
                    finite &=std::isfinite(left[static_cast<std::size_t>(i)]);
                    if(channels==2) {
                        finite &=std::isfinite(right[static_cast<std::size_t>(i)]);
                        noStereoLeak &=left[static_cast<std::size_t>(i)]==0.0f;
                        sumEnergy+=std::abs(right[static_cast<std::size_t>(i)]);
                    } else sumEnergy+=std::abs(left[static_cast<std::size_t>(i)]);
                }
                counter+=n;
            }
            gWatching.store(false,std::memory_order_relaxed);
            const auto allocs=gCallbackAllocs.load(std::memory_order_relaxed);
            const bool pass=allocs==0 && finite && noStereoLeak &&
                       sumEnergy>10.0 && engine.firstHalfWetSample()>=0 &&
                       engine.researchWetMix()>0.95;
            ok &=pass;
            std::printf("CAUSAL_RT_NO_ALLOC sr=%.0f channels=%d callbacks=500"
                        " samples=%d new_calls=%d finite=%d stereo_isolated=%d"
                        " wet=%.5f energy=%.3f pass=%d\n",
                        rate,channels,counter,allocs,finite,noStereoLeak,
                        engine.researchWetMix(),sumEnergy,pass);
        }
    std::printf("CAUSAL_RT_NO_ALLOC verdict=%s"
                " (not device xruns/1ms first-note response)\n",ok?"PASS":"FAIL");
    return ok?0:1;
}
