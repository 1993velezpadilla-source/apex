#pragma once
#include <JuceHeader.h>

namespace DAW {

// =====================================================================
// BubblegumCableOsDragFadeCore
//
// Manages smooth opacity fade-out / fade-in for Bubblegum cables when
// the mixer is being dragged as a floating OS window.
//
// Cross-window compositor sync is physically impossible during OS modal
// drag loops, so cables must gracefully disappear during the drag and
// snap back when the drag settles. This core provides the smooth
// opacity envelope for that transition.
//
// Usage:
//   - Call triggerFadeOut() when OS-level window drag is detected
//   - Call tick(dt) every timer frame
//   - Read getOpacity() in paint; skip painting when fully faded out
//   - isFullyHidden() returns true when opacity has reached zero
// =====================================================================
class BubblegumCableOsDragFadeCore
{
public:
    /** Trigger a smooth fade-out (OS window drag detected). */
    void triggerFadeOut() noexcept
    {
        fading_ = true;
        fadingIn_ = false;
    }

    /** Trigger a smooth fade-in (drag settled, restore cables). */
    void triggerFadeIn() noexcept
    {
        if (fading_ || opacity_ < 1.f)
        {
            fadingIn_ = true;
            fading_ = false;
        }
    }

    /** Advance the fade envelope. Call once per timer tick.
     *  @param dt  delta time in seconds */
    void tick(double dt) noexcept
    {
        if (fading_)
        {
            // Fast fade-out: ~80ms to fully hidden
            opacity_ -= (float)(dt / kFadeOutSec);
            if (opacity_ <= 0.f)
            {
                opacity_ = 0.f;
                fading_ = false;
            }
        }
        else if (fadingIn_)
        {
            // Slightly slower fade-in: ~150ms for a premium snap-back feel
            opacity_ += (float)(dt / kFadeInSec);
            if (opacity_ >= 1.f)
            {
                opacity_ = 1.f;
                fadingIn_ = false;
            }
        }
    }

    /** Current cable opacity [0..1]. Apply in paint via g.setOpacity(). */
    float getOpacity() const noexcept { return opacity_; }

    /** True when cables are completely invisible (skip paint entirely). */
    bool isFullyHidden() const noexcept { return opacity_ <= 0.f; }

    /** True when a fade transition is in progress. */
    bool isTransitioning() const noexcept { return fading_ || fadingIn_; }

    void reset() noexcept
    {
        opacity_ = 1.f;
        fading_ = false;
        fadingIn_ = false;
    }

private:
    static constexpr double kFadeOutSec = 0.08;
    static constexpr double kFadeInSec  = 0.15;

    float opacity_ = 1.f;
    bool  fading_  = false;
    bool  fadingIn_ = false;
};

} // namespace DAW
