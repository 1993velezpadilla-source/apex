#pragma once
#include <JuceHeader.h>

namespace DAW {

class ClickRoutingCore
{
public:
    void prepare(int blockSize) noexcept
    {
        clickBus_.setSize(2, juce::jmax(1, blockSize), false, true, true);
    }

    void clear(int numSamples) noexcept
    {
        if (numSamples <= clickBus_.getNumSamples())
            clickBus_.clear(0, numSamples);
        anyContent_ = false;
    }

    void addClickAtOffset(int sampleOffset, const float* clickSample, int clickSampleLength, float gain) noexcept
    {
        if (clickSample == nullptr || clickSampleLength <= 0) return;
        const int busLen = clickBus_.getNumSamples();
        if (sampleOffset >= busLen) return;

        const int copyLen = juce::jmin(clickSampleLength, busLen - sampleOffset);
        if (copyLen <= 0) return;

        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = clickBus_.getWritePointer(ch, sampleOffset);
            for (int s = 0; s < copyLen; ++s)
                dst[s] += clickSample[s] * gain;
        }
        anyContent_ = true;
    }

    void sumIntoOutput(float* outL, float* outR, int numSamples) const noexcept
    {
        if (!anyContent_ || numSamples <= 0) return;
        if (numSamples > clickBus_.getNumSamples()) return;
        if (outL) juce::FloatVectorOperations::add(outL, clickBus_.getReadPointer(0), numSamples);
        if (outR) juce::FloatVectorOperations::add(outR, clickBus_.getReadPointer(1), numSamples);
    }

    bool hasContent() const noexcept { return anyContent_; }

private:
    juce::AudioBuffer<float> clickBus_;
    bool anyContent_ { false };
};

} // namespace DAW
