// ===========================================================================
// ClipStretchCore.h
// Pure logic for time stretching calculations.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include <cmath>

namespace ArrangementEditor
{
    class ClipStretchCore
    {
    public:
        // Calculate stretch ratio from original vs displayed length
        static float calculateStretchRatio(double originalLength, double displayedLength)
        {
            if (originalLength <= 0.0) return 1.f;
            return (float)(displayedLength / originalLength);
        }

        // Check if stretch is active (rate != 1.0)
        static bool isStretchActive(const ArrangementClipModel& clip)
        {
            return std::fabsf(clip.rate - 1.f) > 0.001f;
        }

        // Calculate actual playback duration given source length and rate
        static double stretchedDuration(double sourceLength, float rate)
        {
            if (rate <= 0.001f) return sourceLength;
            return sourceLength / rate;
        }
    };

} // namespace ArrangementEditor
