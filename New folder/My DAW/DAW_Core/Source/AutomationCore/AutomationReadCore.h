#pragma once

namespace DAW {

/**
 * AutomationReadCore — reads automation data during playback.
 *
 * When a lane is in Read mode, this core evaluates the current
 * playhead position and returns the interpolated automation value.
 * The audio engine applies this value to the target parameter.
 *
 * Thread safety: getValueAtPosition is audio-thread-safe (read-only).
 */
class AutomationReadCore
{
public:
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool isEnabled() const noexcept { return enabled_; }

private:
    bool enabled_ = false;
};

} // namespace DAW
