#pragma once
#include <JuceHeader.h>
#include "AutomationPointCore.h"
#include "../UtilityCore/Types.h"
#include <cstdint>
#include <vector>
#include <algorithm>
#include <unordered_map>

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

            // Binary search: find first point with timeSamples > samplePosition.
            // Points are sorted by timeSamples, so upper_bound gives O(log N).
            auto it = std::upper_bound(points.begin(), points.end(), samplePosition,
                [](int64_t pos, const AutomationPoint& pt) { return pos < pt.timeSamples; });

            if (it == points.begin() || it == points.end())
                return defaultValue;

            const auto& p1 = *it;
            const auto& p0 = *(it - 1);
            const auto span = p1.timeSamples - p0.timeSamples;
            if (span <= 0)
                return p1.value;

            const float t = (float)(samplePosition - p0.timeSamples) / (float)span;
            return AutomationCurveEvalCore::evaluate(p0.value, p1.value, t, p0.curveToNext, p0.tensionToNext);
        }
    };

    std::vector<LaneSnapshot> lanes;
    uint64_t version = 0;

    const LaneSnapshot* findLane(const TrackID& trackIdToFind, const juce::String& parameterIdToFind) const noexcept
    {
        // Fast path: check the O(1) hash index if available.
        if (laneIndexDirty_)
            rebuildLaneIndex();

        const auto key = trackIdToFind + "|" + parameterIdToFind;
        auto it = laneIndex_.find(key);
        if (it != laneIndex_.end())
            return it->second;

        return nullptr;
    }

    /**
        Realtime-safe immutable lookup. Unlike findLane(), this method never
        lazily mutates laneIndex_ and never constructs a temporary hash key.
        The snapshot is published before the audio callback observes it, so a
        bounded linear scan is preferable to first-use allocation on the RT
        path. Control/UI callers should continue to use findLane().
    */
    const LaneSnapshot* findLaneRT(const TrackID& trackIdToFind,
                                   const juce::String& parameterIdToFind) const noexcept
    {
        for (const auto& lane : lanes)
            if (lane.trackId == trackIdToFind
                && lane.parameterId == parameterIdToFind)
                return &lane;

        return nullptr;
    }

private:
    // Mutable O(1) lookup index, rebuilt lazily when lanes change.
    // The snapshot is const-qualified for the audio thread, so index
    // building is deferred to the first findLane() call after mutation.
    mutable bool laneIndexDirty_ = true;
    mutable std::unordered_map<juce::String, const LaneSnapshot*> laneIndex_;

    void rebuildLaneIndex() const
    {
        laneIndex_.clear();
        laneIndex_.reserve(lanes.size());
        for (const auto& lane : lanes)
            laneIndex_.emplace(lane.trackId + "|" + lane.parameterId, &lane);
        laneIndexDirty_ = false;
    }

    friend class AutomationSnapshotPublisherCore;
};

} // namespace DAW
