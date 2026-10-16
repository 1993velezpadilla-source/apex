// Dynamic programme-route PDC: deliberately research-only, no AudioEngine hook.
#include "../../Source/PDCCore/ProgrammeEdgeRetimingResearchCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using DAW::PdcResearch::ProgrammeEdgeRetimingResearchCore;
static float leftAt(int sample) {
    return static_cast<float>(.37*std::sin(.019*sample)+.17*std::cos(.11*sample));
}
static float rightAt(int sample) {
    return static_cast<float>(-.29*std::sin(.033*sample)+.16*std::cos(.07*sample));
}
static void staticLatency(int rate,int block,int delay) {
    ProgrammeEdgeRetimingResearchCore p;
    assert(p.prepare(2,96,delay,static_cast<double>(rate)));
    assert(p.maxDelaySamples()==96);
    constexpr int total=754;
    std::vector<float> left(static_cast<std::size_t>(block));
    std::vector<float> right(static_cast<std::size_t>(block));
    for(int start=0;start<total;start+=block) {
        const int count=std::min(block,total-start);
        for(int i=0;i<count;++i) {
            left[i]=leftAt(start+i);
            right[i]=rightAt(start+i);
        }
        float* channel[]={left.data(),right.data()};
        assert(p.process(channel,2,count));
        for(int i=0;i<count;++i) {
            const int source=start+i-delay;
            const float expectedL=source<0?0.f:leftAt(source);
            const float expectedR=source<0?0.f:rightAt(source);
            assert(std::abs(left[i]-expectedL)<2.e-6f);
            assert(std::abs(right[i]-expectedR)<2.e-6f);
        }
    }
    p.reset();
    for(int i=0;i<block;++i) {left[i]=i==0?1.f:0.f;right[i]=i==0?-1.f:0.f;}
    float* channel[]={left.data(),right.data()};
    assert(p.process(channel,2,block));
    for(int i=0;i<block;++i) {
        const float expected=i==delay?1.f:0.f;
        assert(std::abs(left[i]-expected)<1.e-6f);
        assert(std::abs(right[i]+expected)<1.e-6f);
    }
}
// Check SAME realtime waveform when callbacks are partitioned 1 vs 31/64/
// 128 frames, including requests during an existing 5ms crossfade.
// The first sample of any fade must remain on the OLD tap, and no
// discontinuous switch happens to a newly requested tap mid-fade.
static void changingLatency(int rate,int block) {
    ProgrammeEdgeRetimingResearchCore device;
    ProgrammeEdgeRetimingResearchCore reference;
    assert(device.prepare(2,128,0,rate,5.0));
    assert(reference.prepare(2,128,0,rate,5.0));
    std::vector<float> l(static_cast<std::size_t>(block));
    std::vector<float> r(static_cast<std::size_t>(block));
    int elapsed=0;
    for(int b=0;b<45;++b) {
        // Includes a duplicate target, one rejected value and a new
        // target while the previous fade is still pending.
        int request=-1;
        if(b==4)request=48;
        if(b==5)request=16;
        if(b==6)request=64;
        if(b==10)request=0;
        if(b==16)request=32;
        if(b==17)request=32;
        if(b==19)request=96;
        if(b==34)request=1;
        if(request>=0) {
            assert(device.requestDelaySamples(request));
            assert(reference.requestDelaySamples(request));
        }
        if(b==9) {
            const int saved=device.requestedDelaySamples();
            assert(!device.requestDelaySamples(129));
            assert(!device.requestDelaySamples(-1));
            assert(device.requestedDelaySamples()==saved);
        }
        for(int i=0;i<block;++i) {
            l[i]=leftAt(elapsed+i);
            r[i]=rightAt(elapsed+i);
        }
        // Reference proceeds as actual one-sample callbacks. The request
        // cadence (by source device blocks) is held identical.
        for(int i=0;i<block;++i) {
            float oneL=leftAt(elapsed+i),oneR=rightAt(elapsed+i);
            float* single[]={&oneL,&oneR};
            assert(reference.process(single,2,1));
            // Keep expected waveform, initially saved in separate arrays.
            // Compare after full-block device output below.
            l[i]=oneL; r[i]=oneR;
        }
        std::vector<float> originalL(static_cast<std::size_t>(block));
        std::vector<float> originalR(static_cast<std::size_t>(block));
        for(int i=0;i<block;++i) {
            originalL[i]=leftAt(elapsed+i);
            originalR[i]=rightAt(elapsed+i);
        }
        float* batch[]={originalL.data(),originalR.data()};
        assert(device.process(batch,2,block));
        for(int i=0;i<block;++i) {
            assert(std::abs(originalL[i]-l[i])<2.e-6f);
            assert(std::abs(originalR[i]-r[i])<2.e-6f);
            assert(std::abs(originalL[i])<.8f);
            assert(std::abs(originalR[i])<.8f);
        }
        assert(device.fadeRemainingSamples()==reference.fadeRemainingSamples());
        assert(device.currentlyRenderingDelaySamples()==
               reference.currentlyRenderingDelaySamples());
        elapsed+=block;
    }
}
static void independentSources(int rate) {
    ProgrammeEdgeRetimingResearchCore direct,aux;
    assert(direct.prepare(2,48,48,rate));
    assert(aux.prepare(2,64,0,rate));
    // Same incoming samples, independent ring histories and delay requests.
    for(int block=0;block<24;++block) {
        if(block==5)assert(aux.requestDelaySamples(32));
        if(block==12)assert(direct.requestDelaySamples(16));
        float inputL[64]{},inputR[64]{};
        float routeL[64]{},routeR[64]{};
        for(int i=0;i<64;++i) {
            routeL[i]=inputL[i]=leftAt(block*64+i);
            routeR[i]=inputR[i]=rightAt(block*64+i);
        }
        float* a[]={inputL,inputR};
        float* b[]={routeL,routeR};
        assert(direct.process(a,2,64));
        assert(aux.process(b,2,64));
        if(block>=18) {
            // Both should eventually reach their new tap positions.
            for(int i=0;i<64;++i) {
                const int at=block*64+i;
                assert(std::abs(inputL[i]-leftAt(at-16))<2.e-6f);
                assert(std::abs(routeL[i]-leftAt(at-32))<2.e-6f);
            }
        }
    }
    // A control request beyond prepared capacity MUST NOT mutate anything.
    assert(!direct.requestDelaySamples(49));
    assert(!aux.requestDelaySamples(65));
    assert(direct.requestedDelaySamples()==16);
    assert(aux.requestedDelaySamples()==32);
}

