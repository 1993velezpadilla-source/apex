#pragma once
#include <JuceHeader.h>

namespace DAW
{
/**
 * Realtime-safe polarity reversal, intentionally distinct from frequency-
 * dependent phase rotation and from time alignment. All channels are inverted
 * together. No allocation, locks or graph updates occur in process().
 */
struct TrackPolarityCore final
{
    static void apply(juce::AudioBuffer<float>& audio,
                      int requestedSamples, bool invert) noexcept
    {
        if (!invert || requestedSamples <= 0)
            return;

        const int samples = juce::jmin(requestedSamples, audio.getNumSamples());
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            audio.applyGain(channel, 0, samples, -1.0f);
    }
};
} // namespace DAW
