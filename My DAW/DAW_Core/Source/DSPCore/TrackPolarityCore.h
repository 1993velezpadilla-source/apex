#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace DAW
{
/** Click-reduced, zero-allocation polarity switch for a single track/bus.
    Polarity reversal multiplies samples by -1; it is NOT a time alignment,
    frequency-dependent phase rotation, or stereo correlation correction.
    All channels are switched together to preserve the stereo relationship.
*/
class TrackPolarityCore final
{
public:
    void reset(bool inverted) noexcept
    {
        appliedMultiplier_.store(inverted ? -1.0f : 1.0f, std::memory_order_relaxed);
    }

    void process(juce::AudioBuffer<float>& audio, int requestedSamples, bool inverted) noexcept
    {
        const int samples = juce::jmin(requestedSamples, audio.getNumSamples());
        if (samples <= 0 || audio.getNumChannels() <= 0)
            return;

        const float target = inverted ? -1.0f : 1.0f;
        const float previous = appliedMultiplier_.exchange(target, std::memory_order_relaxed);
        if (previous != target)
            audio.applyGainRamp(0, samples, previous, target); // one-block de-click
        else if (inverted)
            audio.applyGain(0, samples, -1.0f);
    }

private:
    std::atomic<float> appliedMultiplier_ { 1.0f };
};
} // namespace DAW
