#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#if defined(APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC) && APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC
#include "PluginParallelDryDelayResearchCore.h"
#endif

namespace DAW {

class BypassCrossfadeCore
{
public:
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    using DestructionObserverForTesting = void (*)() noexcept;

    static void setDestructionObserverForTesting(DestructionObserverForTesting observer) noexcept
    {
        destructionObserverForTesting_.store(observer, std::memory_order_release);
    }
   #endif

    ~BypassCrossfadeCore() noexcept
    {
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (auto observer = destructionObserverForTesting_.load(std::memory_order_acquire))
            observer();
       #endif
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
#if defined(APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC) && APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC
        dryDelay_.reset();
#endif
        fadeSamples_ = juce::jmax(1, (int) std::round(sampleRate_ * 0.005));
        progress_ = targetBypassed_.load(std::memory_order_relaxed) ? fadeSamples_ : 0;
    }

#if defined(APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC) && APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC
    // Research-only control-plane configuration. Caller must have drained
    // realtime readers before changing the ring format. No allocations in
    // applyResearchDryDelay(), including during automation sub-block slicing.
    bool configureResearchDryDelay(int channels,int latency) {
        return dryDelay_.prepare(channels,latency);
    }
    void applyResearchDryDelay(juce::AudioBuffer<float>& dry,int frames) noexcept {
        dryDelay_.process(dry.getArrayOfWritePointers(),dry.getNumChannels(),frames);
    }
    int researchDryLatency() const noexcept {return dryDelay_.latencySamples();}
#endif

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
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    inline static std::atomic<DestructionObserverForTesting> destructionObserverForTesting_ { nullptr };
   #endif

#if defined(APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC) && APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC
    PluginParallelDryDelayResearchCore dryDelay_{};
#endif
    double sampleRate_ = 44100.0;
    int fadeSamples_ = 220;
    int progress_ = 0;
    std::atomic<bool> targetBypassed_ { false };
};

} // namespace DAW
