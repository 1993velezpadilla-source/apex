#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace DAW {

class ConnectionGainRampCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        maxBlockSize_ = juce::jmax(1, maxBlockSize);
        smoothingCoeff_ = computeSmoothingCoefficient();
        ramp_.resize((size_t) maxBlockSize_, currentGain_);
    }

    void reset(float gain) noexcept
    {
        currentGain_ = gain;
        std::fill(ramp_.begin(), ramp_.end(), gain);
    }

    const float* generate(float targetGain, int numSamples)
    {
        jassert(numSamples <= maxBlockSize_);
        if ((int) ramp_.size() < numSamples)
            ramp_.resize((size_t) numSamples, currentGain_);

        for (int s = 0; s < numSamples; ++s)
        {
            currentGain_ += (targetGain - currentGain_) * (float) smoothingCoeff_;
            if (std::abs(currentGain_ - targetGain) < 1.0e-7f)
                currentGain_ = targetGain;
            ramp_[(size_t) s] = currentGain_;
        }

        return ramp_.data();
    }

    float getCurrentGain() const noexcept { return currentGain_; }
    int getCapacity() const noexcept { return (int) ramp_.size(); }

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
    std::vector<float> ramp_;
};

} // namespace DAW
