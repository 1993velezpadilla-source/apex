#pragma once
#include <JuceHeader.h>
#include "MonitorStateModel.h"

namespace DAW {

/**
 * DimMuteMonoProcessor — audio-thread-safe monitor path DSP.
 *
 * Applies (in order):
 *   1. Mono fold (L = R = (L+R)/2)
 *   2. Combined gain: monitor level × dim attenuation × mute
 *
 * All state is read from MonitorStateModel atomics — no locks.
 * Gain changes use per-sample smoothing for click-free transitions.
 */
class DimMuteMonoProcessor
{
public:
    void prepare(double sampleRate, int /*blockSize*/) noexcept
    {
        smoothedGain_.reset(sampleRate, 0.015); // 15 ms ramp
        smoothedGain_.setCurrentAndTargetValue(1.0f);
    }

    void process(float* outL, float* outR, int numSamples,
                 const MonitorStateModel& state) noexcept
    {
        const bool  mono   = state.monoActive.load(std::memory_order_relaxed);
        const bool  muted  = state.muteActive.load(std::memory_order_relaxed);
        const bool  dimmed = state.dimActive.load(std::memory_order_relaxed);
        const float gain   = state.monitorGain.load(std::memory_order_relaxed);
        const float dimDb  = state.dimAmountDb.load(std::memory_order_relaxed);

        float targetGain = 0.0f;
        if (!muted)
        {
            targetGain = gain;
            if (dimmed)
                targetGain *= std::pow(10.0f, dimDb / 20.0f);
        }

        smoothedGain_.setTargetValue(targetGain);

        for (int i = 0; i < numSamples; ++i)
        {
            float g = smoothedGain_.getNextValue();
            float l = outL[i];
            float r = outR[i];

            if (mono)
            {
                float m = (l + r) * 0.5f;
                l = m;
                r = m;
            }

            outL[i] = l * g;
            outR[i] = r * g;
        }
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain_;
};

} // namespace DAW
