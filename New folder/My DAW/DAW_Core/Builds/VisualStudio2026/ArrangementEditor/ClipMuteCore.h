// ===========================================================================
// ClipMuteCore.h
// Pure logic for muting clips.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"

namespace ArrangementEditor
{
    class ClipMuteCore
    {
    public:
        // Toggle mute state
        static void toggleMute(ArrangementClipModel& clip)
        {
            clip.muted = !clip.muted;
        }

        // Set mute state explicitly
        static void setMuted(ArrangementClipModel& clip, bool muted)
        {
            clip.muted = muted;
        }

        // Check if clip is effectively silent (muted or zero gain)
        static bool isSilent(const ArrangementClipModel& clip)
        {
            return clip.muted || clip.gain < 0.0001f;
        }
    };

} // namespace ArrangementEditor
