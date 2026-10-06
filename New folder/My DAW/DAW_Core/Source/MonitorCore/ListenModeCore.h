#pragma once

namespace DAW {

/**
 * ListenModeCore — selects the active listening mode for the monitor system.
 *
 * Modes:
 *   Normal — monitor receives master bus output
 *   AFL    — monitor receives after-fader signal from a selected source
 *   PFL    — monitor receives pre-fader signal from a selected source
 */
class ListenModeCore
{
public:
    enum class Mode { Normal, AFL, PFL };

    void setMode(Mode m) noexcept { mode_ = m; }
    Mode getMode() const noexcept { return mode_; }

    bool isNormal() const noexcept { return mode_ == Mode::Normal; }
    bool isAFL() const noexcept { return mode_ == Mode::AFL; }
    bool isPFL() const noexcept { return mode_ == Mode::PFL; }

private:
    Mode mode_ = Mode::Normal;
};

} // namespace DAW
