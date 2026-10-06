// ===========================================================================
// PitchTimelineCompensationCore.h
// Arrangement length and tape read-ratio naming split.
// ===========================================================================
#pragma once
#include "UnifiedPitchStateCore.h"

namespace ArrangementEditor
{

class PitchTimelineCompensationCore
{
public:
    static double getProcessedTimelineLengthForArrangement(double sourceLengthSeconds,
                                                           double stretchRatio) noexcept
    {
        return sourceLengthSeconds * (stretchRatio > 0.0001 ? stretchRatio : 1.0);
    }

    static double getEffectiveAudioReadRatio(const UnifiedPitchSnapshot& snapshot) noexcept
    {
        return snapshot.pitchScale / (snapshot.stretchRatio > 0.0001 ? snapshot.stretchRatio : 1.0);
    }

    static double compensatedTapeOutputSamples(double outputSamples, const UnifiedPitchSnapshot& snapshot) noexcept
    {
        juce::ignoreUnused(snapshot);
        return outputSamples;
    }
};

} // namespace ArrangementEditor
