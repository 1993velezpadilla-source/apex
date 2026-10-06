// ===========================================================================
// PitchWaveformInvalidationCore.h
// Async waveform invalidation trigger policy for pitch-affecting changes.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include "UnifiedPitchStateCore.h"

namespace ArrangementEditor
{

class PitchWaveformInvalidationCore
{
public:
    static bool shouldInvalidate(const ArrangementClipModel& before,
                                 const ArrangementClipModel& after) noexcept
    {
        const auto& a = before.timePitch;
        const auto& b = after.timePitch;
        return a.pitchEngineVersion != b.pitchEngineVersion
            || std::abs(a.pitchSemitones - b.pitchSemitones) > 0.0001
            || std::abs(a.fineTuneCents - b.fineTuneCents) > 0.01
            || std::abs(a.stretchRatio - b.stretchRatio) > 0.0001
            || std::abs(a.formantSemitones - b.formantSemitones) > 0.0001
            || std::abs(UnifiedPitchStateCore::snapshotFromState(a).cryptAmount - UnifiedPitchStateCore::snapshotFromState(b).cryptAmount) > 0.0001
            || std::abs(UnifiedPitchStateCore::snapshotFromState(a).echoAmount - UnifiedPitchStateCore::snapshotFromState(b).echoAmount) > 0.0001
            || std::abs(UnifiedPitchStateCore::snapshotFromState(a).howlAmount - UnifiedPitchStateCore::snapshotFromState(b).howlAmount) > 0.0001
            || std::abs(UnifiedPitchStateCore::snapshotFromState(a).wailAmount - UnifiedPitchStateCore::snapshotFromState(b).wailAmount) > 0.0001
            || a.mode != b.mode
            || std::abs(before.gain - after.gain) > 0.0001f
            || std::abs(before.fadeInLength - after.fadeInLength) > 0.0001f
            || std::abs(before.fadeOutLength - after.fadeOutLength) > 0.0001f
            || before.sourceOffset != after.sourceOffset
            || before.reversed != after.reversed;
    }

    static void markInvalidated(ArrangementClipModel& clip) noexcept
    {
        clip.bumpProcessedWaveformVersion();
    }
};

} // namespace ArrangementEditor
