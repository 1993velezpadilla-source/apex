#pragma once

namespace DAW {

/**
 * SoloOverrideCore — overrides solo behavior for special cases.
 *
 * Allows forcing certain tracks/buses to bypass solo logic entirely.
 * Used by the master track (which never participates in solo) and
 * for future "solo defeat" / "solo isolate" features.
 */
class SoloOverrideCore
{
public:
    void setOverrideActive(bool active) noexcept { active_ = active; }
    bool isOverrideActive() const noexcept { return active_; }

    /** Returns true if this node should ignore all solo logic. */
    bool shouldIgnoreSolo() const noexcept { return active_; }

private:
    bool active_ = false;
};

} // namespace DAW
