// ===========================================================================
// EchoTapCore.h
// Dark stereo echo return for DemonAtmosphereCore.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{

class EchoTapCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = std::max(1.0, PitchScaleMathCore::sanitizeNaN(sampleRate, 44100.0));
        juce::ignoreUnused(maxBlockSize);
        const int maxDelay = static_cast<int>(sampleRate_ * 1.0);
        delayL_.setSize(1, maxDelay, false, true, true);
        delayR_.setSize(1, maxDelay, false, true, true);
        reset();
    }

    void reset() noexcept
    {
        delayL_.clear();
        delayR_.clear();
        writePos_ = 0;
        fbStateL_ = fbStateR_ = 0.0f;
        smoothedAmount_ = 0.0;
    }

    void process(const float* input, float* outL, float* outR, int numSamples, double echoAmount) noexcept
    {
        juce::ScopedNoDenormals noDenormals;
        if (input == nullptr || outL == nullptr || outR == nullptr || numSamples <= 0)
            return;

        echoAmount = std::clamp(PitchScaleMathCore::sanitizeNaN(echoAmount), 0.0, 1.0);
        const int n = delayL_.getNumSamples();
        if (n <= 1)
            return;

        const int delayL = std::clamp(static_cast<int>(sampleRate_ * 0.320), 1, n - 1);
        const int delayR = std::clamp(static_cast<int>(sampleRate_ * 0.460), 1, n - 1);
        const float lpCoeff = lowpassCoeff(4500.0);
        const double coeff = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.012);

        float* dl = delayL_.getWritePointer(0);
        float* dr = delayR_.getWritePointer(0);

        // Bug 44 fix: feedback and wet both derive from smoothedAmount_ which
        // advances by a tiny one-pole step per sample. dbToGain (std::pow) was
        // being called every sample. Cache and only recompute on > 0.002 shift.
        float cachedFeedback = static_cast<float>(PitchScaleMathCore::lerp(0.0, 0.30, smoothedAmount_));
        float cachedWet      = dbToGain(PitchScaleMathCore::lerp(-72.0, -16.0, smoothedAmount_));
        double lastCachedAmount = smoothedAmount_;

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedAmount_ = PitchScaleMathCore::onePoleNext(smoothedAmount_, echoAmount, coeff);

            if (std::abs(smoothedAmount_ - lastCachedAmount) > 0.002)
            {
                cachedFeedback   = static_cast<float>(PitchScaleMathCore::lerp(0.0, 0.30, smoothedAmount_));
                cachedWet        = dbToGain(PitchScaleMathCore::lerp(-72.0, -16.0, smoothedAmount_));
                lastCachedAmount = smoothedAmount_;
            }

            const float feedback = cachedFeedback;
            const float wet      = cachedWet;

            int readL = writePos_ - delayL;
            int readR = writePos_ - delayR;
            while (readL < 0) readL += n;
            while (readR < 0) readR += n;

            const float tapL = dl[readL % n];
            const float tapR = dr[readR % n];
            fbStateL_ += lpCoeff * (tapR - fbStateL_);
            fbStateR_ += lpCoeff * (tapL - fbStateR_);
            fbStateL_ = PitchScaleMathCore::flushDenormal(fbStateL_);
            fbStateR_ = PitchScaleMathCore::flushDenormal(fbStateR_);

            dl[writePos_] = PitchScaleMathCore::flushDenormal(input[i] + fbStateL_ * feedback);
            dr[writePos_] = PitchScaleMathCore::flushDenormal(input[i] + fbStateR_ * feedback);

            outL[i] += tapL * wet;
            outR[i] += tapR * wet;

            writePos_ = (writePos_ + 1) % n;
        }
    }

    int getCurrentLatencySamples() const noexcept { return 0; }

private:
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
    juce::AudioBuffer<float> delayL_;
    juce::AudioBuffer<float> delayR_;
    int writePos_ = 0;
    float fbStateL_ = 0.0f;
    float fbStateR_ = 0.0f;
    double smoothedAmount_ = 0.0;
};

} // namespace ArrangementEditor
