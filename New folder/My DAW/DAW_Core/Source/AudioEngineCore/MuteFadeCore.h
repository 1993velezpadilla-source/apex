#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace DAW {

class MuteFadeCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        maxBlockSize_ = juce::jmax(1, maxBlockSize);
        smoothingCoeff_ = computeSmoothingCoefficient();
        ramp_.resize((size_t) maxBlockSize_, currentGain_);
    }

    void reset(bool muted) noexcept
    {
        currentGain_ = muted ? 0.0f : 1.0f;
        targetGain_ = currentGain_;
        std::fill(ramp_.begin(), ramp_.end(), currentGain_);
    }

    const float* generate(bool muted, int numSamples)
    {
        jassert(numSamples <= maxBlockSize_);
        targetGain_ = muted ? 0.0f : 1.0f;
        if ((int) ramp_.size() < numSamples)
            ramp_.resize((size_t) numSamples, currentGain_);

        for (int s = 0; s < numSamples; ++s)
        {
            currentGain_ += (targetGain_ - currentGain_) * (float) smoothingCoeff_;
            if (std::abs(currentGain_ - targetGain_) < 1.0e-6f)
                currentGain_ = targetGain_;
            ramp_[(size_t) s] = std::sqrt(juce::jlimit(0.0f, 1.0f, currentGain_));
        }

        return ramp_.data();
    }

    bool isFullyMuted() const noexcept
    {
        return targetGain_ == 0.0f && currentGain_ <= 1.0e-5f;
    }

private:
    double computeSmoothingCoefficient() const noexcept
    {
        constexpr double tau = 0.005;
        return 1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * tau));
    }

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    double smoothingCoeff_ = 1.0;
    float currentGain_ = 1.0f;
    float targetGain_ = 1.0f;
    std::vector<float> ramp_;
};

} // namespace DAW
