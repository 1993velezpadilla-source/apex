#pragma once
#include <JuceHeader.h>
#include "AutomationPointCore.h"
#include "../UtilityCore/Types.h"
#include <cstdint>
#include <vector>

namespace DAW {

struct AutomationSnapshot
{
    struct LaneSnapshot
    {
        TrackID trackId;
        juce::String parameterId;
        bool enabled = true;
        std::vector<AutomationPoint> points;

        float getValueAtSample(int64_t samplePosition, float defaultValue) const noexcept
        {
            if (!enabled || points.empty())
                return defaultValue;

            if (samplePosition <= points.front().timeSamples)
                return points.front().value;

            if (samplePosition >= points.back().timeSamples)
                return points.back().value;

            for (size_t i = 1; i < points.size(); ++i)
            {
                const auto& p1 = points[i];
                if (samplePosition <= p1.timeSamples)
                {
                    const auto& p0 = points[i - 1];
                    const auto span = p1.timeSamples - p0.timeSamples;
                    if (span <= 0)
                        return p1.value;

                    const float t = (float)(samplePosition - p0.timeSamples) / (float)span;
                    return AutomationCurveEvalCore::evaluate(p0.value, p1.value, t, p0.curveToNext, p0.tensionToNext);
                }
            }

            return defaultValue;
        }
    };

    std::vector<LaneSnapshot> lanes;
    uint64_t version = 0;

    const LaneSnapshot* findLane(const TrackID& trackIdToFind, const juce::String& parameterIdToFind) const noexcept
    {
        for (const auto& lane : lanes)
            if (lane.trackId == trackIdToFind && lane.parameterId == parameterIdToFind)
                return &lane;

        return nullptr;
    }
};

} // namespace DAW
