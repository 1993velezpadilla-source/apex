#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumTransitionCore — manages 80-120 ms fade transitions.
 *
 * No flicker. No layout jump. Smooth crossfade on open/close/source change.
 */
class BubblegumTransitionCore
{
public:
    static constexpr float kFadeDurationMs = 100.0f;

    void beginFadeIn() noexcept  { fadeTarget_ = 1.0f; fading_ = true; }
    void beginFadeOut() noexcept { fadeTarget_ = 0.0f; fading_ = true; }

    void tick(float deltaMs) noexcept
    {
        if (!fading_) return;

        float step = deltaMs / kFadeDurationMs;
        if (fadeTarget_ > alpha_)
            alpha_ = juce::jmin(alpha_ + step, fadeTarget_);
        else
            alpha_ = juce::jmax(alpha_ - step, fadeTarget_);

        if (std::abs(alpha_ - fadeTarget_) < 0.01f)
        {
            alpha_  = fadeTarget_;
            fading_ = false;
        }
    }

    float getAlpha() const noexcept        { return alpha_; }
    bool  isFading() const noexcept        { return fading_; }
    bool  isFullyVisible() const noexcept  { return alpha_ >= 0.99f; }
    bool  isFullyHidden() const noexcept   { return alpha_ <= 0.01f; }

    void snapTo(float a) noexcept
    {
        alpha_ = a; fadeTarget_ = a; fading_ = false;
    }

private:
    float alpha_      = 0.0f;
    float fadeTarget_  = 0.0f;
    bool  fading_      = false;
};

} // namespace DAW
