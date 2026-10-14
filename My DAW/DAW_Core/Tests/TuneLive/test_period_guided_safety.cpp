#include "../../Source/TuneLiveCore/Research/ApexTuneLivePeriodGuidedPrototype.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
namespace {
std::atomic<bool> checkAlloc{false};
std::atomic<int> allocationCount{0};
}
void* operator new(std::size_t n){
    if(checkAlloc.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){
    if(checkAlloc.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int main(){
    using APEX::TuneLive::Research::PeriodGuidedPrototype;
    PeriodGuidedPrototype shifter;
    std::array<float,128> left{},right{};
    float* channels[2]={left.data(),right.data()};
    bool safe=true;
    int cases=0;
    for(double sr:std::array<double,4>{44100.0,48000.0,96000.0,192000.0}){
        shifter.prepare(sr,2);
        for(double f0:std::array<double,3>{55.0,220.0,1200.0})
          for(double ratio:std::array<double,3>{0.85,1.0,1.15}){
            shifter.reset();
            shifter.setPeriodAndRatio(f0,ratio);
            safe &= shifter.requiredHistorySamples()<PeriodGuidedPrototype::kCapacity;
            for(int i=0;i<640;++i){
                for(int j=0;j<128;++j){
                    const double t=(i*128+j)/sr;
                    left[static_cast<std::size_t>(j)]=0.0f;
                    right[static_cast<std::size_t>(j)]=static_cast<float>(
                        0.28*std::sin(6.283185307179586*f0*t));
                    if((j%97)==0 && i>4)
                        right[static_cast<std::size_t>(j)]=
                            std::numeric_limits<float>::quiet_NaN();
                }
                checkAlloc.store(true,std::memory_order_relaxed);
                shifter.process(channels,2,128);
                checkAlloc.store(false,std::memory_order_relaxed);
                for(int j=0;j<128;++j){
                    safe &= std::isfinite(left[static_cast<std::size_t>(j)]);
                    safe &= std::isfinite(right[static_cast<std::size_t>(j)]);
                    safe &= left[static_cast<std::size_t>(j)]==0.0f;
                    safe &= std::abs(right[static_cast<std::size_t>(j)])<=8.0f;
                }
            }
            ++cases;
        }
    }
    shifter.setPeriodAndRatio(std::numeric_limits<double>::quiet_NaN(),1.12);
    left.fill(0.0f);right.fill(0.0f);
    checkAlloc.store(true,std::memory_order_relaxed);
    shifter.process(channels,2,128);
    checkAlloc.store(false,std::memory_order_relaxed);
    for(float sample:right) safe &= std::isfinite(sample);
    safe &= allocationCount.load(std::memory_order_relaxed)==0;
    std::cout<<"PERIOD_GUIDED safety_cases="<<cases
             <<" callback_allocations="<<allocationCount.load()
             <<" no_stereo_leak="<<safe<<"\n";
    return safe?0:1;
}
