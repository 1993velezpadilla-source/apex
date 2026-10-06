// ===========================================================================
// CryptReverbCore.h
// Dark stereo algorithmic reverb return for DemonAtmosphereCore.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include <JuceHeader.h>
#include <array>

namespace ArrangementEditor
{

class CryptReverbCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = std::max(1.0, PitchScaleMathCore::sanitizeNaN(sampleRate, 44100.0));
        juce::ignoreUnused(maxBlockSize);
        const int maxDelay = static_cast<int>(sampleRate_ * 0.25);
        for (auto& line : lines_)
            line.setSize(maxDelay);
        reset();
    }

    void reset() noexcept
    {
        for (auto& line : lines_) line.reset();
        for (auto& s : dampState_) s = 0.0f;
        smoothedAmount_ = 0.0;
    }

    void process(const float* input, float* outL, float* outR, int numSamples, double cryptAmount) noexcept
    {
        juce::ScopedNoDenormals noDenormals;
        if (input == nullptr || outL == nullptr || outR == nullptr || numSamples <= 0)
            return;

        cryptAmount = std::clamp(PitchScaleMathCore::sanitizeNaN(cryptAmount), 0.0, 1.0);
        const double amountCoeff = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.015);

        // Bug 42 fix: dampCoeff, feedback, and wet all derive from smoothedAmount_
        // which changes by only ~onePoleCoefficient * (target - current) per sample.
        // Recomputing std::exp and std::pow every sample was 3 expensive math calls
        // per sample for effectively constant values. Cache and only recompute when
        // smoothedAmount_ shifts by more than 0.002 (inaudible granularity).
        float cachedDampCoeff = lowpassCoeff(PitchScaleMathCore::lerp(6000.0, 2500.0, smoothedAmount_));
        float cachedFeedback  = static_cast<float>(std::clamp(std::exp(-3.0 * baseDelayMs_
                                    / PitchScaleMathCore::lerp(800.0, 3000.0, smoothedAmount_)), 0.15, 0.82));
        float cachedWet       = dbToGain(PitchScaleMathCore::lerp(-72.0, -14.0, smoothedAmount_));
        double lastCachedAmount = smoothedAmount_;

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedAmount_ = PitchScaleMathCore::onePoleNext(smoothedAmount_, cryptAmount, amountCoeff);
            if (smoothedAmount_ < 0.001)
                continue;

            if (std::abs(smoothedAmount_ - lastCachedAmount) > 0.002)
            {
                const double decayMs   = PitchScaleMathCore::lerp(800.0, 3000.0, smoothedAmount_);
                const double dampingHz = PitchScaleMathCore::lerp(6000.0, 2500.0, smoothedAmount_);
                cachedDampCoeff = lowpassCoeff(dampingHz);
                cachedFeedback  = static_cast<float>(std::clamp(std::exp(-3.0 * baseDelayMs_ / decayMs), 0.15, 0.82));
                cachedWet       = dbToGain(PitchScaleMathCore::lerp(-72.0, -14.0, smoothedAmount_));
                lastCachedAmount = smoothedAmount_;
            }

            const float dampCoeff = cachedDampCoeff;
            const float feedback  = cachedFeedback;
            const float wet       = cachedWet;
            const float in = input[i] + 0.00003f * std::sin(modPhase_);

            float sumL = 0.0f;
            float sumR = 0.0f;
            for (int l = 0; l < 8; ++l)
            {
                const int delaySamples = delaySamplesForLine(l);
                float y = lines_[l].read(delaySamples);
                dampState_[l] += dampCoeff * (y - dampState_[l]);
                dampState_[l] = PitchScaleMathCore::flushDenormal(dampState_[l]);
                y = dampState_[l];
                const float feed = in * 0.18f + y * feedback * ((l & 1) ? -0.72f : 0.72f);
                lines_[l].write(feed);
                if (l & 1) sumR += y; else sumL += y;
            }

            modPhase_ += 2.0 * juce::MathConstants<double>::pi * 0.11 / sampleRate_;
            if (modPhase_ >= 2.0 * juce::MathConstants<double>::pi)
                modPhase_ -= 2.0 * juce::MathConstants<double>::pi;

            outL[i] += PitchScaleMathCore::flushDenormal(sumL * 0.25f * wet);
            outR[i] += PitchScaleMathCore::flushDenormal(sumR * 0.25f * wet);
        }
    }

    int getCurrentLatencySamples() const noexcept { return 0; }

private:
    struct DelayLine
    {
        void setSize(int size)
        {
            buffer.setSize(1, std::max(1, size), false, true, true);
            buffer.clear();
            writePos = 0;
        }

        void reset() noexcept
        {
            buffer.clear();
            writePos = 0;
        }

        float read(int delay) const noexcept
        {
            const int n = buffer.getNumSamples();
            if (n <= 0) return 0.0f;
            int pos = writePos - std::clamp(delay, 1, n - 1);
            while (pos < 0) pos += n;
            return buffer.getReadPointer(0)[pos % n];
        }

        void write(float v) noexcept
        {
            const int n = buffer.getNumSamples();
            if (n <= 0) return;
            buffer.getWritePointer(0)[writePos] = PitchScaleMathCore::flushDenormal(v);
            writePos = (writePos + 1) % n;
        }

        juce::AudioBuffer<float> buffer;
        int writePos = 0;
    };

    int delaySamplesForLine(int line) const noexcept
    {
        static constexpr double ms[8] { 29.0, 37.0, 43.0, 53.0, 61.0, 71.0, 83.0, 97.0 };
        return static_cast<int>(sampleRate_ * ms[line & 7] / 1000.0);
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
    static constexpr double baseDelayMs_ = 61.0;
    std::array<DelayLine, 8> lines_;
    float dampState_[8] {};
    double smoothedAmount_ = 0.0;
    double modPhase_ = 0.0;
};

} // namespace ArrangementEditor
