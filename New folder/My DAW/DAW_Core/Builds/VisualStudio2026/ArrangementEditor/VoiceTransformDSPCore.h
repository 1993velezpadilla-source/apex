// ===========================================================================
// VoiceTransformDSPCore.h
// Hidden post-shaping for ULTRA DEMON: tone, saturation, sub harmonic, safety.
// ===========================================================================
#pragma once
#include "VoiceTransformTypesCore.h"
#include <JuceHeader.h>
#include <cmath>

namespace ArrangementEditor
{

class VoiceTransformDSPCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        juce::ignoreUnused(maxBlockSize);
        reset();
    }

    void reset() noexcept
    {
        lowState_[0] = lowState_[1] = 0.f;
        darkState_[0] = darkState_[1] = 0.f;
        subState_[0] = subState_[1] = 0.f;
        previousIn_[0] = previousIn_[1] = 0.f;
        current_ = {};
    }

    void process(float* samples, int numSamples, int channel, const VoiceTransformTargets& target) noexcept
    {
        if (samples == nullptr || numSamples <= 0)
            return;

        const int ch = channel & 1;
        const float smooth = smoothingCoefficient(18.0f);
        float low  = lowState_[ch];
        float dark = darkState_[ch];
        float sub  = subState_[ch];
        float prev = previousIn_[ch];

        const float lowCoeff = onePoleCoeff(140.f);
        // Bug 38 fix: hoist sub filter coefficient out of sample loop.
        // onePoleCoeff(85.f) computes std::exp on every sample — it is a
        // constant-frequency coefficient that never changes.
        const float subCoeff = onePoleCoeff(85.f);

        for (int i = 0; i < numSamples; ++i)
        {
            smoothTargets(target, smooth);

            // Bug 39 fix: compute darkCutHz from the SMOOTHED current_.highCutDb,
            // not from target.highCutDb. Using the target value made darkCoeff
            // jump immediately on parameter changes (zipper on the shelf filter),
            // while all other tonal parameters followed the 18ms smoother.
            const float darkCutHz = juce::jlimit(1200.f, 16000.f, 10000.f + current_.highCutDb * 650.f);
            const float darkCoeff = onePoleCoeff(darkCutHz);

            float x = samples[i];
            low  += lowCoeff  * (x - low);
            dark += darkCoeff * (x - dark);

            const float lowGain  = juce::Decibels::decibelsToGain(current_.lowBoostDb) - 1.f;
            x += low * lowGain;

            const float darkMix = juce::jlimit(0.f, 0.85f, -current_.highCutDb / 12.f);
            x = x * (1.f - darkMix) + dark * darkMix;

            const float folded = std::abs(x + prev) * 0.5f;
            sub  += subCoeff * (folded - sub);
            prev  = x;
            x += sub * current_.subAmount * 0.35f;

            const float satDrive = 1.f + current_.saturation * 8.f;
            const float wet = std::tanh(x * satDrive) / std::tanh(satDrive);
            x = x * (1.f - current_.saturation) + wet * current_.saturation;

            samples[i] = juce::jlimit(-0.98f, 0.98f, x);
        }

        lowState_[ch]    = low;
        darkState_[ch]   = dark;
        subState_[ch]    = sub;
        previousIn_[ch]  = prev;
    }

private:
    void smoothTargets(const VoiceTransformTargets& target, float coeff) noexcept
    {
        current_.effectiveAmount += (target.effectiveAmount - current_.effectiveAmount) * coeff;
        current_.pitchSemitones += (target.pitchSemitones - current_.pitchSemitones) * coeff;
        current_.formantShift += (target.formantShift - current_.formantShift) * coeff;
        current_.lowBoostDb += (target.lowBoostDb - current_.lowBoostDb) * coeff;
        current_.highCutDb += (target.highCutDb - current_.highCutDb) * coeff;
        current_.saturation += (target.saturation - current_.saturation) * coeff;
        current_.subAmount += (target.subAmount - current_.subAmount) * coeff;
        current_.stereoWidth += (target.stereoWidth - current_.stereoWidth) * coeff;
        current_.transientShape += (target.transientShape - current_.transientShape) * coeff;
    }

    float smoothingCoefficient(float timeMs) const noexcept
    {
        const double samples = juce::jmax(1.0, sampleRate_ * (double) timeMs * 0.001);
        return 1.f - (float) std::exp(-1.0 / samples);
    }

    float onePoleCoeff(float hz) const noexcept
    {
        const float x = juce::jlimit(0.0001f, 0.99f,
            1.f - std::exp(-2.f * juce::MathConstants<float>::pi * hz / (float) sampleRate_));
        return x;
    }

    double sampleRate_ = 44100.0;
    VoiceTransformTargets current_;
    float lowState_[2] = { 0.f, 0.f };
    float darkState_[2] = { 0.f, 0.f };
    float subState_[2] = { 0.f, 0.f };
    float previousIn_[2] = { 0.f, 0.f };
};

} // namespace ArrangementEditor
