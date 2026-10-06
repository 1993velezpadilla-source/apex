#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumOrbAudioReactiveCore — maps audio energy to visual modulation values.
 *
 * Layered on TOP of idle animation, never replaces it.
 * Even "High" intensity remains tasteful and premium.
 *
 * Final visual = idle base + (energy * reactive delta)
 */
class BubblegumOrbAudioReactiveCore
{
public:
    enum class Intensity { Off, Low, Medium, High };

    struct ReactiveValues
    {
        float outerGlowDelta  = 0.f;  // added to base glow
        float innerLightDelta = 0.f;  // added to base inner light
        float scaleDelta      = 0.f;  // added to base scale
        float dripDelta       = 0.f;  // added to base drip intensity
    };

    void setIntensity(Intensity i) noexcept { intensity_ = i; }
    Intensity getIntensity() const noexcept { return intensity_; }
    bool isEnabled() const noexcept { return intensity_ != Intensity::Off; }

    ReactiveValues compute(float energy) const
    {
        if (intensity_ == Intensity::Off)
            return {};

        float mult = getMultiplier();

        ReactiveValues v;
        v.outerGlowDelta  = energy * 0.15f * mult;   // base 0.15, max +0.15 = 0.30 total
        v.innerLightDelta  = energy * 0.12f * mult;   // base 0.10, max +0.12 = 0.22 total
        v.scaleDelta       = energy * 0.015f * mult;  // max +0.015 = 1.015 total
        v.dripDelta        = energy * 0.20f * mult;   // +20% drip motion increase
        return v;
    }

private:
    Intensity intensity_ = Intensity::Medium;

    float getMultiplier() const noexcept
    {
        switch (intensity_)
        {
            case Intensity::Low:    return 0.5f;
            case Intensity::Medium: return 1.0f;
            case Intensity::High:   return 1.4f;
            default:                return 0.0f;
        }
    }
};

} // namespace DAW
