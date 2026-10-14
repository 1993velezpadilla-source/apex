#pragma once
// APEX host parallel-mix latency research primitive, NOT activated in
// PluginChainCore. A delay line for one dry branch, prepared on control side.
// Unlike a linear-phase FIR's group delay, the dry delay itself is exact.
//
// C++17 and dependency-free: no locks, allocations or exceptions in process().
// This is a staging block for a future coordinated slot, chain, send and
// bypass PDC integration. It must not silently affect shipping plugin mixes.
#include <algorithm>
#include <cstddef>
#include <vector>

namespace DAW {
class PluginParallelDryDelayResearchCore final {
public:
    // Message/control thread only, preferably after draining realtime readers.
    // Idempotent for unchanged format; the caller may reset at transport/rate
    // boundaries. Rejects unsupported topologies/latencies without modifying
    // live state, so the caller can explicitly fail the PDC release gate.
    bool prepare(int channels, int latencySamples) {
        if(channels<1 || channels>32 || latencySamples<0 || latencySamples>131072)
            return false;
        if(channels==channels_ && latencySamples==latencySamples_)return true;
        const auto stride=static_cast<std::size_t>(latencySamples);
        std::vector<float> replacement(static_cast<std::size_t>(channels)*stride,0.f);
        history_.swap(replacement);
        channels_=channels;
        latencySamples_=latencySamples;
        cursor_=0;
        return true;
    }
    void reset() noexcept {
        std::fill(history_.begin(),history_.end(),0.f);
        cursor_=0;
    }
    int latencySamples() const noexcept {return latencySamples_;}
    int preparedChannels() const noexcept {return channels_;}

    // Replaces the dry branch in place with N exact whole-sample delay.
    // data[ch] points to at least frames valid float samples. The caller
    // ensures channels/latency match prepare(); no per-block heap operations.
    void process(float* const* data,int channels,int frames) noexcept {
        if(!data || frames<=0 || channels<=0 || channels>channels_)return;
        if(latencySamples_==0)return;
        const auto stride=static_cast<std::size_t>(latencySamples_);
        for(int n=0;n<frames;++n) {
            for(int ch=0;ch<channels;++ch) {
                auto* ptr=data[ch];
                if(!ptr)continue;
                const auto index=static_cast<std::size_t>(ch)*stride+cursor_;
                const float delayed=history_[index];
                history_[index]=ptr[n];
                ptr[n]=delayed;
            }
            if(++cursor_>=stride)cursor_=0;
        }
    }
private:
    std::vector<float> history_{};
    int channels_=0;
    int latencySamples_=0;
    std::size_t cursor_=0;
};
} // namespace DAW
