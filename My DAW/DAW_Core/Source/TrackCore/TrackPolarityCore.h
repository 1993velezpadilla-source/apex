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
    // Audio-thread-owned transition state. Each Track keeps one instance,
    // shared by that track's graph and graph-free processing paths.
    struct TransitionState
    {
        float currentGain = 1.0f;
        float targetGain = 1.0f;
        float gainStep = 0.0f;
        int remainingSamples = 0;
    };

    /**
     * Smooth a user polarity change across a short, continuous gain ramp.
     * A raw +1 -> -1 sign switch can click at arbitrary waveform positions;
     * this zero-latency, bounded ramp passes through zero instead. Outside
     * the transition, gain is EXACTLY +/-1 so phase-null tests remain exact.
     *
     * The state must be mutated only by the owner track's audio processing
     * context (never by its GUI setter). No allocation or synchronization.
     */
    template <typename AudioBufferType>
    static void applyWithTransition(AudioBufferType& audio,
                                    int requestedSamples,
                                    bool invert,
                                    TransitionState& state,
                                    int fadeSamples = 64) noexcept
    {
        if (requestedSamples <= 0)
            return;

        const int samples = std::min(requestedSamples, audio.getNumSamples());
        if (samples <= 0)
            return;

        const float target = invert ? -1.0f : 1.0f;
        if (target != state.targetGain)
        {
            state.targetGain = target;
            state.remainingSamples = std::max(1, fadeSamples);
            state.gainStep = (target - state.currentGain)
                           / static_cast<float>(state.remainingSamples);
        }

        const int ramped = std::min(samples, state.remainingSamples);
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            auto* samplesOut = audio.getWritePointer(channel);
            float gain = state.currentGain;
            for (int sample = 0; sample < ramped; ++sample)
            {
                gain += state.gainStep;
                samplesOut[sample] *= gain;
            }

            if (ramped < samples && state.targetGain < 0.0f)
                audio.applyGain(channel, ramped, samples - ramped, -1.0f);
        }

        state.currentGain += state.gainStep * static_cast<float>(ramped);
        state.remainingSamples -= ramped;
        if (state.remainingSamples == 0)
        {
            state.currentGain = state.targetGain;
            state.gainStep = 0.0f;
        }
    }

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
