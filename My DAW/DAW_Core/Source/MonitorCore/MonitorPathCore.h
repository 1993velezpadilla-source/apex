#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * MonitorPathCore — signal path from master to hardware output.
 *
 * Owns the concept of the monitoring signal chain that is
 * SEPARATE from the master/render path.
 *
 * Monitor path changes NEVER affect render output.
 *
 * Signal: masterBuffer → [monitor FX] → [dim/mute/mono] → [speaker trim] → hardware
 */
class MonitorPathCore
{
public:
    void setMonitorEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool isMonitorEnabled() const noexcept { return enabled_; }

    /** Returns true if the monitor path should produce output. */
    bool shouldOutput() const noexcept { return enabled_; }

private:
    bool enabled_ = true;
};

} // namespace DAW
