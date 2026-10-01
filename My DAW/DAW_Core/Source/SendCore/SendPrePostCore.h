#pragma once

namespace DAW {

/**
 * SendPrePostCore — manages the pre/post fader mode for a send.
 *
 * Pre-fader: send level is independent of source track fader.
 *   Use cases: headphone mixes, parallel FX while fader moves.
 *
 * Post-fader: send follows source track fader.
 *   Use cases: typical reverb/delay workflow.
 */
class SendPrePostCore
{
public:
    enum class Mode { PreFader, PostFader };

    void setMode(Mode m) noexcept { mode_ = m; }
    Mode getMode() const noexcept { return mode_; }

    bool isPreFader() const noexcept { return mode_ == Mode::PreFader; }
    bool isPostFader() const noexcept { return mode_ == Mode::PostFader; }

private:
    Mode mode_ = Mode::PostFader;
};

} // namespace DAW
