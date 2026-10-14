#pragma once

#include "AutomationLaneCore.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace DAW {

/** Deterministic, bounded UI-thread transformation of a whole automation lane.
 *  Preserve each point's source curve/tension and timestamp unless the
 *  operation explicitly shifts time. This never writes from audio thread.
 */
struct AutomationCurveTransformCore
{
    enum class Operation { InvertValues, CompressHalf, ExpandDouble, ShiftRightBeat };

    static bool supports(const juce::String& parameter) noexcept
    {
        return parameter == AutomationLaneCore::trackVolumeParameterId
            || parameter == AutomationLaneCore::trackPanParameterId
            || parameter.startsWith("plugin.")
            || parameter.startsWith("instrument.");
    }

    static bool transform(const juce::String& parameter,
                          Operation operation,
                          const std::vector<AutomationPoint>& source,
                          double sampleRate, double tempoBpm,
                          std::vector<AutomationPoint>& destination)
    {
        if (!supports(parameter) || source.empty() || source.size() > 32768)
            return false;

        const float lo = parameter == AutomationLaneCore::trackPanParameterId ? -1.0f : 0.0f;
        const float hi = parameter == AutomationLaneCore::trackVolumeParameterId ? 2.0f : 1.0f;
        const float centre = (lo + hi) * 0.5f;
        int64_t shift = 0;
        if (operation == Operation::ShiftRightBeat)
        {
            if (!std::isfinite(sampleRate) || !std::isfinite(tempoBpm)
                || sampleRate <= 0.0 || tempoBpm <= 0.0)
                return false;
            const double samples = sampleRate * 60.0 / tempoBpm;
            if (!std::isfinite(samples) || samples < 1.0
                || samples > static_cast<double>(std::numeric_limits<int64_t>::max() / 4))
                return false;
            shift = static_cast<int64_t>(std::llround(samples));
        }

        std::vector<AutomationPoint> next;
        next.reserve(source.size());
        for (auto point : source)
        {
            if (point.timeSamples < 0 || !std::isfinite(point.value)
                || !std::isfinite(point.tensionToNext))
                return false;
            switch (operation)
            {
                case Operation::InvertValues: point.value = lo + hi - point.value; break;
                case Operation::CompressHalf: point.value = centre + (point.value - centre) * 0.5f; break;
                case Operation::ExpandDouble: point.value = centre + (point.value - centre) * 2.0f; break;
                case Operation::ShiftRightBeat:
                    if (point.timeSamples > std::numeric_limits<int64_t>::max() - shift)
                        return false;
                    point.timeSamples += shift;
                    break;
                default: return false;
            }
            point.value = juce::jlimit(lo, hi, point.value);
            next.push_back(point);
        }

        destination = std::move(next);
        return true;
    }
};

} // namespace DAW
