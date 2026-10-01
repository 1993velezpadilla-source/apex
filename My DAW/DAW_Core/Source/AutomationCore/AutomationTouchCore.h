#pragma once

namespace DAW {

/**
 * AutomationTouchCore — Touch mode automation recording.
 *
 * Writes automation ONLY while the user is actively touching/moving
 * a control. When the user releases, the parameter returns to
 * the previously existing automation value.
 *
 * This is the most common automation mode for mixing.
 */
class AutomationTouchCore
{
public:
    void beginTouch() noexcept { touching_ = true; }
    void endTouch() noexcept { touching_ = false; }
    bool isTouching() const noexcept { return touching_; }

    /** Returns true if automation should be written at this moment. */
    bool shouldWrite() const noexcept { return touching_; }

private:
    bool touching_ = false;
};

} // namespace DAW
