#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumOrbAudioEnvelopeCore — smoothed audio envelope for the Bubblegum Orb.
 *
 * Reads raw peak level from the source track and produces a smooth
 * normalized energy value [0..1] suitable for driving subtle visual modulation.
 *
 * Attack is slightly faster than release to feel responsive yet elegant.
 * No jitter. No raw peaks. Premium smoothing.
 */
class BubblegumOrbAudioEnvelopeCore
{
public:
    static constexpr float kAttackMs  = 30.f;
    static constexpr float kReleaseMs = 180.f;

    void feedLevel(float rawPeak)
    {
        rawPeak = juce::jlimit(0.0f, 1.0f, rawPeak);

        if (rawPeak > envelope_)
            envelope_ += (rawPeak - envelope_) * attackCoeff_;
        else
            envelope_ += (rawPeak - envelope_) * releaseCoeff_;
    }

    void tick(float deltaMs)
    {
        // Recompute coefficients if tick rate changes
        if (deltaMs > 0.01f)
        {
            attackCoeff_  = 1.0f - std::exp(-deltaMs / kAttackMs);
            releaseCoeff_ = 1.0f - std::exp(-deltaMs / kReleaseMs);
        }
    }

    float getEnergy() const noexcept { return envelope_; }

    void reset() noexcept { envelope_ = 0.0f; }

private:
    float envelope_     = 0.0f;
    float attackCoeff_  = 0.05f;
    float releaseCoeff_ = 0.01f;
};

} // namespace DAW
