// ===========================================================================
// ClipSplitCore.h
// Pure logic for splitting a clip at a specific time position.
//
// SPLIT RULES:
//   - Both halves inherit timePitch unchanged (same mode, pitch, stretch, formant).
//   - sourceOffset advances correctly for the right clip.
//   - sourceStartSample / sourceEndSample are set precisely for each half.
//   - Fades are reset on the cut edge.
//   - TimePitchState is NEVER modified during split.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include "TimePitchSerializationCore.h"

namespace ArrangementEditor
{
    struct SplitResult
    {
        ArrangementClipModel left;
        ArrangementClipModel right;
        bool valid = false;
    };

    class ClipSplitCore
    {
    public:
        // Split a clip at splitTime (project time, not clip-relative).
        // Returns two new clips with correct offsets and lengths.
        // Fades are reset on the cut edges.
        static SplitResult splitClip(const ArrangementClipModel& clip, double splitTime)
        {
            SplitResult result;

            const double visualLength = juce::jmax(0.000001, clip.visualLength());
            const double visualEndTime = clip.startTime + visualLength;

            if (splitTime <= clip.startTime || splitTime >= visualEndTime)
            {
                result.valid = false;
                return result;
            }

            const double leftVisualLength  = splitTime - clip.startTime;
            const double rightVisualLength = visualEndTime - splitTime;
            const double splitFraction = juce::jlimit(0.0, 1.0, leftVisualLength / visualLength);

            const double leftLength  = clip.length * splitFraction;
            const double rightLength = juce::jmax(0.0, clip.length - leftLength);
            const int64_t sourceStart = clip.sourceStartSample;
            const int64_t sourceEnd = clip.sourceEndSample;
            const bool hasExplicitSourceBounds = sourceEnd > sourceStart;

            // Left clip — same start, shorter end
            result.left = clip;
            result.left.id            = juce::Uuid();
            result.left.length        = leftLength;
            result.left.fadeOutLength = 0.f;
            // sourceEndSample shrinks proportionally (in source-file sample space)
            // sourceOffset and sourceStartSample stay the same
            if (hasExplicitSourceBounds)
            {
                const double srcLen = (double)(sourceEnd - sourceStart);
                const double frac   = splitFraction;
                result.left.sourceStartSample = sourceStart;
                result.left.sourceEndSample = sourceStart
                    + (int64_t)std::round(srcLen * frac);
            }

            // Right clip — starts at splitTime, offset moves forward
            result.right = clip;
            result.right.id            = juce::Uuid();
            result.right.startTime     = splitTime;
            result.right.length        = rightLength;
            result.right.fadeInLength  = 0.f;
            result.right.sourceOffset  = clip.sourceOffset + leftLength;
            // sourceStartSample advances to where the left clip ended
            if (hasExplicitSourceBounds)
            {
                result.right.sourceStartSample = result.left.sourceEndSample;
                result.right.sourceEndSample = sourceEnd;
            }

            // TimePitchState: both halves get EXACT copy — no modification
            result.left.timePitch  = TimePitchSerializationCore::inheritForSplit(clip.timePitch);
            result.right.timePitch = TimePitchSerializationCore::inheritForSplit(clip.timePitch);

            result.valid = true;
            return result;
        }
    };

} // namespace ArrangementEditor
