#pragma once

#include "AutomationLaneCore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace DAW {

/**
 * Clipboard for the *whole* visible legacy automation curve.
 *
 * Keeps musical PPQ positions rather than pixel or sample coordinates so a
 * curve copied at 48 kHz remains in the same beat positions if pasted into
 * a 44.1 kHz session or after a tempo change. The existing per-point and
 * per-segment clipboards are intentionally unaffected.
 *
 * MESSAGE THREAD ONLY: copy/paste allocates; never use from the audio callback.
 * Parameter identity must match exactly to prevent accidental cross-parameter
 * reinterpretation (e.g. pan -1..1 pasted into volume 0..1 or foreign VST3).
 */
class AutomationLaneCurveClipboardCore
{
public:
    struct MusicalPoint
    {
        double timePPQ = 0.0;
        float value = 0.0f;
        AutomationCurveType curve = AutomationCurveType::Linear;
        float tension = 0.0f;
    };

    static AutomationLaneCurveClipboardCore& shared()
    {
        static AutomationLaneCurveClipboardCore clipboard;
        return clipboard;
    }

    void clear()
    {
        points_.clear();
        parameterId_.clear();
    }

    bool hasCurveFor(const juce::String& parameterId) const
    {
        return !points_.empty() && parameterId.isNotEmpty() && parameterId_ == parameterId;
    }

    size_t pointCount() const noexcept { return points_.size(); }

    bool copyFrom(const AutomationLaneCore& lane, double sampleRate, double tempoBpm)
    {
        if (!validClock(sampleRate, tempoBpm) || lane.points.empty()
            || lane.points.size() > maxPoints || lane.getParameterId().isEmpty())
            return false;

        std::vector<MusicalPoint> next;
        next.reserve(lane.points.size());
        const double ppqPerSample = tempoBpm / (60.0 * sampleRate);
        for (const auto& point : lane.points)
        {
            if (point.timeSamples < 0 || !std::isfinite(point.value)
                || !std::isfinite(point.tensionToNext))
                return false;
            const double ppq = static_cast<double>(point.timeSamples) * ppqPerSample;
            if (!std::isfinite(ppq))
                return false;
            next.push_back({ ppq, point.value, point.curveToNext, point.tensionToNext });
        }

        // On failure the previous clipboard must remain intact.
        points_ = std::move(next);
        parameterId_ = lane.getParameterId();
        return true;
    }

    bool makePastedPoints(const juce::String& destinationParameter,
                          double sampleRate, double tempoBpm,
                          std::vector<AutomationPoint>& result) const
    {
        if (!hasCurveFor(destinationParameter) || !validClock(sampleRate, tempoBpm))
            return false;

        std::vector<AutomationPoint> next;
        next.reserve(points_.size());
        const double samplesPerPPQ = (60.0 * sampleRate) / tempoBpm;

        for (const auto& point : points_)
        {
            const double samplePos = point.timePPQ * samplesPerPPQ;
            // Keep llround strictly inside the representable int64 range.
            if (!std::isfinite(samplePos) || samplePos < 0.0
                || samplePos > static_cast<double>(std::numeric_limits<int64_t>::max() / 2))
                return false;

            AutomationPoint copy;
            copy.timeSamples = static_cast<int64_t>(std::llround(samplePos));
            copy.value = AutomationLaneCore::clampValueForParameter(destinationParameter, point.value);
            copy.curveToNext = point.curve;
            copy.tensionToNext = juce::jlimit(-1.0f, 1.0f, point.tension);
            next.push_back(copy);
        }
        result = std::move(next);
        return true;
    }

private:
    static constexpr size_t maxPoints = 32768;

    static bool validClock(double sampleRate, double tempoBpm) noexcept
    {
        return std::isfinite(sampleRate) && std::isfinite(tempoBpm)
            && sampleRate > 0.0 && tempoBpm > 0.0;
    }

    std::vector<MusicalPoint> points_;
    juce::String parameterId_;
};

} // namespace DAW
