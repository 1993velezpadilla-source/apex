#pragma once

namespace DAW {

/**
 * BubblegumPanelCore — manages the Bubblegum panel open/close state.
 *
 * Pure state toggle. No side-effects.
 */
class BubblegumPanelCore
{
public:
    void open() noexcept  { active_ = true; }
    void close() noexcept { active_ = false; }
    void toggle() noexcept { active_ = !active_; }
    bool isOpen() const noexcept { return active_; }

private:
    bool active_ = false;
};

} // namespace DAW
