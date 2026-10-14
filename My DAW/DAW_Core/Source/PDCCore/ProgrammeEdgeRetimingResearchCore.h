#pragma once
// APEX PDC — research-only smooth per-edge delay retiming.
// Not wired to shipping AudioEngine, MasterPdcCore or Opto 2A.
//
// prepare(): control thread only; preallocates and zeroes history.
// requestDelaySamples(): atomic target publication; never changes allocation.
// process(): realtime callback, bounded per-sample work, no allocations/locks.
// Each programme edge MUST own its own instance. Same raw input routed twice
// requires two independently primed histories (not a dest-node-indexed ring).
//
// WARNING: delay retiming crossfades signal phase/history rather than moving
// audio in time transparently. Graph changes may require a full coordinated
// destination-plan commit; this building block is NOT a graph-wide PDC fix.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace DAW::PdcResearch {
class ProgrammeEdgeRetimingResearchCore final {
public:
    static constexpr int maxSupportedDelay=131072;
    bool prepare(int channels,int maxDelay,int initialDelay,
                 double sampleRate,double fadeMilliseconds=5.0) {
        if(channels<1||channels>8||maxDelay<0||maxDelay>maxSupportedDelay ||
           initialDelay<0||initialDelay>maxDelay ||
           !std::isfinite(sampleRate)||sampleRate<8000||sampleRate>384000 ||
           !std::isfinite(fadeMilliseconds)||fadeMilliseconds<0 ||
           fadeMilliseconds>100)
            return false;
        const auto stride=static_cast<std::size_t>(maxDelay)+1;
        // All allocations occur before the bank is made visible to an audio
        // callback. A failed validation never destroys a live prepared bank.
        std::vector<float> history(static_cast<std::size_t>(channels)*stride,0.f);
        history_.swap(history);
        channels_=channels;
        stride_=stride;
        maxDelay_=maxDelay;
        fadeSamples_=std::max(1,static_cast<int>(
            std::round(sampleRate*fadeMilliseconds*.001)));
        cursor_=0;
        currentDelay_=initialDelay;
        previousDelay_=initialDelay;
        fadeRemaining_=0;
        requestedDelay_.store(initialDelay,std::memory_order_release);
        return true;
    }
    // Message/control thread: calls are accepted only within preallocated
    // capacity. Intentionally keeps an already-requested delay on failure.
    bool requestDelaySamples(int delay) noexcept {
        if(channels_<=0 || delay<0 || delay>maxDelay_)return false;
        requestedDelay_.store(delay,std::memory_order_release);
        return true;
    }
    // Called only after audio readers are quiescent; same contract as the
    // rest of the research delay bank's transport reset.
    void reset() noexcept {
        std::fill(history_.begin(),history_.end(),0.f);
        cursor_=0;
        currentDelay_=requestedDelay_.load(std::memory_order_acquire);
        previousDelay_=currentDelay_;
        fadeRemaining_=0;
    }
    int maxDelaySamples() const noexcept{return maxDelay_;}
    int requestedDelaySamples() const noexcept {
        return requestedDelay_.load(std::memory_order_acquire);
    }
    int currentlyRenderingDelaySamples() const noexcept{return currentDelay_;}
    int fadeRemainingSamples() const noexcept{return fadeRemaining_;}

    // No string lookups, loops beyond prepared channels, or dynamic work.
    // If parameters are invalid, NOTHING is written to the caller's audio.
    bool process(float* const* data,int channels,int frames) noexcept {
        if(channels!=channels_||frames<0||!data||stride_==0)return false;
        for(int ch=0;ch<channels;++ch)if(!data[ch])return false;
        for(int n=0;n<frames;++n) {
            if(fadeRemaining_==0) {
                const int target=requestedDelay_.load(std::memory_order_acquire);
                if(target!=currentDelay_) {
                    previousDelay_=currentDelay_;
                    currentDelay_=target;
                    fadeRemaining_=fadeSamples_;
                }
            }
            const bool fading=fadeRemaining_>0;
            const float t=!fading?1.f:
                fadeSamples_<=1?1.f:
                static_cast<float>(fadeSamples_-fadeRemaining_)/
                static_cast<float>(fadeSamples_-1);
            const auto indexNew=(cursor_+stride_-
                static_cast<std::size_t>(currentDelay_))%stride_;
            const auto indexOld=(cursor_+stride_-
                static_cast<std::size_t>(previousDelay_))%stride_;
            for(int ch=0;ch<channels;++ch) {
                const auto row=static_cast<std::size_t>(ch)*stride_;
                float* ptr=data[ch];
                const float raw=ptr[n];
                // Write before read: D=0 returns THIS sample, while D>=1
                // returns previous samples. A ring needs at least maxDelay+1
                // entries to prevent overwriting the maximum-delay tap.
                history_[row+cursor_]=raw;
                const float newValue=history_[row+indexNew];
                const float oldValue=fading?history_[row+indexOld]:newValue;
                ptr[n]=fading?oldValue*(1.f-t)+newValue*t:newValue;
            }
            if(fading)--fadeRemaining_;
            if(++cursor_==stride_)cursor_=0;
        }
        return true;
    }
private:
    std::vector<float> history_;
    std::atomic<int> requestedDelay_{0};
    int channels_=0;
    int maxDelay_=0;
    int currentDelay_=0;
    int previousDelay_=0;
    int fadeRemaining_=0;
    int fadeSamples_=1;
    std::size_t stride_=0;
    std::size_t cursor_=0;
};
} // namespace DAW::PdcResearch
