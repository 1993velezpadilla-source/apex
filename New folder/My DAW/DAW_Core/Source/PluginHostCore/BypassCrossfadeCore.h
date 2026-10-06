#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace DAW {

class BypassCrossfadeCore
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        fadeSamples_ = juce::jmax(1, (int) std::round(sampleRate_ * 0.005));
        progress_ = targetBypassed_.load(std::memory_order_relaxed) ? fadeSamples_ : 0;
    }

    void reset() noexcept
    {
        progress_ = targetBypassed_.load(std::memory_order_relaxed) ? fadeSamples_ : 0;
    }

    void setTargetBypassed(bool bypassed) noexcept
    {
        targetBypassed_.store(bypassed, std::memory_order_relaxed);
    }

    bool isTargetBypassed() const noexcept
    {
        return targetBypassed_.load(std::memory_order_relaxed);
    }

    bool isFullyBypassed() const noexcept
    {
        return targetBypassed_.load(std::memory_order_relaxed) && progress_ >= fadeSamples_;
    }

    void processSlotBypass(const juce::AudioBuffer<float>& dryIn,
                           juce::AudioBuffer<float>& wetOut,
                           int numSamples) noexcept
    {
        const bool targetBypassed = targetBypassed_.load(std::memory_order_relaxed);
        const int channels = juce::jmin(dryIn.getNumChannels(), wetOut.getNumChannels());

        for (int s = 0; s < numSamples; ++s)
        {
            if (targetBypassed)
                progress_ = juce::jmin(fadeSamples_, progress_ + 1);
            else
                progress_ = juce::jmax(0, progress_ - 1);

            const float t = (float) progress_ / (float) juce::jmax(1, fadeSamples_);
            const float dryGain = std::sin(t * juce::MathConstants<float>::halfPi);
            const float wetGain = std::cos(t * juce::MathConstants<float>::halfPi);

            if (progress_ <= 0)
                continue;

            for (int ch = 0; ch < channels; ++ch)
            {
                const float dry = dryIn.getSample(ch, s);
                const float wet = wetOut.getSample(ch, s);
                wetOut.setSample(ch, s, dry * dryGain + wet * wetGain);
            }
        }
    }

private:
    double sampleRate_ = 44100.0;
    int fadeSamples_ = 220;
    int progress_ = 0;
    std::atomic<bool> targetBypassed_ { false };
};

} // namespace DAW
