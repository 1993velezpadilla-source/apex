#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace DAW {

/** Per-track software input trim gain with 5ms smoothing. */
class InputTrimCore
{
public:
    InputTrimCore() = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        constexpr double tau = 0.005;
        smoothingCoeff_ = (float)(1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * tau)));
        smoothedGain_ = targetGain_.load(std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        smoothedGain_ = targetGain_.load(std::memory_order_relaxed);
    }

    void setTargetGainDb(float db) noexcept
    {
        if (!std::isfinite(db)) db = 0.0f;
        db = juce::jlimit(-120.0f, 24.0f, db);
        targetGainDb_.store(db, std::memory_order_relaxed);
        const float linear = (db <= -120.0f) ? 0.0f : std::pow(10.0f, db * 0.05f);
        targetGain_.store(linear, std::memory_order_relaxed);
    }

    float getTargetGainDb() const noexcept
    {
        return targetGainDb_.load(std::memory_order_relaxed);
    }

    float getTargetGainLinear() const noexcept
    {
        return targetGain_.load(std::memory_order_relaxed);
    }

    /** Apply the requested gain without advancing audio-thread smoothing.
        Used only for the independent input-meter preview copy. */
    void applyTargetGainToStereoBuffer(float* L, float* R, int numSamples) const noexcept
    {
        if (numSamples <= 0)
            return;
        const float gain = getTargetGainLinear();
        if (L) juce::FloatVectorOperations::multiply(L, gain, numSamples);
        if (R) juce::FloatVectorOperations::multiply(R, gain, numSamples);
    }

    void applyToStereoBuffer(float* L, float* R, int numSamples) noexcept
    {
        const juce::ScopedNoDenormals noDenormals;

        const float t = targetGain_.load(std::memory_order_relaxed);

        if (std::abs(t - 1.0f) < kUnityEpsilon
            && std::abs(smoothedGain_ - 1.0f) < kUnityEpsilon)
        {
            smoothedGain_ = 1.0f;
            return;
        }

        if (t == 0.0f && smoothedGain_ < kSilenceFloor)
        {
            smoothedGain_ = 0.0f;
            if (L) juce::FloatVectorOperations::clear(L, numSamples);
            if (R) juce::FloatVectorOperations::clear(R, numSamples);
            return;
        }

        if (std::abs(t - smoothedGain_) < kSilenceFloor)
        {
            smoothedGain_ = t;
            if (L) juce::FloatVectorOperations::multiply(L, t, numSamples);
            if (R) juce::FloatVectorOperations::multiply(R, t, numSamples);
            return;
        }

        for (int s = 0; s < numSamples; ++s)
        {
            smoothedGain_ += (t - smoothedGain_) * smoothingCoeff_;
            if (smoothedGain_ < kSilenceFloor && t == 0.0f)
                smoothedGain_ = 0.0f;
            if (L) L[s] *= smoothedGain_;
            if (R) R[s] *= smoothedGain_;
        }
    }

private:
    static constexpr float kUnityEpsilon = 1.0e-5f;
    static constexpr float kSilenceFloor = 1.0e-7f;

    std::atomic<float> targetGain_     { 1.0f };
    std::atomic<float> targetGainDb_   { 0.0f };
    float              smoothedGain_   { 1.0f };
    float              smoothingCoeff_ { 1.0f };
    double             sampleRate_     { 44100.0 };
};

} // namespace DAW
