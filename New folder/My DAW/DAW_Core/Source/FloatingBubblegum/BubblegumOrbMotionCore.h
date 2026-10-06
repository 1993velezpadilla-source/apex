#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumOrbMotionCore — physically believable elastic motion for the Bubblegum Orb.
 *
 * States: Idle (breathing), Hover, Tap (3-phase squish), Active (glow pulse), Drag.
 * All motion uses easing curves — no linear interpolation.
 */
class BubblegumOrbMotionCore
{
public:
    enum class State { Idle, Hover, TapCompress, TapOvershoot, TapSettle, Active, Drag };

    // ── Timing constants ─────────────────────────────────────────────────
    static constexpr float kBreathPeriodMs   = 2600.f;
    static constexpr float kBreathMinScale   = 1.0f;
    static constexpr float kBreathMaxScale   = 1.035f;

    static constexpr float kHoverScale       = 1.08f;
    static constexpr float kHoverDurationMs  = 140.f;

    static constexpr float kTapCompressScale = 0.92f;
    static constexpr float kTapCompressMs    = 60.f;
    static constexpr float kTapOvershootScale = 1.06f;
    static constexpr float kTapOvershootMs   = 80.f;
    static constexpr float kTapSettleMs      = 120.f;

    static constexpr float kActivePulsePeriodMs = 1600.f;
    static constexpr float kActiveGlowMin    = 0.25f;
    static constexpr float kActiveGlowMax    = 0.55f;

    static constexpr float kDragSnapThreshold = 24.f;
    static constexpr float kDragSnapMs       = 180.f;

    // ── Current output values ────────────────────────────────────────────
    float getScale() const noexcept       { return scale_; }
    float getGlowAlpha() const noexcept   { return glowAlpha_; }
    State getState() const noexcept       { return state_; }

    // ── State transitions ────────────────────────────────────────────────

    void setIdle()
    {
        state_ = State::Idle;
        phase_ = 0.f;
    }

    void setHover()
    {
        if (state_ == State::Drag) return;
        state_ = State::Hover;
        phase_ = 0.f;
        scaleFrom_ = scale_;
    }

    void setActive(bool active)
    {
        if (active)
        {
            state_ = State::Active;
            phase_ = 0.f;
        }
        else
        {
            setIdle();
        }
    }

    void triggerTap()
    {
        state_ = State::TapCompress;
        phase_ = 0.f;
        scaleFrom_ = scale_;
    }

    void beginDrag() { state_ = State::Drag; }
    void endDrag()   { state_ = State::Idle; phase_ = 0.f; }

    // ── Tick (call at ~60 Hz, deltaMs ≈ 16.6) ───────────────────────────

    void tick(float deltaMs)
    {
        phase_ += deltaMs;

        switch (state_)
        {
        case State::Idle:
            tickIdle();
            break;
        case State::Hover:
            tickHover();
            break;
        case State::TapCompress:
            tickTapCompress();
            break;
        case State::TapOvershoot:
            tickTapOvershoot();
            break;
        case State::TapSettle:
            tickTapSettle();
            break;
        case State::Active:
            tickActive();
            break;
        case State::Drag:
            break; // scale stays at current value during drag
        }
    }

private:
    State state_ = State::Idle;
    float phase_ = 0.f;
    float scale_ = 1.0f;
    float scaleFrom_ = 1.0f;
    float glowAlpha_ = 0.15f;

    // ── Easing helpers ───────────────────────────────────────────────────

    static float easeInOutSine(float t)
    {
        return 0.5f * (1.0f - std::cos(t * juce::MathConstants<float>::pi));
    }

    // Approximation of cubic-bezier(0.22, 1, 0.36, 1) — fast decel
    static float easeOutExpo(float t)
    {
        return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    }

    static float easeOutBack(float t)
    {
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
    }

    // ── State tick functions ─────────────────────────────────────────────

    void tickIdle()
    {
        float t = std::fmod(phase_, kBreathPeriodMs) / kBreathPeriodMs;
        float breath = easeInOutSine(t);
        scale_ = kBreathMinScale + breath * (kBreathMaxScale - kBreathMinScale);
        glowAlpha_ = 0.15f;
    }

    void tickHover()
    {
        float t = juce::jmin(phase_ / kHoverDurationMs, 1.0f);
        float e = easeOutExpo(t);
        scale_ = scaleFrom_ + e * (kHoverScale - scaleFrom_);
        glowAlpha_ = 0.20f + e * 0.10f;
    }

    void tickTapCompress()
    {
        float t = juce::jmin(phase_ / kTapCompressMs, 1.0f);
        float e = easeInOutSine(t);
        scale_ = scaleFrom_ + e * (kTapCompressScale - scaleFrom_);
        if (t >= 1.0f)
        {
            state_ = State::TapOvershoot;
            phase_ = 0.f;
            scaleFrom_ = scale_;
        }
    }

    void tickTapOvershoot()
    {
        float t = juce::jmin(phase_ / kTapOvershootMs, 1.0f);
        float e = easeOutBack(t);
        scale_ = scaleFrom_ + e * (kTapOvershootScale - scaleFrom_);
        if (t >= 1.0f)
        {
            state_ = State::TapSettle;
            phase_ = 0.f;
            scaleFrom_ = scale_;
        }
    }

    void tickTapSettle()
    {
        float t = juce::jmin(phase_ / kTapSettleMs, 1.0f);
        float e = easeInOutSine(t);
        scale_ = scaleFrom_ + e * (1.0f - scaleFrom_);
        if (t >= 1.0f)
            setIdle();
    }

    void tickActive()
    {
        // Breathing base
        float breathT = std::fmod(phase_, kBreathPeriodMs) / kBreathPeriodMs;
        float breath = easeInOutSine(breathT);
        scale_ = kBreathMinScale + breath * (kBreathMaxScale - kBreathMinScale);

        // Glow pulse
        float glowT = std::fmod(phase_, kActivePulsePeriodMs) / kActivePulsePeriodMs;
        float glow = easeInOutSine(glowT);
        glowAlpha_ = kActiveGlowMin + glow * (kActiveGlowMax - kActiveGlowMin);
    }
};

} // namespace DAW
