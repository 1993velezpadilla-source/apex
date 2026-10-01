// ===========================================================================
// IndependentPitchEngineCore.h
// Wrapper for the existing duration-preserving pitch engine.
// ===========================================================================
#pragma once
#include "PhaseVocoderStretchCore.h"
#include "UnifiedPitchStateCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{

class IndependentPitchEngineCore
{
public:
    void prepare(double sampleRate, int maxBlockSize, TimePitchQuality quality = TimePitchQuality::Realtime)
    {
        const auto effectiveQuality = quality == TimePitchQuality::Realtime
            ? TimePitchQuality::Balanced
            : quality;
        engine_.prepare(sampleRate, maxBlockSize, effectiveQuality);
    }

    void reset() { engine_.reset(); }

    void setState(const TimePitchState& state, bool preserveFormants)
    {
        TimePitchState s = state;
        s.mode = TimePitchMode::Vocal;
        s.preserveFormants = preserveFormants;
        engine_.setState(s);
    }

    void renderSegment(const float* src, int srcTotal, int64_t srcStart, int64_t srcEnd,
                       float* dst, int dstNumSamples, int channel)
    {
        juce::ScopedNoDenormals noDenormals;
        engine_.renderSegment(src, srcTotal, srcStart, srcEnd, dst, dstNumSamples, channel);
    }

    int getCurrentLatencySamples() const noexcept
    {
        return static_cast<int>(engine_.getStretchRatio() * 256.0);
    }

private:
    PhaseVocoderStretchCore engine_;
};

} // namespace ArrangementEditor
