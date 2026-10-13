// APEX Tune Live 8-insert stress and CPU telemetry.
// Measure actual production DSP with human-ish harmonics at 48 kHz / 128,
// two stereo instances + six mono. CI machines vary: record CPU usage
// without claiming an Apollo Solo xrun result from hosted CI timings.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

int main() {
    using namespace APEX::TuneLive;
    constexpr int sampleRate=48000,block=128,totalSamples=sampleRate*2;
    constexpr int voiceCount=8;
    struct Instance {
        std::unique_ptr<APEX::TuneLive::DspCore> effect=std::make_unique<APEX::TuneLive::DspCore>();
        APEX::TuneLive::DspSettings settings;
        int channels=1;
        std::vector<float> left=std::vector<float>(128,0.0f);
        std::vector<float> right=std::vector<float>(128,0.0f);
    };
    std::array<Instance,voiceCount> inserts;
    for(int voice=0;voice<voiceCount;++voice) {
        auto& c=inserts[static_cast<std::size_t>(voice)];
        c.channels=(voice==0 || voice==7)?2:1;
        c.effect->prepare(sampleRate,block,c.channels);
        c.settings.correction=1.0f;
        c.settings.retune=0.0f;
        c.settings.humanize=0.4f;
        c.settings.flexTune=0.0f;
        c.settings.wet=1.0f;
        c.settings.stability=0.25f;
    }
    auto worstNs=std::chrono::steady_clock::duration::zero();
    double peak=0;
    const auto begin=std::chrono::steady_clock::now();
    for(int start=0;start<totalSamples;start+=block) {
        const auto tic=std::chrono::steady_clock::now();
        for(int voice=0;voice<voiceCount;++voice) {
            auto& c=inserts[static_cast<std::size_t>(voice)];
            const double pitch=110.0*std::pow(2.0,voice/4.0);
            for(int i=0;i<block;++i) {
                const double t=static_cast<double>(start+i)/sampleRate;
                const double hz=pitch*(1.0+0.002*std::sin(6.283185307179586*5.0*t));
                const double value=0.29*std::sin(6.283185307179586*hz*t)
                                 +0.07*std::sin(6.283185307179586*2*hz*t);
                c.left[static_cast<std::size_t>(i)]=static_cast<float>(value);
                c.right[static_cast<std::size_t>(i)]=static_cast<float>(value*0.85);
            }
            float* channelData[2]={c.left.data(),c.channels==2?c.right.data():nullptr};
            c.effect->process(channelData,c.channels,block,c.settings);
            for(int i=0;i<block;++i) {
                const float v=c.left[static_cast<std::size_t>(i)];
                if(!std::isfinite(v))return 1;
                peak=std::max(peak,std::abs(static_cast<double>(v)));
                if(c.channels==2 && !std::isfinite(c.right[static_cast<std::size_t>(i)]))
                    return 1;
            }
        }
        worstNs=std::max(worstNs,std::chrono::steady_clock::now()-tic);
    }
    const auto elapsed=std::chrono::duration<double>(
        std::chrono::steady_clock::now()-begin).count();
    const auto maxBlockMs=std::chrono::duration<double,std::milli>(worstNs).count();
    const double realtimeBudgetMs=1000.0*block/sampleRate;
    std::cout<<"voices="<<voiceCount<<" nominal_buffer_ms="<<realtimeBudgetMs
             <<" elapsed_simulation_sec="<<elapsed<<" stream_seconds=2"
             <<" processing_ratio_to_realtime="<<elapsed/2.0
             <<" worst_aggregate_8insert_block_ms="<<maxBlockMs
             <<" peak="<<peak<<"\n";
    if(!std::isfinite(elapsed) || !std::isfinite(peak) || peak<=0 || peak>8.0
       || elapsed<=0)return 1;
    // No machine-dependent CPU threshold on arbitrary hosted runners. APEX
    // release requires measured xrun-free hardware scheduling in real app.
    std::cout<<"PASS: eight independent inserts finite; CPU recorded only"
                " (not xrun/Antares latency certification)\n";
}
