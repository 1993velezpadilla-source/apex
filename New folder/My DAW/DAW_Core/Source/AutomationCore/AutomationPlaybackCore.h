#pragma once
#include "AutomationLaneCore.h"
#include "AutomationTargetCore.h"
#include <vector>
#include <map>

namespace DAW {

/**
 * AutomationPlaybackCore — evaluates all active automation during playback.
 *
 * The audio engine calls this each block to get current automation
 * values for all active parameters. Values are then applied to
 * their respective targets (track volume, plugin params, etc.).
 *
 * Automation modes (per lane):
 *   Off   — no automation active
 *   Read  — reads existing data
 *   Write — overwrites continuously
 *   Touch — writes only while touching
 *   Latch — writes after first touch until stop
 */
class AutomationPlaybackCore
{
public:
    enum class Mode { Off, Read, Write, Touch, Latch };

    struct LaneBinding
    {
        AutomationTargetCore target;
        AutomationLaneCore lane;
        Mode mode = Mode::Off;
    };

    /** Add a new automation lane binding. Returns the lane index. */
    int addLane(const AutomationTargetCore& target)
    {
        LaneBinding binding;
        binding.target = target;
        binding.lane.setParameterId(target.stableParameterId);
        lanes_.push_back(std::move(binding));
        return (int)lanes_.size() - 1;
    }

    int getNumLanes() const { return (int)lanes_.size(); }

    LaneBinding* getLane(int index)
    {
        if (index >= 0 && index < (int)lanes_.size())
            return &lanes_[(size_t)index];
        return nullptr;
    }

    /** Get the automated value for a target at a position.
     *  Returns defaultValue if no automation exists. */
    float getValue(int laneIndex, juce::int64 position, float defaultValue) const
    {
        if (laneIndex < 0 || laneIndex >= (int)lanes_.size())
            return defaultValue;

        const auto& binding = lanes_[(size_t)laneIndex];
        if (binding.mode == Mode::Off) return defaultValue;
        if (binding.lane.isEmpty()) return defaultValue;

        return binding.lane.getValueAtPosition(position);
    }

    void setMode(int laneIndex, Mode mode)
    {
        if (laneIndex >= 0 && laneIndex < (int)lanes_.size())
            lanes_[(size_t)laneIndex].mode = mode;
    }

    void clearAll() { lanes_.clear(); }

private:
    std::vector<LaneBinding> lanes_;
};

} // namespace DAW
