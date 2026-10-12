#pragma once
#include <algorithm>

namespace DAW
{
/**
 * Realtime-safe per-track polarity reversal, not time alignment or an
 * all-pass phase rotation. Applies the same sign to every active channel.
 * No allocations, locks, plugin calls, or graph mutations on the audio thread.
 *
 * A buffer needs JUCE AudioBuffer's getNumSamples(), getNumChannels(), and
 * applyGain(channel, firstSample, numberOfSamples, gain) interface. Keeping
 * this DSP primitive header-only and interface-templated lets the exact
 * production logic be tested without loading a DAW or audio device.
 */
struct TrackPolarityCore final
{
    template <typename AudioBufferType>
    static void apply(AudioBufferType& audio,
                      int requestedSamples, bool invert) noexcept
    {
        if (!invert || requestedSamples <= 0)
            return;

        const int samples = std::min(requestedSamples, audio.getNumSamples());
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            audio.applyGain(channel, 0, samples, -1.0f);
    }
};
} // namespace DAW
