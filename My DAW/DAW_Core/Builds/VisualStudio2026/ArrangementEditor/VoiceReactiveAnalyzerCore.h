// ===========================================================================
// VoiceReactiveAnalyzerCore.h
// Audio-thread safe RMS/envelope/transient follower for reactive vocal effects.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace ArrangementEditor
{

class VoiceReactiveAnalyzerCore
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        // Bug 48 fix: pre-compute all four constant coefficients in prepare()
        // instead of calling std::exp on every sample/block inside processBlock.
        attackCoeff_     = coefficient(18.0f);
        releaseCoeff_    = coefficient(220.0f);
        energyAttCoeff_  = coefficient(25.0f);
        energyRelCoeff_  = coefficient(240.0f);
        reset();
    }

    void reset() noexcept
    {
        envelope_ = 0.f;
        smoothedEnergy_ = 0.f;
        previousEnvelope_ = 0.f;
    }

    float processBlock(const float* samples, int numSamples) noexcept
    {
        if (samples == nullptr || numSamples <= 0)
            return smoothedEnergy_;

        double sumSquares = 0.0;
        float peakEnvelope = envelope_;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = std::abs(samples[i]);
            const float coeff = x > envelope_ ? attackCoeff_ : releaseCoeff_;
            envelope_ = x + coeff * (envelope_ - x);
            peakEnvelope = juce::jmax(peakEnvelope, envelope_);
            sumSquares += (double) samples[i] * (double) samples[i];
        }

        const float rms = std::sqrt((float) (sumSquares / (double) numSamples));
        const float rmsEnergy = juce::jlimit(0.f, 1.f, rms * 3.0f);
        const float envelopeEnergy = juce::jlimit(0.f, 1.f, peakEnvelope * 2.5f);
        const float transientEnergy = juce::jlimit(0.f, 1.f, (peakEnvelope - previousEnvelope_) * 8.0f);
        previousEnvelope_ = peakEnvelope;

        const float target = juce::jlimit(0.f, 1.f,
            rmsEnergy * 0.55f + envelopeEnergy * 0.30f + transientEnergy * 0.15f);

        const float energyCoeff = target > smoothedEnergy_ ? energyAttCoeff_ : energyRelCoeff_;
        smoothedEnergy_ = target + energyCoeff * (smoothedEnergy_ - target);
        return smoothedEnergy_;
    }

    float getEnergy() const noexcept { return smoothedEnergy_; }

private:
    float coefficient(float timeMs) const noexcept
    {
        const double samples = juce::jmax(1.0, sampleRate_ * (double) timeMs * 0.001);
        return (float) std::exp(-1.0 / samples);
    }

    double sampleRate_ = 44100.0;
    float envelope_ = 0.f;
    float smoothedEnergy_ = 0.f;
    float previousEnvelope_ = 0.f;
    float attackCoeff_    = 0.f;
    float releaseCoeff_   = 0.f;
    float energyAttCoeff_ = 0.f;
    float energyRelCoeff_ = 0.f;
};

} // namespace ArrangementEditor
