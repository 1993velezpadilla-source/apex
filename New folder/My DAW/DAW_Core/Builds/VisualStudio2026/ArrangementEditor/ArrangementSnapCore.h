// ===========================================================================
// ArrangementSnapCore.h
// Pure logic for snapping time positions to grid or clip edges.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include <vector>
#include <cmath>

namespace ArrangementEditor
{
    enum class SnapMode
    {
        Free,        // no snapping
        ThirtySecond,// 1/32 notes
        Sixteenth,   // 1/16 notes
        Eighth,      // 1/8 notes
        Quarter,     // 1/4 notes
        Half,        // 1/2 notes
        Bar,         // snap to bar lines
        Bar2,        // 2 bars
        Bar4,        // 4 bars
        Beat,        // snap to beats
        ClipEdge     // snap to clip boundaries
    };

    class ArrangementSnapCore
    {
    public:
        // Snap a time position to the grid
        static double snapToGrid(double time, double bpm, SnapMode mode)
        {
            if (mode == SnapMode::Free || bpm <= 0.0)
                return time;

            double beatDuration = 60.0 / bpm;
            double gridSize = 0.0;

            switch (mode)
            {
            case SnapMode::ThirtySecond: gridSize = beatDuration * 0.125; break;
            case SnapMode::Sixteenth:    gridSize = beatDuration * 0.25;  break;
            case SnapMode::Eighth:       gridSize = beatDuration * 0.5;   break;
            case SnapMode::Quarter:      gridSize = beatDuration;         break;
            case SnapMode::Half:         gridSize = beatDuration * 2.0;   break;
            case SnapMode::Bar:       gridSize = beatDuration * 4.0; break;
            case SnapMode::Bar2:      gridSize = beatDuration * 8.0; break;
            case SnapMode::Bar4:      gridSize = beatDuration * 16.0; break;
            case SnapMode::Beat:      gridSize = beatDuration;       break;
            default: return time;
            }

            if (gridSize <= 0.0) return time;
            return std::round(time / gridSize) * gridSize;
        }

        // Snap to nearest clip edge within threshold
        static double snapToClipEdge(double time,
                                      const std::vector<ArrangementClipModel>& clips,
                                      double threshold = 0.1)
        {
            double closestDist = threshold;
            double snappedTime = time;

            for (auto& clip : clips)
            {
                // Check start edge
                double distStart = std::fabs(time - clip.startTime);
                if (distStart < closestDist)
                {
                    closestDist = distStart;
                    snappedTime = clip.startTime;
                }

                // Check end edge
                double distEnd = std::fabs(time - clip.endTime());
                if (distEnd < closestDist)
                {
                    closestDist = distEnd;
                    snappedTime = clip.endTime();
                }
            }

            return snappedTime;
        }
    };

} // namespace ArrangementEditor
