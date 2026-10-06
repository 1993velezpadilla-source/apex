#pragma once
#include <JuceHeader.h>

namespace DAW {

class MasterOversamplingCore
{
public:
    void prepare(double sampleRate, int blockSize, int factor)
    {
        sampleRate_ = sampleRate;
        blockSize_ = juce::jmax(1, blockSize);
        factor_ = juce::jlimit(1, 4, factor);
        buffer_.setSize(2, blockSize_ * factor_, false, false, true);
    }

    int getFactor() const noexcept { return factor_; }

    juce::AudioBuffer<float>& getBuffer() noexcept { return buffer_; }

    void upsample(const float* L, const float* R, int numSamples)
    {
        const int outSamples = numSamples * factor_;
        if (buffer_.getNumSamples() < outSamples)
            buffer_.setSize(2, outSamples, false, false, true);

        auto* oL = buffer_.getWritePointer(0);
        auto* oR = buffer_.getWritePointer(1);
        if (factor_ <= 1)
        {
            std::memcpy(oL, L, sizeof(float) * (size_t)numSamples);
            std::memcpy(oR, R, sizeof(float) * (size_t)numSamples);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float nextL = (i + 1 < numSamples) ? L[i + 1] : L[i];
            const float nextR = (i + 1 < numSamples) ? R[i + 1] : R[i];
            for (int k = 0; k < factor_; ++k)
            {
                const float t = (float)k / (float)factor_;
                oL[i * factor_ + k] = L[i] + (nextL - L[i]) * t;
                oR[i * factor_ + k] = R[i] + (nextR - R[i]) * t;
            }
        }
    }

    void downsample(float* L, float* R, int numSamples)
    {
        const auto* oL = buffer_.getReadPointer(0);
        const auto* oR = buffer_.getReadPointer(1);
        if (factor_ <= 1)
        {
            std::memcpy(L, oL, sizeof(float) * (size_t)numSamples);
            std::memcpy(R, oR, sizeof(float) * (size_t)numSamples);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float sumL = 0.0f, sumR = 0.0f;
            for (int k = 0; k < factor_; ++k)
            {
                sumL += oL[i * factor_ + k];
                sumR += oR[i * factor_ + k];
            }
            L[i] = sumL / (float)factor_;
            R[i] = sumR / (float)factor_;
        }
    }

private:
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    int factor_ = 1;
    juce::AudioBuffer<float> buffer_;
};

} // namespace DAW
