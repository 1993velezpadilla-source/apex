// ===========================================================================
// WailModulationCore.h
// Vibrato/wail LFO modulation for tape pitch engine.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>

namespace ArrangementEditor
{

class WailModulationCore
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = std::max(1.0, PitchScaleMathCore::sanitizeNaN(sampleRate, 44100.0));
        smoothCoeff_ = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.012);
        phase_ = 0.0;
        randomWalkPhase_ = 0.0;
        smoothedAmount_ = 0.0;
    }

    void reset() noexcept
    {
        phase_ = 0.0;
        randomWalkPhase_ = 0.0;
        smoothedAmount_ = 0.0;
    }

    double processRatio(double baseRatio, double wailAmount) noexcept
    {
        wailAmount = std::clamp(PitchScaleMathCore::sanitizeNaN(wailAmount), 0.0, 1.0);
        smoothedAmount_ = PitchScaleMathCore::onePoleNext(smoothedAmount_, wailAmount, smoothCoeff_);

        if (smoothedAmount_ < 0.02)
            return baseRatio;

        randomWalkPhase_ += 2.0 * juce::MathConstants<double>::pi * 0.07 / sampleRate_;
        if (randomWalkPhase_ >= 2.0 * juce::MathConstants<double>::pi)
            randomWalkPhase_ -= 2.0 * juce::MathConstants<double>::pi;

        const double rateJitter = 1.0 + 0.10 * std::sin(randomWalkPhase_);
        phase_ += 2.0 * juce::MathConstants<double>::pi * 0.6 * rateJitter / sampleRate_;
        if (phase_ >= 2.0 * juce::MathConstants<double>::pi)
            phase_ -= 2.0 * juce::MathConstants<double>::pi;

        const double depthCents = 12.0 * smoothedAmount_;
        const double cents = std::sin(phase_) * depthCents;
        return baseRatio * PitchScaleMathCore::semitonesToRatio(PitchScaleMathCore::centsToSemitones(cents));
    }

private:
    double sampleRate_ = 44100.0;
    double smoothCoeff_ = 0.0;
    double phase_ = 0.0;
    double randomWalkPhase_ = 0.0;
    double smoothedAmount_ = 0.0;
};

} // namespace ArrangementEditor
