// ===========================================================================
// DarkVoiceBodyCore.h
// Low-mid body EQ and high-shelf cut for dark pitch zones.
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

class DarkVoiceBodyCore
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
        for (auto& c : channels_) c = ChannelState();
    }

    void process(float* samples, int numSamples, int channel, const UnifiedPitchSnapshot& snapshot)
    {
        juce::ScopedNoDenormals noDenormals;
        if (samples == nullptr || numSamples <= 0 || snapshot.darkIntensity < 0.05)
            return;

        auto& st = channels_[channel & 7];
        const double rmsCoeff = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.050);
        const float lp60c  = lowpassCoeff(60.0);
        const float lp120c = lowpassCoeff(120.0);
        const float lp300c = lowpassCoeff(300.0);

        for (int i = 0; i < numSamples; ++i)
        {
            const float x      = samples[i];
            const float low    = st.low120.processLowpass(x, lp120c);
            const float low60  = st.low60.processLowpass(x, lp60c);
            const float mid300 = st.mid300.processLowpass(x, lp300c);
            const float bandLow = low - low60;
            const float bandMid = mid300 - low;
            st.lowEnergy = PitchScaleMathCore::onePoleNext(st.lowEnergy, static_cast<double>(bandLow * bandLow), rmsCoeff);
            st.midEnergy = PitchScaleMathCore::onePoleNext(st.midEnergy, static_cast<double>(bandMid * bandMid), rmsCoeff);
        }

        const double lowMidRatio  = std::sqrt(st.lowEnergy) / (std::sqrt(st.midEnergy) + 1.0e-9);
        const bool   bassHeavy    = lowMidRatio > 1.5;
        const double bodyCenterHz = bassHeavy
            ? PitchScaleMathCore::lerp(185.0, 245.0, snapshot.darkIntensity)
            : PitchScaleMathCore::lerp(125.0, 190.0, snapshot.darkIntensity);
        const double bodyGainDb   = bassHeavy
            ? PitchScaleMathCore::lerp(0.0, 4.5, snapshot.darkIntensity)
            : PitchScaleMathCore::lerp(0.0, 9.0, snapshot.darkIntensity);
        const double safetyHpfHz  = bassHeavy ? 18.0 : 12.0;
        const float  bodyGain     = static_cast<float>(std::pow(10.0, bodyGainDb / 20.0) - 1.0);
        const float  shelfCut     = static_cast<float>(PitchScaleMathCore::lerp(1.0, std::pow(10.0, -5.5 / 20.0), snapshot.darkIntensity));
        const float  drive        = static_cast<float>(snapshot.darkIntensity * 0.48);

        const float bodyCoeff  = lowpassCoeff(bodyCenterHz);
        const float hpfCoeff   = lowpassCoeff(safetyHpfHz);
        const float shelfCoeff = lowpassCoeff(6000.0);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];
            const float hpLow = st.hpf.processLowpass(x, hpfCoeff);
            x -= hpLow;
            const float body = st.body.processLowpass(x, bodyCoeff);
            x += body * bodyGain;
            const float hfLow = st.shelf.processLowpass(x, shelfCoeff);
            const float hf    = x - hfLow;
            x = hfLow + hf * shelfCut;
            if (drive > 0.001f)
                x = std::tanh(x * (1.0f + drive)) / (1.0f + drive * 0.35f);
            samples[i] = PitchScaleMathCore::flushDenormal(x);
        }
    }

private:
    struct OnePole
    {
        float z = 0.0f;
        float processLowpass(float x, float a) noexcept
        {
            z += a * (x - z);
            z = PitchScaleMathCore::flushDenormal(z);
            return z;
        }
    };

    struct ChannelState
    {
        OnePole low60, low120, mid300, hpf, body, shelf;
        double lowEnergy = 0.0;
        double midEnergy = 0.0;
    };

    float lowpassCoeff(double hz) const noexcept
    {
        const double c = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * hz / sampleRate_);
        return static_cast<float>(std::clamp(c, 0.0001, 1.0));
    }

    double sampleRate_ = 44100.0;
    ChannelState channels_[8];
};

} // namespace ArrangementEditor
