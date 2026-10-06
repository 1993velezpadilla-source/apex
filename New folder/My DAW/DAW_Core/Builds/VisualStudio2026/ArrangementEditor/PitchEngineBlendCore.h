// ===========================================================================
// PitchEngineBlendCore.h
// Equal-power unified pitch summing point with dry-leak detection.
// ===========================================================================
#pragma once
#include "PitchForensicAuditCore.h"
#include "UnifiedPitchStateCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{

class PitchEngineBlendCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        juce::ignoreUnused(maxBlockSize);
        dryTapGain_ = 1.0;
    }

    void reset() noexcept { dryTapGain_ = 1.0; }

    void blend(const float* dry, const float* independent, const float* tape,
               float* output, int numSamples, const UnifiedPitchSnapshot& snapshot,
               PitchForensicAuditCore* audit)
    {
        juce::ScopedNoDenormals noDenormals;
        if (output == nullptr || numSamples <= 0)
            return;

        const double coeff = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.006);
        const bool fullyWet = snapshot.wet > 0.999;
        const double targetDry = fullyWet ? 0.0 : 1.0 - snapshot.wet;
        // Linear crossfade: tapeBlend fades tape in and independent out so that
        // ind * (1 - tapeBlend) + tape * tapeBlend = exactly one pitched voice.
        // Equal-power (sin) was wrong here — at tapeBlend=1 both gains equal 1.0,
        // summing two pitch-shifted voices and producing the audible double-voice.
        const float tapeGain        = static_cast<float>(juce::jlimit(0.0, 1.0, snapshot.tapeBlend));
        const float independentGain = 1.0f - tapeGain;

        for (int i = 0; i < numSamples; ++i)
        {
            if (fullyWet)
            {
                // Hard-snap to zero instead of asymptotically approaching it.
                // The one-pole smoother never fully reaches 0.0, so dryTapGain_
                // stayed at ~1e-5 forever, permanently leaking dry signal and
                // triggering the audit every sample. Snapping eliminates this.
                dryTapGain_ = 0.0;
            }
            else
            {
                dryTapGain_ = PitchScaleMathCore::onePoleNext(dryTapGain_, targetDry, coeff);
            }

            if (dryTapGain_ > 1.0e-4 && fullyWet && audit != nullptr)
                audit->markDryLeak();

            const float d = dry != nullptr ? dry[i] : 0.0f;
            const float ind = independent != nullptr ? independent[i] : 0.0f;
            const float tap = tape != nullptr ? tape[i] : 0.0f;
            const float wet = ind * independentGain + tap * tapeGain;
            output[i] = PitchScaleMathCore::flushDenormal(static_cast<float>(dryTapGain_) * d + static_cast<float>(snapshot.wet) * wet);
        }
    }

private:
    double sampleRate_ = 44100.0;
    double dryTapGain_ = 1.0;
};

} // namespace ArrangementEditor
