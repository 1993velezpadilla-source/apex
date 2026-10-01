// ===========================================================================
// DemonAtmosphereCore.h
// Orchestrates subtle crypt atmosphere behind the unified dark pitch voice.
// ===========================================================================
#pragma once
#include "CryptReverbCore.h"
#include "EchoTapCore.h"
#include "HowlNoiseCore.h"
#include "PitchForensicAuditCore.h"
#include "UnifiedPitchStateCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{

enum class DemonAtmosphereStyle
{
    Off = 0,
    Subtle,
    Crypt
};

class DemonAtmosphereCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = std::max(1.0, PitchScaleMathCore::sanitizeNaN(sampleRate, 44100.0));
        maxBlockSize_ = std::max(1, maxBlockSize);
        crypt_.prepare(sampleRate_, maxBlockSize_);
        echo_.prepare(sampleRate_, maxBlockSize_);
        howl_.prepare(sampleRate_, maxBlockSize_);
        returnL_.setSize(1, maxBlockSize_, false, true, true);
        returnR_.setSize(1, maxBlockSize_, false, true, true);
        howlBus_.setSize(1, maxBlockSize_, false, true, true);
        reset();
    }

    void reset()
    {
        crypt_.reset();
        echo_.reset();
        howl_.reset();
        dryRms_ = 0.0;
        atmoRms_ = 0.0;
        lastBusReturnDb_ = -120.0;
        atmoBypassed_ = true;
    }

    void setStyle(DemonAtmosphereStyle style) noexcept
    {
        style_ = style;
    }

    DemonAtmosphereStyle getStyle() const noexcept { return style_; }

    void processAndSum(const float* voice, float* output, int numSamples, int channel,
                       const UnifiedPitchSnapshot& snapshot, PitchForensicAuditCore* audit)
    {
        juce::ScopedNoDenormals noDenormals;
        if (voice == nullptr || output == nullptr || numSamples <= 0)
            return;

        // Bug 35 fix: resize internal buffers on demand instead of silently
        // clamping safeSamples — dropped samples produce silence gaps in output.
        if (returnL_.getNumSamples() < numSamples)
        {
            returnL_.setSize(1, numSamples, false, true, true);
            returnR_.setSize(1, numSamples, false, true, true);
            howlBus_.setSize(1, numSamples, false, true, true);
            maxBlockSize_ = numSamples;
        }
        const int safeSamples = numSamples;
        const double styleScale = styleScaleFor(style_);
        const double cryptAmount = snapshot.cryptAmount * styleScale;
        const double echoAmount = snapshot.echoAmount * styleScale;
        const double howlAmount = snapshot.howlAmount * styleScale;
        const double total = cryptAmount + echoAmount + howlAmount;

        atmoBypassed_ = style_ == DemonAtmosphereStyle::Off || total < 0.02;
        if (atmoBypassed_)
            return;

        float* retL = returnL_.getWritePointer(0);
        float* retR = returnR_.getWritePointer(0);
        float* howlBus = howlBus_.getWritePointer(0);
        juce::FloatVectorOperations::clear(retL, safeSamples);
        juce::FloatVectorOperations::clear(retR, safeSamples);
        juce::FloatVectorOperations::clear(howlBus, safeSamples);

        if (cryptAmount > 0.001)
        {
            // Crypt reverb removed — was source of static noise on pitch.
            juce::ignoreUnused(cryptAmount);
        }

        if (echoAmount > 0.001)
        {
            // Echo tap removed — was source of static noise on pitch.
            juce::ignoreUnused(echoAmount);
        }

        if (howlAmount > 0.001)
        {
            howl_.process(voice, howlBus, safeSamples, howlAmount);
            crypt_.process(howlBus, retL, retR, safeSamples, std::max(cryptAmount, howlAmount * 0.65));
            if (audit != nullptr) audit->markHowl();
        }

        const double rmsCoeff = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.400);
        for (int i = 0; i < safeSamples; ++i)
        {
            const float atmo = (channel & 1) ? retR[i] : retL[i];
            dryRms_ = PitchScaleMathCore::onePoleNext(dryRms_, static_cast<double>(voice[i] * voice[i]), rmsCoeff);
            atmoRms_ = PitchScaleMathCore::onePoleNext(atmoRms_, static_cast<double>(atmo * atmo), rmsCoeff);
        }

        const double dry = std::sqrt(dryRms_ + 1.0e-12);
        const double atmo = std::sqrt(atmoRms_ + 1.0e-12);
        const double guardGain = atmo > dry ? dry / atmo : 1.0;
        const double cryptHotGain = style_ == DemonAtmosphereStyle::Crypt ? std::pow(10.0, 3.0 / 20.0) : 1.0;
        const float finalGain = static_cast<float>(std::min(guardGain, 1.0) * cryptHotGain);

        for (int i = 0; i < safeSamples; ++i)
            output[i] = PitchScaleMathCore::flushDenormal(output[i] + (((channel & 1) ? retR[i] : retL[i]) * finalGain));

        lastBusReturnDb_ = 20.0 * std::log10((std::sqrt(atmoRms_ + 1.0e-12) * finalGain) / (std::sqrt(dryRms_ + 1.0e-12) + 1.0e-12));
    }

    int getCurrentLatencySamples() const noexcept { return 0; }
    double getLastBusReturnDb() const noexcept { return lastBusReturnDb_; }
    bool isBypassed() const noexcept { return atmoBypassed_; }

private:
    static double styleScaleFor(DemonAtmosphereStyle style) noexcept
    {
        switch (style)
        {
            case DemonAtmosphereStyle::Off: return 0.0;
            case DemonAtmosphereStyle::Crypt: return std::pow(10.0, 3.0 / 20.0);
            case DemonAtmosphereStyle::Subtle:
            default: return 1.0;
        }
    }

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    DemonAtmosphereStyle style_ = DemonAtmosphereStyle::Subtle;
    CryptReverbCore crypt_;
    EchoTapCore echo_;
    HowlNoiseCore howl_;
    juce::AudioBuffer<float> returnL_;
    juce::AudioBuffer<float> returnR_;
    juce::AudioBuffer<float> howlBus_;
    double dryRms_ = 0.0;
    double atmoRms_ = 0.0;
    double lastBusReturnDb_ = -120.0;
    bool atmoBypassed_ = true;
};

} // namespace ArrangementEditor
