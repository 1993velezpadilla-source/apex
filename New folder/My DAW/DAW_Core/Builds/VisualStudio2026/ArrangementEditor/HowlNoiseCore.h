// ===========================================================================
// HowlNoiseCore.h
// Envelope-followed filtered breath/noise layer for DemonAtmosphereCore.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{

class HowlNoiseCore
{
public:
    void prepare(double sampleRate, int maxBlockSize) noexcept
    {
        sampleRate_ = std::max(1.0, PitchScaleMathCore::sanitizeNaN(sampleRate, 44100.0));
        juce::ignoreUnused(maxBlockSize);
        reset();
    }

    void reset() noexcept
    {
        envelope_ = 0.0;
        smoothedAmount_ = 0.0;
        noiseState_ = 0x12345678u;
        bpLow_ = bpMid_ = bpHigh_ = 0.0f;
    }

    void process(const float* dryVoice, float* outMono, int numSamples, double howlAmount) noexcept
    {
        juce::ScopedNoDenormals noDenormals;
        if (dryVoice == nullptr || outMono == nullptr || numSamples <= 0)
            return;

        howlAmount = std::clamp(PitchScaleMathCore::sanitizeNaN(howlAmount), 0.0, 1.0);
        const double amountCoeff = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.012);
        const double attack = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.008);
        const double release = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.250);

        // Bug 43 fix: lowpassCoeff (2× std::exp) and dbToGain (std::pow) were
        // called every sample from smoothedAmount_ which barely changes per step.
        // Cache and only recompute when smoothedAmount_ shifts > 0.002.
        float cachedLowCoeff  = lowpassCoeff(PitchScaleMathCore::lerp(400.0, 250.0, smoothedAmount_) * 0.55);
        float cachedHighCoeff = lowpassCoeff(PitchScaleMathCore::lerp(400.0, 250.0, smoothedAmount_) * 1.65);
        float cachedWet       = dbToGain(PitchScaleMathCore::lerp(-84.0, -28.0, smoothedAmount_));
        double lastCachedAmount = smoothedAmount_;

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedAmount_ = PitchScaleMathCore::onePoleNext(smoothedAmount_, howlAmount, amountCoeff);
            if (smoothedAmount_ < 0.001)
                continue;

            const double targetEnv = std::abs(dryVoice[i]);
            envelope_ = PitchScaleMathCore::onePoleNext(envelope_, targetEnv, targetEnv > envelope_ ? attack : release);

            if (std::abs(smoothedAmount_ - lastCachedAmount) > 0.002)
            {
                const double centerHz = PitchScaleMathCore::lerp(400.0, 250.0, smoothedAmount_);
                cachedLowCoeff  = lowpassCoeff(centerHz * 0.55);
                cachedHighCoeff = lowpassCoeff(centerHz * 1.65);
                cachedWet       = dbToGain(PitchScaleMathCore::lerp(-84.0, -28.0, smoothedAmount_));
                lastCachedAmount = smoothedAmount_;
            }

            const float lowCoeff  = cachedLowCoeff;
            const float highCoeff = cachedHighCoeff;
            const float wet       = cachedWet;

            const float white = nextNoise();
            bpLow_ += lowCoeff * (white - bpLow_);
            bpHigh_ += highCoeff * (white - bpHigh_);
            const float band = bpHigh_ - bpLow_;
            bpMid_ += 0.025f * (band - bpMid_);
            bpLow_ = PitchScaleMathCore::flushDenormal(bpLow_);
            bpHigh_ = PitchScaleMathCore::flushDenormal(bpHigh_);
            bpMid_ = PitchScaleMathCore::flushDenormal(bpMid_);

            outMono[i] += PitchScaleMathCore::flushDenormal(bpMid_ * static_cast<float>(envelope_) * wet);
        }
    }

private:
    float nextNoise() noexcept
    {
        noiseState_ = noiseState_ * 1664525u + 1013904223u;
        const int32_t signedBits = static_cast<int32_t>(noiseState_ >> 9);
        return static_cast<float>(signedBits) * (1.0f / 4194304.0f);
    }

    float lowpassCoeff(double hz) const noexcept
    {
        const double c = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * hz / sampleRate_);
        return static_cast<float>(std::clamp(c, 0.0001, 1.0));
    }

    static float dbToGain(double db) noexcept
    {
        return static_cast<float>(std::pow(10.0, db / 20.0));
    }

    double sampleRate_ = 44100.0;
    double envelope_ = 0.0;
    double smoothedAmount_ = 0.0;
    uint32_t noiseState_ = 0x12345678u;
    float bpLow_ = 0.0f;
    float bpMid_ = 0.0f;
    float bpHigh_ = 0.0f;
};

} // namespace ArrangementEditor
