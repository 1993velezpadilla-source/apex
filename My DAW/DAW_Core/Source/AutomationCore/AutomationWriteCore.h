#pragma once
#include "AutomationLaneCore.h"

namespace DAW {

/**
 * AutomationWriteCore — writes automation data during recording.
 *
 * In Write mode, continuously overwrites automation data at the
 * current playhead position. Destructive to existing data.
 *
 * Must be used on the message thread for data modification,
 * with audio thread providing timestamped parameter snapshots.
 */
class AutomationWriteCore
{
public:
    void setArmed(bool armed) noexcept { armed_ = armed; }
    bool isArmed() const noexcept { return armed_; }

    /** Record a value at the given position into the lane. */
    void writeValue(AutomationLaneCore& lane, juce::int64 position, float value)
    {
        if (!armed_) return;
        lane.addPoint(position, value);
    }

private:
    bool armed_ = false;
};

} // namespace DAW
