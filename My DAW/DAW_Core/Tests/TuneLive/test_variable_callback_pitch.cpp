// Actual 40ms production audio correction under heterogeneous host callbacks.
// Previous PDC test exercised 257-sample chunks with bypass ON only; this
// covers the fully processed, singing-wet signal and real YIN remeasurement.
// GUI/host-independent. This is not ASIO scheduling or vocal fidelity proof.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {
constexpr int sr=48000,prepared=128,frames=48000;
constexpr double pi=3.1415926535897932384626433832795;
struct Scenario {
    std::array<int,5> blocks;
    const char* name;
};
bool test(const Scenario& scenario,int channels,int note) {
    using namespace APEX::TuneLive;
    const double expected=(note==3)?220.0:440.0;
    const double input=expected*std::pow(2.0,30.0/1200.0);
    std::vector<float> left(frames),right(frames);
    double phase=0.0;
    for(int i=0;i<frames;++i) {
        phase+=2*pi*input/sr;
        const float sample=static_cast<float>(0.29*std::sin(phase)
                                               +0.055*std::sin(2*phase));
        left[static_cast<std::size_t>(i)]=sample;
        if(channels==2)right[static_cast<std::size_t>(i)]=0.77f*sample;
    }
    DspCore core;
    core.prepare(sr,prepared,channels);
    DspSettings settings;
    settings.correction=1.0f;
    settings.retune=0.0f;
    settings.humanize=0.0f;
    settings.flexTune=0.0f;
    settings.wet=1.0f;
    settings.stability=0.20f;
    settings.key=9;
    settings.scale=0;
    std::size_t chunk=0;
    for(int i=0;i<frames;) {
        const int count=std::min(frames-i,scenario.blocks[chunk%scenario.blocks.size()]);
        float* ptr[2]={left.data()+i,channels==2?right.data()+i:nullptr};
        core.process(ptr,channels,count,settings);
        ++chunk;
        i+=count;
    }
    PitchTracker measured;
    measured.prepare(sr);
    double peak=0.0;
    for(int i=frames-sr/3;i<frames;++i) {
        const float sample=left[static_cast<std::size_t>(i)];
        if(!std::isfinite(sample))return false;
        measured.push(sample);
        peak=std::max(peak,std::abs(static_cast<double>(sample)));
        if(channels==2 && !std::isfinite(right[static_cast<std::size_t>(i)]))
            return false;
    }
    const auto f0=measured.latest();
    const double error=f0.voiced?1200.0*std::log2(f0.hz/expected):9999.0;
    std::cout<<"TUNE_LIVE_WET_VARIABLE_HOST_CALLBACK"
             <<" plan="<<scenario.name
             <<" channels="<<channels<<" note="<<(note==3?"A3":"A4")
             <<" callback_count="<<chunk
             <<" PDC="<<core.latencySamples()
             <<" source_cents=30"
             <<" corrected_cents="<<error
             <<" peak="<<peak<<"\n";
    return f0.voiced && std::isfinite(error) && std::abs(error)<18.0
       && peak>0.01 && peak<2.0 && core.latencySamples()==1920;
}
}
int main(){
    constexpr std::array<Scenario,5> plans{{
        {{{128,128,128,128,128}},"native128"},
        {{{1,7,64,128,257}},"mixed_host"},
        {{{32,32,32,32,32}},"smaller32"},
        {{{512,512,512,512,512}},"larger512"},
        {{{17,257,3,521,64}},"irregular"},
    }};
    bool pass=true;
    for(const auto& plan:plans)for(int channels=1;channels<=2;++channels)
        for(int note:std::array<int,2>{{3,4}})
            pass=test(plan,channels,note)&&pass;
    if(!pass){
        std::cerr<<"FAIL: wet voice accuracy depends on host callback partition\n";
        return 1;
    }
    std::cout<<"TUNE_LIVE_WET_VARIABLE_CALLBACK_PASS: 20 real DSP+F0 runs; "
                "48k/128 prepared, 1..521 sample live host chunks\n";
}
