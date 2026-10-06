#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumTrackPulseCore — soft breathing pulse for send targets.
 *
 * Slow pulse (1.8 s cycle), full 0→1 normalized output.
 * Rendering code applies its own alpha curves from this base.
 */
class BubblegumTrackPulseCore
{
public:
    static constexpr float kPulsePeriodMs = 1800.0f;
    static constexpr float kPulseMinAlpha = 0.0f;
    static constexpr float kPulseMaxAlpha = 1.0f;

    void tick(float deltaMs) noexcept
    {
        if (!enabled_) return;
        phase_ += (deltaMs / kPulsePeriodMs) * juce::MathConstants<float>::twoPi;
        if (phase_ > juce::MathConstants<float>::twoPi)
            phase_ -= juce::MathConstants<float>::twoPi;
    }

    float getPulseAlpha() const noexcept
    {
        if (!enabled_) return 0.0f;
        float t = (std::sin(phase_) + 1.0f) * 0.5f;
        return kPulseMinAlpha + t * (kPulseMaxAlpha - kPulseMinAlpha);
    }

    void setEnabled(bool e) noexcept { enabled_ = e; if (!e) phase_ = 0.0f; }
    bool isEnabled() const noexcept  { return enabled_; }
    void reset() noexcept            { phase_ = 0.0f; }

private:
    float phase_   = 0.0f;
    bool  enabled_ = false;
};

} // namespace DAW
