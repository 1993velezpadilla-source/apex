#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumOrbReactiveDripCore — audio-reactive drip intensity modulation.
 *
 * Slightly increases liquid edge motion based on audio energy.
 * Feels like extra tension/pressure when sound is present.
 * Elegant and controlled — never animated goo.
 */
class BubblegumOrbReactiveDripCore
{
public:
    static constexpr float kBaseDripIntensity = 0.3f;
    static constexpr float kMaxDripBoost      = 0.2f; // +20% max

    float computeDripIntensity(float audioEnergy, float reactiveDripDelta) const
    {
        if (!enabled_) return kBaseDripIntensity;
        return juce::jlimit(0.0f, 1.0f,
            kBaseDripIntensity + audioEnergy * kMaxDripBoost + reactiveDripDelta);
    }

    void setEnabled(bool e) noexcept { enabled_ = e; }
    bool isEnabled() const noexcept  { return enabled_; }

private:
    bool enabled_ = true;
};

} // namespace DAW
