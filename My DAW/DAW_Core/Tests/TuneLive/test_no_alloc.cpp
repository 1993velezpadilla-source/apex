// APEX Tune Live — allocation sentinel around production DspCore callback.
// The global scalar/array new guards do not detect malloc-only external libs;
// native Windows RT callback auditing remains a separate full-host gate.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

namespace {
std::atomic<bool> watchAllocations{false};
std::atomic<unsigned int> allocations{0};
}

void* operator new(std::size_t size) {
    if(watchAllocations.load(std::memory_order_relaxed))
        allocations.fetch_add(1,std::memory_order_relaxed);
    if(auto* ptr=std::malloc(size==0?1:size)) return ptr;
    throw std::bad_alloc();
}
void operator delete(void* ptr) noexcept {std::free(ptr);}
void operator delete(void* ptr,std::size_t) noexcept {std::free(ptr);}
void* operator new[](std::size_t size) {
    if(watchAllocations.load(std::memory_order_relaxed))
        allocations.fetch_add(1,std::memory_order_relaxed);
    if(auto* ptr=std::malloc(size==0?1:size)) return ptr;
    throw std::bad_alloc();
}
void operator delete[](void* ptr) noexcept {std::free(ptr);}
void operator delete[](void* ptr,std::size_t) noexcept {std::free(ptr);}

int main() {
    constexpr int sr=48000;
    constexpr int block=128;
    constexpr int samples=sr*3;
    std::vector<float> left(samples),right(samples);
    for(int i=0;i<samples;++i) {
        const auto t=static_cast<double>(i)/sr;
        left[static_cast<std::size_t>(i)]=
           static_cast<float>(0.31*std::sin(6.283185307179586*223.0*t));
        right[static_cast<std::size_t>(i)]=
           static_cast<float>(0.28*std::sin(6.283185307179586*223.0*t));
    }
    APEX::TuneLive::DspCore dsp;
    dsp.prepare(sr,block,2);
    APEX::TuneLive::DspSettings settings;
    // First process full silence and voice to prime all lazy buffers.
    for(int off=0;off<sr;++off) {
        // intentionally the prepared playback path only; no RT measuring yet
        if(off%block==0) {
            float* channels[2]={left.data()+off,right.data()+off};
            dsp.process(channels,2,std::min(block,sr-off),settings);
        }
    }
    allocations.store(0);
    watchAllocations.store(true,std::memory_order_seq_cst);
    for(int off=sr;off<samples;off+=block) {
        float* channels[2]={left.data()+off,right.data()+off};
        settings.humanize=off%512==0?0.0f:0.8f;
        settings.preserveFormants=(off%1024 != 0);
        dsp.process(channels,2,std::min(block,samples-off),settings);
    }
    watchAllocations.store(false,std::memory_order_seq_cst);
    const unsigned int measured=allocations.load();
    std::cout<<"callback_cpp_heap_allocations="<<measured<<"\n";
    if(measured!=0) {
        std::cerr<<"FAIL: production DSP allocated with operator new in audio callback\n";
        return 1;
    }
    for(int i=sr;i<samples;++i)
        if(!std::isfinite(left[static_cast<std::size_t>(i)])
          || !std::isfinite(right[static_cast<std::size_t>(i)])) return 1;
    std::cout<<"PASS: no scalar/array new during 2 sec streaming voice at 48k/128\n";
}
