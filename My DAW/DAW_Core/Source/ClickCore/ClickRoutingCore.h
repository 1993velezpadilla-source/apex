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

        // C9: carry the truncated remainder into the next block instead of
        // hard-cutting the burst at the block boundary. Latest tail wins
        // (concurrent tails are the same click sample at near-identical phase).
        if (copyLen < clickSampleLength)
        {
            pendingTail_.clickSample = clickSample;
            pendingTail_.gain = gain;
            pendingTail_.offset = copyLen;
            pendingTail_.remaining = clickSampleLength - copyLen;
        }
        anyContent_ = true;
    }

    /** C9: mix a burst tail carried over from the previous block. Call once
     *  per block after clear() and before adding new beats. */
    void carryTailIntoBlock() noexcept
    {
        if (pendingTail_.clickSample == nullptr || pendingTail_.remaining <= 0) return;
        const int busLen = clickBus_.getNumSamples();
        const int copyLen = juce::jmin(pendingTail_.remaining, busLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = clickBus_.getWritePointer(ch);
            for (int s = 0; s < copyLen; ++s)
                dst[s] += pendingTail_.clickSample[pendingTail_.offset + s] * pendingTail_.gain;
        }
        pendingTail_.offset += copyLen;
        pendingTail_.remaining -= copyLen;
        if (pendingTail_.remaining <= 0)
            pendingTail_.clickSample = nullptr;
        anyContent_ = true;
    }

    void sumIntoOutput(float* outL, float* outR, int numSamples, const float* gainRamp = nullptr) const noexcept
    {
        if (!anyContent_ || numSamples <= 0) return;
        if (numSamples > clickBus_.getNumSamples()) return;
        if (gainRamp != nullptr)
        {
            // C9: fade the click with the engine's transport fade ramp so a
            // transport stop/start cannot hard-cut a click burst.
            const auto* busL = clickBus_.getReadPointer(0);
            const auto* busR = clickBus_.getReadPointer(1);
            for (int s = 0; s < numSamples; ++s)
            {
                if (outL) outL[s] += busL[s] * gainRamp[s];
                if (outR) outR[s] += busR[s] * gainRamp[s];
            }
            return;
        }
        if (outL) juce::FloatVectorOperations::add(outL, clickBus_.getReadPointer(0), numSamples);
        if (outR) juce::FloatVectorOperations::add(outR, clickBus_.getReadPointer(1), numSamples);
    }

    bool hasContent() const noexcept { return anyContent_; }

private:
    struct PendingTail
    {
        const float* clickSample = nullptr;
        float gain = 1.0f;
        int offset = 0;
        int remaining = 0;
    };

    juce::AudioBuffer<float> clickBus_;
    bool anyContent_ { false };
    PendingTail pendingTail_;
};

} // namespace DAW
