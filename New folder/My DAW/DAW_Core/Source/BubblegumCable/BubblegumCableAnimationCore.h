#pragma once
#include "BubblegumCableTypes.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableAnimationCore — single time source for all cable layers.
//
// All animated values (blob phases, sweep positions, pulse) are derived
// from this core's time. This means:
//   - No per-layer time drift.
//   - Mixer scroll / resize never invalidates anything (pure t-space).
//   - Single tick() per frame, called from BubblegumCableSystem.
// =====================================================================
class BubblegumCableAnimationCore
{
public:
    void tick(float deltaMs) noexcept
    {
        const float dt = deltaMs * 0.001f;
        time_ += dt;
    }

    void reset() noexcept { time_ = 0.f; }

    float time() const noexcept { return time_; }

    // ── Blob lane t-positions ────────────────────────────────────────────
    // blobCount determines how many lanes exist for this cable.
    // Returns normalized [0,1] position along the cable centerline.
    // FAST multiplier: laneDrift is the speed scalar per blob.
    static float blobLaneT(float time,
                            float seedBase,
                            int   index,
                            int   blobCount,
                            float speed = 1.0f) noexcept
    {
        const float baseT = juce::jmap(
            (float)(index + 1) / (float)(blobCount + 1), 0.14f, 0.86f);
        const float driftAmp = 0.045f;
        const float freq = 0.28f + 0.04f * (float)index;
        const float phase = stable01(seedBase + (float)index * 9.1f)
                          * juce::MathConstants<float>::twoPi;
        const float drift = std::sin(time * freq * speed + phase) * driftAmp;
        return juce::jlimit(0.06f, 0.94f, baseT + drift);
    }

    // ── Sweep highlight lane ─────────────────────────────────────────────
    static float sweepLaneT(float time, int index, float speed = 1.0f) noexcept
    {
        return frac(0.16f + time * (0.042f + 0.005f * (float)index) * speed
                  + (float)index * 0.27f);
    }

    // ── Pulse value [0,1] ────────────────────────────────────────────────
    static float pulseSin(float time, float phase) noexcept
    {
        return 0.5f + 0.5f * std::sin(time * 2.1f + phase);
    }

    // ── Shared seed from cable endpoints ────────────────────────────────
    static float cableSeed(const BubblegumCableInput& in) noexcept
    {
        return in.source.x * 0.031f
             + in.target.x * 0.017f
             + in.target.y * 0.013f;
    }

    static float stable01(float seed) noexcept
    {
        return frac(std::sin(seed * 12.9898f + 78.233f) * 43758.5453f);
    }

    static float frac(float v) noexcept { return v - std::floor(v); }

private:
    float time_ = 0.f;
};

} // namespace DAW
