#pragma once
#include "BubblegumCableTypesV2.h"
#include <cmath>

namespace DAW::BgV2 {

class BubblegumCableAnimationCoreV2 {
public:
    void tick(float deltaSec) noexcept { time_ += deltaSec; }
    void setTime(float t) noexcept { time_ = t; }
    void reset() noexcept { time_ = 0.f; }
    float time() const noexcept { return time_; }

    static float rand01(float seed) noexcept {
        return frac(std::sin(seed * 12.9898f + 78.233f) * 43758.5453f);
    }
    static float frac(float v) noexcept { return v - std::floor(v); }

    static float smoothstep(float t) noexcept {
        const float c = juce::jlimit(0.f, 1.f, t);
        return c * c * (3.f - 2.f * c);
    }

    static float loopPhase(float time, float freq, float seedOffset = 0.f) noexcept {
        return frac(time * freq + seedOffset);
    }

private:
    float time_ = 0.f;
};

} // namespace DAW::BgV2
