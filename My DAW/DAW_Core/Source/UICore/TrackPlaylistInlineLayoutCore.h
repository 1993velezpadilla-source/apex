#pragma once
#include <algorithm>
#include <cmath>

namespace DAW
{
// Geometry for a single track unfolded into virtual, NON-routing take lanes.
// Physical tracks keep their original indices and the RT playback graph
// remains untouched. Only the UI coordinate-to-physical-track projection
// changes. Gap is inserted after the owning track's main band.
struct TrackPlaylistInlineLayoutCore final
{
    static constexpr int contentTop = 80;

    // Keep each virtual take proportional to the parent's native vertical
    // zoom; 40px minimum preserves accessible buttons/selection gestures.
    static int takeHeightForTrackHeight(int trackHeight) noexcept
    {
        return std::clamp(
            static_cast<int>(std::lround(66.0 * std::max(1, trackHeight) / 80.0)),
            40, 180);
    }

    static int topOfLane(int physicalLaneIndex, int trackHeight,
                         int expandedLaneIndex, int expandedPixels) noexcept
    {
        const int safeHeight = std::max(1, trackHeight);
        const int extra = std::max(0, expandedPixels);
        return contentTop + std::max(0, physicalLaneIndex) * safeHeight
               + (expandedLaneIndex >= 0
                  && physicalLaneIndex > expandedLaneIndex ? extra : 0);
    }

    static int laneAtY(int contentY, int trackHeight,
                       int expandedLaneIndex, int expandedPixels) noexcept
    {
        if (contentY < contentTop)
            return -1;
        const int h = std::max(1, trackHeight);
        const int extra = std::max(0, expandedPixels);
        int y = contentY - contentTop;
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

    struct Anchor
    {
        enum class Band { header, track, takes };
        Band band = Band::header;
        int lane = 0;
        double position = 0.0;
    };

    static Anchor anchorAtY(double contentY, int trackHeight,
                            int expandedLaneIndex, int expandedPixels) noexcept
    {
        const double y = contentY - contentTop;
        if (y < 0)
            return { Anchor::Band::header, 0, contentY };

        const double h = std::max(1, trackHeight);
        const int extra = expandedLaneIndex >= 0 ? std::max(0, expandedPixels) : 0;
        const double gapStart = (expandedLaneIndex + 1.0) * h;
        if (extra > 0 && y >= gapStart && y < gapStart + extra)
            return { Anchor::Band::takes, expandedLaneIndex, (y - gapStart) / extra };

        const double physicalY = (extra > 0 && y >= gapStart + extra) ? y - extra : y;
        const auto lane = static_cast<int>(physicalY / h);
        return { Anchor::Band::track, lane, (physicalY - lane * h) / h };
    }

    static double yForAnchor(Anchor anchor, int trackHeight,
                              int expandedLaneIndex, int expandedPixels) noexcept
    {
        if (anchor.band == Anchor::Band::header)
            return anchor.position;

        const int h = std::max(1, trackHeight);
        const int top = topOfLane(anchor.lane, h, expandedLaneIndex, expandedPixels);
        if (anchor.band == Anchor::Band::takes)
        {
            const int extra = expandedLaneIndex == anchor.lane ? std::max(0, expandedPixels) : 0;
            return top + h + anchor.position * extra;
        }
        return top + anchor.position * h;
    }

    static int takeY(int expandedLaneIndex, int trackHeight) noexcept
    {
        return topOfLane(expandedLaneIndex + 1, trackHeight, -1, 0);
    }
};
} // namespace DAW
