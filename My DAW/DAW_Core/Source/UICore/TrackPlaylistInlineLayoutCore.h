#pragma once
#include <JuceHeader.h>

namespace DAW
{
// Geometry for a single track unfolded into virtual, NON-routing take lanes.
// Physical tracks keep their original indices and the RT playback graph
// remains untouched. Only the UI coordinate-to-physical-track projection
// changes. Gap is inserted after the owning track's main band.
struct TrackPlaylistInlineLayoutCore final
{
    // Keep each virtual take proportional to the parent's native vertical
    // zoom; 40px minimum preserves accessible buttons/selection gestures.
    static int takeHeightForTrackHeight(int trackHeight) noexcept
    {
        return juce::jlimit(40, 180,
            juce::roundToInt(66.0 * (double)juce::jmax(1, trackHeight) / 80.0));
    }

    static int topOfLane(int physicalLaneIndex, int trackHeight,
                         int expandedLaneIndex, int expandedPixels) noexcept
    {
        const int safeHeight = juce::jmax(1, trackHeight);
        const int extra = juce::jmax(0, expandedPixels);
        return 80 + juce::jmax(0, physicalLaneIndex) * safeHeight
               + (expandedLaneIndex >= 0
                  && physicalLaneIndex > expandedLaneIndex ? extra : 0);
    }

    static int laneAtY(int contentY, int trackHeight,
                       int expandedLaneIndex, int expandedPixels) noexcept
    {
        if (contentY < 80)
            return -1;
        const int h = juce::jmax(1, trackHeight);
        const int extra = juce::jmax(0, expandedPixels);
        int y = contentY - 80;
        if (expandedLaneIndex >= 0 && extra > 0)
        {
            const int insertedAt = (expandedLaneIndex + 1) * h;
            if (y >= insertedAt && y < insertedAt + extra)
                return -1; // a take lane is never a DIFFERENT routing track
            if (y >= insertedAt + extra)
                y -= extra;
        }
        return y / h;
    }

    static int takeY(int expandedLaneIndex, int trackHeight) noexcept
    {
        return topOfLane(expandedLaneIndex + 1, trackHeight, -1, 0);
    }
};
} // namespace DAW
