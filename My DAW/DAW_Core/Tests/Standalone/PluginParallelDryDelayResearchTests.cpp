// APEX research: prepare-time, allocation-free-in-callback dry-branch PDC.
// This tests the reusable primitive, not the production PluginChainCore.
#include "../../Source/PluginHostCore/PluginParallelDryDelayResearchCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static void test(int latency,int frames) {
    constexpr int n=511;
    DAW::PluginParallelDryDelayResearchCore delay;
    assert(delay.prepare(2,latency));
    assert(delay.preparedChannels()==2);
    assert(delay.latencySamples()==latency);
    std::vector<float> origL(n),origR(n);
    for(int i=0;i<n;++i) {
        origL[i]=static_cast<float>(.3*std::sin(.051*i)+.05*std::cos(.173*i));
        origR[i]=static_cast<float>(-.2*std::cos(.039*i));
    }
    auto l=origL, r=origR;
    for(int start=0;start<n;start+=frames) {
        float* ptrs[]={l.data()+start,r.data()+start};
        delay.process(ptrs,2,std::min(frames,n-start));
    }
    for(int i=0;i<n;++i) {
        const float expectedL=i>=latency?origL[i-latency]:0.f;
        const float expectedR=i>=latency?origR[i-latency]:0.f;
        assert(std::abs(l[i]-expectedL)<1.e-7f);
        assert(std::abs(r[i]-expectedR)<1.e-7f);
    }
    delay.reset();
    auto againL=origL,againR=origR;
    for(int start=0;start<n;start+=31) {
        float* ptrs[]={againL.data()+start,againR.data()+start};
        delay.process(ptrs,2,std::min(31,n-start));
    }
    assert(againL==l);
    assert(againR==r);
}

int main() {
    for(int latency:{0,1,16,32,96,255})
        for(int frames:{1,31,64,128})test(latency,frames);

    DAW::PluginParallelDryDelayResearchCore config;
    assert(config.prepare(2,32));
    assert(config.prepare(2,32)); // no change means no state flush
    assert(!config.prepare(0,32));
    assert(!config.prepare(33,32));
    assert(!config.prepare(2,-1));
    assert(!config.prepare(2,131073));
    assert(config.preparedChannels()==2);
    assert(config.latencySamples()==32);

    // Exact per-sample delay is rate-independent: 32/44.1k and 32/48k.
    const double ms44=1000.0*32.0/44100.0;
    const double ms48=1000.0*32.0/48000.0;
    assert(ms44>.72 && ms44<.73);
    assert(ms48>.66 && ms48<.67);
    std::cout<<"APEX parallel dry delay C++17: 0/1/16/32/96/255 samples,"
             <<" 1/31/64/128 frames, stereo/reset/invalid contracts GREEN\\n";
}
