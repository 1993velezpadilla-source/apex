#pragma once

namespace DAW {

/**
 * AutomationLatchCore — Latch mode automation recording.
 *
 * Once the user begins touching a control, automation is written
 * continuously until transport stops. Does NOT return to previous
 * value on release (unlike Touch mode).
 */
class AutomationLatchCore
{
public:
    void beginTouch() noexcept { latched_ = true; }
    void stopTransport() noexcept { latched_ = false; }
    bool isLatched() const noexcept { return latched_; }

    /** Returns true if automation should be written at this moment. */
    bool shouldWrite() const noexcept { return latched_; }

private:
    bool latched_ = false;
};

} // namespace DAW
