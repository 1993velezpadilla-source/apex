// ===========================================================================
// ClipGlueCore.h
// Pure logic for merging two adjacent clips into one.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"

namespace ArrangementEditor
{
    struct GlueResult
    {
        ArrangementClipModel merged;
        bool valid = false;
        std::string errorMessage;
    };

    class ClipGlueCore
    {
    public:
        // Merge two clips if they are adjacent and on the same track
        // Left clip's parameters take precedence (gain, pitch, rate)
        // Fade in from left, fade out from right
        static GlueResult glueClips(const ArrangementClipModel& left,
                                     const ArrangementClipModel& right)
        {
            GlueResult result;

            // Must be on same track
            if (left.trackIndex != right.trackIndex)
            {
                result.errorMessage = "Clips must be on the same track";
                return result;
            }

            // Must be adjacent or overlapping
            double gap = right.startTime - left.endTime();
            if (gap > 0.001) // allow tiny gaps
            {
                result.errorMessage = "Clips must be adjacent";
                return result;
            }

            // Create merged clip
            result.merged = left;
            result.merged.id = juce::Uuid(); // new ID
            result.merged.startTime = juce::jmin(left.startTime, right.startTime);
            result.merged.length = juce::jmax(right.endTime(), left.endTime()) - result.merged.startTime;

            // Keep fade in from left, fade out from right
            result.merged.fadeInLength = left.fadeInLength;
            result.merged.fadeOutLength = right.fadeOutLength;

            // Use left clip's processing params (user was warned)
            // gain, pitch, rate already copied from left

            result.valid = true;
            return result;
        }
    };

} // namespace ArrangementEditor
