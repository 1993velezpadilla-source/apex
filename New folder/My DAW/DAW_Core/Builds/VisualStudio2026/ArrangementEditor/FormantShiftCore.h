// ===========================================================================
// FormantShiftCore.h
// Lightweight formant shifter via frequency-warped filtering.
// Used by ClipPitchRenderPathCore.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include "UnifiedPitchStateCore.h"
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>

namespace ArrangementEditor
{

class FormantShiftCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        juce::ignoreUnused(maxBlockSize);
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : lowState_) s = 0.0f;
        for (auto& s : highState_) s = 0.0f;
        currentScale_ = 1.0;
    }

    void process(float* samples, int numSamples, int channel, const UnifiedPitchSnapshot& snapshot)
    {
        juce::ScopedNoDenormals noDenormals;
        if (samples == nullptr || numSamples <= 0 || std::abs(snapshot.formantScale - 1.0) < 0.01)
            return;

        const int ch = channel & 7;
        const double coeff = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.012);
        for (int i = 0; i < numSamples; ++i)
        {
            currentScale_ = PitchScaleMathCore::onePoleNext(currentScale_, snapshot.formantScale, coeff);
            const float lowCoeff  = static_cast<float>(std::clamp(0.014 / std::max(0.24, currentScale_), 0.002, 0.18));
            const float highCoeff = static_cast<float>(std::clamp(0.06 * std::max(0.5, currentScale_), 0.002, 0.35));
            lowState_[ch]  += lowCoeff  * (samples[i] - lowState_[ch]);
            highState_[ch] += highCoeff * (samples[i] - highState_[ch]);
            const float low  = lowState_[ch];
            const float high = samples[i] - highState_[ch];
            const float mix  = static_cast<float>(std::clamp(std::abs(currentScale_ - 1.0), 0.0, 1.0));
            const float warped = currentScale_ < 1.0
                ? (samples[i] + low * 0.90f - high * 0.34f)
                : (samples[i] - low * 0.25f + high * 0.45f);
            samples[i] = PitchScaleMathCore::flushDenormal(samples[i] + mix * (warped - samples[i]));
        }
    }

private:
    double sampleRate_ = 44100.0;
    double currentScale_ = 1.0;
    float lowState_[8]  {};
    float highState_[8] {};
};

} // namespace ArrangementEditor
