#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"

namespace DAW {

/**
 * BubbleMergeOverlay — transparent animation + preview layer above all bubbles.
 *
 * Two modes:
 *
 *   PREVIEW MODE (while holding drag over another bubble)
 *     A semi-transparent ghost MasterBubble pulses at the midpoint of the two
 *     overlapping bubbles. Text: "Drop to merge".
 *     Cleared by endMergePreview() when the user moves away or drops.
 *
 *   ANIMATION MODE (on drop — triggered by beginMerge / beginMergeBoth)
 *     One or two shrinking circles travel toward the MasterBubble center.
 *     onComplete fires when all animations finish.
 */
class BubbleMergeOverlay : public juce::Component,
                            private juce::Timer
{
public:
    BubbleMergeOverlay() { setInterceptsMouseClicks(false, false); setVisible(false); }

    // ── Preview ───────────────────────────────────────────────────────────────

    /** Show ghost MasterBubble while holding drag over another bubble. */
    void beginMergePreview(juce::Point<float> midpoint, bool targetIsMaster)
    {
        previewActive_       = true;
        previewCenter_       = midpoint;
        previewTargetMaster_ = targetIsMaster;
        previewPulse_        = 0.f;
        setVisible(true);
        if (!isTimerRunning()) startTimerHz(60);
        repaint();
    }

    /** Call when the drag moves away from merge zone or drops. */
    void endMergePreview()
    {
        previewActive_ = false;
        if (!isAnyAnimActive()) { stopTimer(); setVisible(false); }
        repaint();
    }

    // ── Merge animation ───────────────────────────────────────────────────────

    /** Animate one bubble being absorbed. */
    void beginMerge(juce::Point<float>    srcCenter,
                    float                 srcRadius,
                    juce::Colour          srcColor,
                    juce::Point<float>    dstCenter,
                    std::function<void()> onComplete)
    {
        anim_[0] = { srcCenter, dstCenter, srcRadius, srcColor, 0.f, true };
        anim_[1].active = false;
        onComplete_     = std::move(onComplete);
        pending_        = 1;
        setVisible(true);
        if (!isTimerRunning()) startTimerHz(60);
    }

    /** Animate two bubbles being absorbed simultaneously. */
    void beginMergeBoth(juce::Point<float>    src1Center, float src1Radius, juce::Colour src1Color,
                         juce::Point<float>    src2Center, float src2Radius, juce::Colour src2Color,
                         juce::Point<float>    dstCenter,
                         std::function<void()> onComplete)
    {
        anim_[0] = { src1Center, dstCenter, src1Radius, src1Color, 0.f, true };
        anim_[1] = { src2Center, dstCenter, src2Radius, src2Color, 0.f, true };
        onComplete_ = std::move(onComplete);
        pending_    = 2;
        setVisible(true);
        if (!isTimerRunning()) startTimerHz(60);
    }

    void paint(juce::Graphics& g) override
    {
        if (previewActive_)             drawGhostPreview(g);
        for (auto& a : anim_)
            if (a.active)               drawBubbleAnim(g, a);
    }

    bool hitTest(int, int) override { return false; }

private:
    // ── State ─────────────────────────────────────────────────────────────────

    struct AnimState
    {
        juce::Point<float> src, dst;
        float  radius   = 28.f;
        juce::Colour color;
        float  progress = 0.f;
        bool   active   = false;
    };

    AnimState              anim_[2];
    int                    pending_             = 0;
    std::function<void()>  onComplete_;

    bool  previewActive_       = false;
    juce::Point<float> previewCenter_;
    float previewPulse_        = 0.f;
    bool  previewTargetMaster_ = false;

    bool isAnyAnimActive() const { return anim_[0].active || anim_[1].active; }

    // ── Timer ─────────────────────────────────────────────────────────────────

    void timerCallback() override
    {
        bool keepRunning = previewActive_;

        for (auto& a : anim_)
        {
            if (!a.active) continue;
            a.progress += 0.055f;
            if (a.progress >= 1.f)
            {
                a.progress = 1.f;
                a.active   = false;
                --pending_;
                if (pending_ <= 0 && onComplete_)
                {
                    pending_ = 0;
                    auto cb  = std::move(onComplete_);
                    cb();  // may show/hide things
                }
            }
            else { keepRunning = true; }
        }

        if (previewActive_)
        {
            previewPulse_ = std::fmod(previewPulse_ + 0.022f, 1.f);
            keepRunning = true;
        }

        repaint();

        if (!keepRunning && !isAnyAnimActive())
        {
            stopTimer();
            if (!previewActive_) setVisible(false);
        }
    }

    // ── Drawing ───────────────────────────────────────────────────────────────

    void drawBubbleAnim(juce::Graphics& g, const AnimState& a)
    {
        float t     = a.progress;
        float ease  = t * t;
        float scale = 1.f - ease;
        float alpha = juce::jmax(0.f, 1.f - t * 1.15f);

        juce::Point<float> center(a.src.x + (a.dst.x - a.src.x) * ease,
                                   a.src.y + (a.dst.y - a.src.y) * ease);
        float r = a.radius * scale;
        if (r < 0.5f) return;

        auto bub = juce::Rectangle<float>(center.x - r, center.y - r, r * 2.f, r * 2.f);

        // Shadow
        g.setColour(juce::Colours::black.withAlpha(alpha * 0.35f));
        g.fillEllipse(bub.translated(0.f, 3.f));

        // Body
        g.setColour(a.color.withAlpha(alpha));
        g.fillEllipse(bub);

        // Rim
        g.setColour(juce::Colours::white.withAlpha(alpha * 0.3f));
        g.drawEllipse(bub.reduced(0.5f), 1.f);

        // Spark trail
        float dx = a.dst.x - center.x, dy = a.dst.y - center.y;
        float dirD = std::sqrt(dx * dx + dy * dy);
        if (dirD > 0.001f && r > 4.f)
        {
            float nx = dx / dirD, ny = dy / dirD;
            g.setColour(a.color.withAlpha(alpha * 0.5f));
            for (int i = 1; i <= 3; ++i)
            {
                float dotR = r * 0.2f * (1.f - (float)i * 0.25f);
                float d2   = (float)i * r * 0.18f;
                auto  dot  = juce::Rectangle<float>(center.x + nx * d2 - dotR,
                                                     center.y + ny * d2 - dotR,
                                                     dotR * 2.f, dotR * 2.f);
                if (dotR > 0.5f) g.fillEllipse(dot);
            }
        }
    }

    void drawGhostPreview(juce::Graphics& g)
    {
        auto& t = Theme::getInstance();

        // Pulsing alpha — sin wave so it breathes in and out
        float pulse      = std::sin(previewPulse_ * juce::MathConstants<float>::twoPi);
        float baseAlpha  = previewTargetMaster_ ? 0.45f : 0.50f;
        float alpha      = baseAlpha + pulse * 0.12f;
        float ringExpand = 4.f + pulse * 6.f;

        float ghostR = 28.f; // (MasterBubble::kBubbleSize / 2 - 4)
        auto  ghost  = juce::Rectangle<float>(previewCenter_.x - ghostR,
                                               previewCenter_.y - ghostR,
                                               ghostR * 2.f, ghostR * 2.f);

        // Shadow
        g.setColour(juce::Colours::black.withAlpha(alpha * 0.4f));
        g.fillEllipse(ghost.translated(0.f, 4.f).expanded(2.f));

        // Chrome body
        juce::ColourGradient grad(juce::Colour(0xff555560).withAlpha(alpha),
                                   ghost.getCentreX(), ghost.getY(),
                                   juce::Colour(0xff282830).withAlpha(alpha),
                                   ghost.getCentreX(), ghost.getBottom(), false);
        g.setGradientFill(grad);
        g.fillEllipse(ghost);

        // Accent ring (pulsing)
        g.setColour(t.colors.accent.withAlpha(alpha * 0.85f));
        g.drawEllipse(ghost.expanded(ringExpand), 2.5f);
        g.setColour(t.colors.accent.withAlpha(alpha * 0.15f));
        g.fillEllipse(ghost.expanded(ringExpand));

        // Inner highlight
        g.setColour(juce::Colours::white.withAlpha(alpha * 0.12f));
        g.drawEllipse(ghost.reduced(1.f), 1.f);

        // Hub icon — 2×2 dot grid
        float cx = ghost.getCentreX(), cy = ghost.getCentreY();
        float off = ghostR * 0.35f, dotR = 3.f;
        g.setColour(t.colors.accent.withAlpha(alpha));
        g.fillEllipse(cx - off - dotR, cy - off - dotR, dotR * 2.f, dotR * 2.f);
        g.fillEllipse(cx + off - dotR, cy - off - dotR, dotR * 2.f, dotR * 2.f);
        g.fillEllipse(cx - off - dotR, cy + off - dotR, dotR * 2.f, dotR * 2.f);
        g.fillEllipse(cx + off - dotR, cy + off - dotR, dotR * 2.f, dotR * 2.f);

        // "Drop to merge" label
        g.setFont(juce::Font(10.f));
        g.setColour(juce::Colours::white.withAlpha(alpha * 0.9f));
        g.drawText("Drop to merge",
                   juce::Rectangle<float>(previewCenter_.x - 52.f,
                                          previewCenter_.y + ghostR + ringExpand + 4.f,
                                          104.f, 16.f),
                   juce::Justification::centred);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubbleMergeOverlay)
};

} // namespace DAW
