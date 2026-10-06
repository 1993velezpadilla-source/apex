#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace DAW {

class MasterGainRampCore
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        blockSize_ = juce::jmax(1, blockSize);
        coeff_ = 1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * 0.010));
        ramp_.resize((size_t)blockSize_, currentGain_);
        reset();
    }

    void setTargetGain(float linearGain) noexcept
    {
        targetGain_.store(juce::jmax(0.0f, linearGain), std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        currentGain_ = targetGain_.load(std::memory_order_relaxed);
        std::fill(ramp_.begin(), ramp_.end(), currentGain_);
    }

    float getCurrentGain() const noexcept { return currentGain_; }
    float getTargetGain() const noexcept { return targetGain_.load(std::memory_order_relaxed); }

    void applyToStereoBuffer(float* L, float* R, int numSamples)
    {
        if (L == nullptr || R == nullptr || numSamples <= 0) return;
        if ((int)ramp_.size() < numSamples)
            ramp_.resize((size_t)numSamples, currentGain_);

        const float target = targetGain_.load(std::memory_order_relaxed);
        if (std::abs(target - currentGain_) < 1.0e-6f)
        {
            currentGain_ = target;
            if (std::abs(target - 1.0f) > 1.0e-6f)
            {
                juce::FloatVectorOperations::multiply(L, target, numSamples);
                juce::FloatVectorOperations::multiply(R, target, numSamples);
            }
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            currentGain_ += (target - currentGain_) * (float)coeff_;
            if (std::abs(currentGain_ - target) < 1.0e-7f)
                currentGain_ = target;
            L[i] *= currentGain_;
            R[i] *= currentGain_;
        }
    }

private:
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    double coeff_ = 1.0;
    std::atomic<float> targetGain_ { 1.0f };
    float currentGain_ = 1.0f;
    std::vector<float> ramp_;
};

} // namespace DAW
