#pragma once
#include <JuceHeader.h>

namespace DAW {

enum class SlotMixCurve
{
    Linear,
    EqualPower
};

class PluginSlotMixCore
{
public:
    static void apply(const juce::AudioBuffer<float>& dry,
                      juce::AudioBuffer<float>& wet,
                      float mixNormalized,
                      SlotMixCurve curve = SlotMixCurve::EqualPower) noexcept
    {
        const float mix = juce::jlimit(0.0f, 1.0f, mixNormalized);
        float dryGain = 1.0f - mix;
        float wetGain = mix;

        if (curve == SlotMixCurve::EqualPower)
        {
            dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
            wetGain = std::sin(mix * juce::MathConstants<float>::halfPi);
        }

        const int channels = juce::jmin(dry.getNumChannels(), wet.getNumChannels());
        const int samples = juce::jmin(dry.getNumSamples(), wet.getNumSamples());

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* d = dry.getReadPointer(ch);
            auto* w = wet.getWritePointer(ch);
            for (int s = 0; s < samples; ++s)
                w[s] = d[s] * dryGain + w[s] * wetGain;
        }
    }
};

} // namespace DAW