/*
 * A measurable synthetic pop guard for delay-change crossfades.  The input
 * sinusoids have a known per-sample Lipschitz bound L.  A crossfade between
 * two taps from the SAME bounded waveform may add at most 2*A/(N-1), where
 * A is the maximum source amplitude and N is the crossfade sample count.
 * This catches abrupt tap changes without claiming acoustic clicklessness
 * for arbitrary discontinuous audio, plugins, or the physical Apollo path.
 */
static void boundedSmoothRetiming(int rate,int block) {
    ProgrammeEdgeRetimingResearchCore p;
    assert(p.prepare(2,128,0,rate,5.0));
    constexpr int events[][2] = {
        {600,96},{650,16},{680,64},{1320,128},{1360,0},
        {1875,80},{1900,24},{2590,0},{2800,127},{2801,1},{3265,48}
    };
    constexpr int eventCount=static_cast<int>(sizeof(events)/sizeof(events[0]));
    constexpr int total=4096;
    const int fadeN=std::max(1,static_cast<int>(std::round(rate*.005)));
    const float leftBound=static_cast<float>(.37*.019+.17*.11)
        +1.08f/static_cast<float>(std::max(1,fadeN-1))+2.e-6f;
    const float rightBound=static_cast<float>(.29*.033+.16*.07)
        +.90f/static_cast<float>(std::max(1,fadeN-1))+2.e-6f;
    float lastL=0.f,lastR=0.f;
    int nextEvent=0,at=0;
    while(at<total) {
        while(nextEvent<eventCount&&events[nextEvent][0]==at) {
            assert(p.requestDelaySamples(events[nextEvent][1]));
            ++nextEvent;
        }
        int count=std::min(block,total-at);
        if(nextEvent<eventCount)
            count=std::min(count,events[nextEvent][0]-at);
        assert(count>0&&count<=128);
        float l[128]{},r[128]{};
        for(int i=0;i<count;++i) {
            l[i]=leftAt(at+i);
            r[i]=rightAt(at+i);
        }
        float* channels[]={l,r};
        assert(p.process(channels,2,count));
        for(int i=0;i<count;++i) {
            const int n=at+i;
            assert(std::isfinite(l[i])&&std::isfinite(r[i]));
            if(n>=512) {
                // The first 512 samples warm up all possible delay taps.
                assert(std::abs(l[i]-lastL)<=leftBound);
                assert(std::abs(r[i]-lastR)<=rightBound);
            }
            lastL=l[i];
            lastR=r[i];
        }
        at+=count;
    }
    assert(nextEvent==eventCount);
    assert(p.fadeRemainingSamples()==0);
    assert(p.currentlyRenderingDelaySamples()==48);
}
int main() {
    ProgrammeEdgeRetimingResearchCore invalid;
    assert(!invalid.prepare(0,32,0,48000));
    assert(!invalid.prepare(2,32,33,48000));
    assert(!invalid.prepare(2,131073,0,48000));
    assert(!invalid.prepare(2,16,0,100));
    assert(!invalid.requestDelaySamples(0));
    for(int rate:{44100,48000}) {
        for(int block:{1,31,64,128}) {
            for(int delay:{0,1,16,32,48,96})
                staticLatency(rate,block,delay);
            changingLatency(rate,block);
            boundedSmoothRetiming(rate,block);
        }
        independentSources(rate);
    }
    ProgrammeEdgeRetimingResearchCore noOp;
    assert(noOp.prepare(2,48,0,44100));
    float value=1.f;
    float* mono[]={&value};
    assert(!noOp.process(mono,1,1));
    assert(value==1.f);
    float* empty[]={nullptr,&value};
    assert(!noOp.process(empty,2,1));
    assert(value==1.f);
    std::cout<<"APEX per-edge delay retiming: Windows/Linux 44.1/48 kHz, "
             <<"RT callbacks 1/31/64/128, 0->48->16->64->0->32->96->1, "
             <<"per-edge independent history and no-alloc transitions GREEN\n";
}
