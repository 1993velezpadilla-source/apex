#pragma once

#include "AutomationLaneCore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace DAW {

/**
 * Bounded offline pencil-stroke commit. Mouse/touch samples are collected by
 * the arrangement editor and committed as a SINGLE lane edit at mouseUp.
 * No alloc, lock or UI call occurs inside the playback/audio callback.
 */
struct AutomationFreehandStrokeCore
{
    static constexpr size_t maxStrokePoints = 4096;

    static bool commit(AutomationLaneCore& lane,
                       const std::vector<AutomationPoint>& samples)
    {
        if (samples.empty() || samples.size() > maxStrokePoints
            || !lane.isEnabled() || lane.getParameterId().isEmpty())
            return false;

        std::vector<AutomationPoint> clean;
        clean.reserve(samples.size());
        for (auto point : samples)
        {
            if (point.timeSamples < 0 || !std::isfinite(point.value))
                return false;
            point.value = AutomationLaneCore::clampValueForParameter(
                lane.getParameterId(), point.value);
            point.curveToNext = AutomationCurveType::Linear;
            point.tensionToNext = 0.0f;
            clean.push_back(point);
        }

        // Reverse drag direction is permitted; time ordering stays canonical.
        std::stable_sort(clean.begin(), clean.end(),
            [](const AutomationPoint& a, const AutomationPoint& b)
            {
                return a.timeSamples < b.timeSamples;
            });

        std::vector<AutomationPoint> stroke;
        stroke.reserve(clean.size());
        for (const auto& point : clean)
        {
            if (!stroke.empty() && stroke.back().timeSamples == point.timeSamples)
                stroke.back() = point; // last user movement at this timestamp wins
            else
                stroke.push_back(point);
        }

        const auto first = stroke.front().timeSamples;
        const auto last = stroke.back().timeSamples;
        std::vector<AutomationPoint> updated;
        updated.reserve(lane.points.size() + stroke.size());
        for (const auto& old : lane.points)
            if (old.timeSamples < first || old.timeSamples > last)
                updated.push_back(old);
        updated.insert(updated.end(), stroke.begin(), stroke.end());

        lane.points = std::move(updated);
        lane.sortAndResolveDuplicates();
        return true;
    }
};

} // namespace DAW
