#pragma once
#include <JuceHeader.h>
#include "BubblegumOrbMotionCore.h"
#include "BubblegumOrbDripCore.h"
#include "BubblegumOrbAudioEnvelopeCore.h"
#include "BubblegumOrbAudioReactiveCore.h"
#include "BubblegumOrbReactiveDripCore.h"
#include "../Bubblegum/BubblegumV2System.h"
#include "../TrackCore/Track.h"
#include "../UICore/CursorThemeCore.h"

namespace DAW {

/**
 * BubblegumOrbComponent — the floating Bubblegum launcher orb.
 *
 * Always visible on screen. Acts as the primary Bubblegum control.
 * Tap: if mixer closed → open mixer + activate; if mixer open → toggle panel.
 * Renders with elastic motion, liquid drip edge, and subtle audio reactivity.
 *
 * Single source of truth: BubblegumV2System::panel.toggle()
 */
class BubblegumOrbComponent : public juce::Component,
                              private juce::Timer
{
public:
    static constexpr int kOrbSize = 48;

    // ── Callbacks (wired by MainComponent) ───────────────────────────────
    std::function<void()> onClicked;           // toggle Bubblegum via single state
    std::function<float()> getSourceTrackLevel; // returns source track peak [0..1]

    BubblegumOrbComponent()
    {
        setSize(kOrbSize + 16, kOrbSize + 16); // extra space for glow
        setAlwaysOnTop(true);
        startTimerHz(60);
    }

    void setBubblegumSystem(BubblegumV2System* sys) { bgV2_ = sys; }

    // ── Component overrides ──────────────────────────────────────────────

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float baseR = (float)kOrbSize * 0.5f;

        bool active = bgV2_ && bgV2_->isActive();
        float scale = motion_.getScale() + reactiveScale_;
        float r = baseR * scale;

        // ── Outer glow / aura ────────────────────────────────────────
        float outerGlow = motion_.getGlowAlpha() + reactiveOuterGlow_;
        if (outerGlow > 0.02f)
        {
            juce::ColourGradient aura(
                juce::Colour(0xFFFF7DB8).withAlpha(outerGlow),
                cx, cy,
                juce::Colour(0xFFFF7DB8).withAlpha(0.0f),
                cx, cy - r * 1.6f, true);
            g.setGradientFill(aura);
            g.fillEllipse(cx - r * 1.5f, cy - r * 1.5f, r * 3.f, r * 3.f);
        }

        // ── Drip edge path ───────────────────────────────────────────
        auto dripState = drip_.compute();
        juce::Path orbPath;
        for (int i = 0; i < BubblegumOrbDripCore::kNumPoints; ++i)
        {
            float angle = (float)i / (float)BubblegumOrbDripCore::kNumPoints
                          * juce::MathConstants<float>::twoPi;
            float dr = r + dripState.displacements[i];
            float px = cx + std::cos(angle) * dr;
            float py = cy + std::sin(angle) * dr;
            if (i == 0) orbPath.startNewSubPath(px, py);
            else        orbPath.lineTo(px, py);
        }
        orbPath.closeSubPath();

        // ── Drop shadow ──────────────────────────────────────────────
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        {
            auto shadowPath = orbPath;
            shadowPath.applyTransform(juce::AffineTransform::translation(0.f, 3.f));
            g.fillPath(shadowPath);
        }

        // ── Gel gradient body ────────────────────────────────────────
        juce::ColourGradient bodyGrad(
            active ? juce::Colour(0xFFFF7DB8) : juce::Colour(0xFFE06BA0),
            cx, cy - r * 0.6f,
            active ? juce::Colour(0xFFC44E88) : juce::Colour(0xFF9E3A6E),
            cx, cy + r * 0.6f, false);
        g.setGradientFill(bodyGrad);
        g.fillPath(orbPath);

        // ── Inner light (reactive) ───────────────────────────────────
        float innerAlpha = 0.10f + reactiveInnerLight_;
        juce::ColourGradient innerGrad(
            juce::Colours::white.withAlpha(innerAlpha),
            cx, cy - r * 0.3f,
            juce::Colours::transparentWhite,
            cx, cy + r * 0.2f, true);
        g.setGradientFill(innerGrad);
        g.fillPath(orbPath);

        // ── Specular highlight ───────────────────────────────────────
        g.setColour(juce::Colours::white.withAlpha(active ? 0.30f : 0.18f));
        g.fillEllipse(cx - r * 0.3f, cy - r * 0.65f,
                      r * 0.55f, r * 0.35f);

        // ── Rim ──────────────────────────────────────────────────────
        g.setColour(juce::Colour(0xFFC44E88).withAlpha(active ? 0.7f : 0.4f));
        g.strokePath(orbPath, juce::PathStrokeType(1.2f));

        // ── "B" label ────────────────────────────────────────────────
        g.setColour(juce::Colours::white.withAlpha(active ? 0.95f : 0.6f));
        g.setFont(juce::Font(14.f, juce::Font::bold));
        g.drawText("B", bounds, juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        if (motion_.getState() != BubblegumOrbMotionCore::State::Active)
            motion_.setHover();
        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::PointingHandCursor));
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        bool active = bgV2_ && bgV2_->isActive();
        if (active)
            motion_.setActive(true);
        else
            motion_.setIdle();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        motion_.triggerTap();
        dragStartPos_ = e.getScreenPosition();
        dragging_ = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        auto delta = e.getScreenPosition() - dragStartPos_;
        if (!dragging_ && delta.getDistanceFromOrigin() > 6)
        {
            dragging_ = true;
            motion_.beginDrag();
        }

        if (dragging_)
        {
            auto parent = getParentComponent();
            if (!parent) return;

            auto newPos = getBounds().getCentre() + juce::Point<int>(delta.x, delta.y);
            dragStartPos_ = e.getScreenPosition();

            // Magnetic edge snap
            auto pb = parent->getLocalBounds();
            int threshold = (int)BubblegumOrbMotionCore::kDragSnapThreshold;
            if (newPos.x < threshold) newPos.x = 8;
            if (newPos.x > pb.getWidth() - threshold) newPos.x = pb.getWidth() - 8;
            if (newPos.y < threshold) newPos.y = 8;
            if (newPos.y > pb.getHeight() - threshold) newPos.y = pb.getHeight() - 8;

            setCentrePosition(newPos);
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging_)
        {
            motion_.endDrag();
            dragging_ = false;
            DBG("[BubblegumOrb::mouseUp] drag ended (not a tap)");
        }
        else
        {
            // Tap — toggle via single source of truth
            DBG("[BubblegumOrb::mouseUp] STEP 1: orb click received"
                " bgV2_=" + juce::String(bgV2_ != nullptr ? "OK" : "NULL")
                + " wasActive=" + juce::String(bgV2_ && bgV2_->isActive() ? 1 : 0)
                + " onClicked=" + juce::String(onClicked ? "YES" : "NULL"));
            if (onClicked)
            {
                DBG("[BubblegumOrb::mouseUp] STEP 2: calling onClicked()");
                onClicked();
                DBG("[BubblegumOrb::mouseUp] STEP 9: onClicked() returned"
                    " isActive now=" + juce::String(bgV2_ && bgV2_->isActive() ? 1 : 0));
            }
            else
            {
                DBG("[BubblegumOrb::mouseUp] ERROR: onClicked callback is NULL!");
            }
        }

        bool active = bgV2_ && bgV2_->isActive();
        motion_.setActive(active);
    }

private:
    BubblegumV2System* bgV2_ = nullptr;
    BubblegumOrbMotionCore motion_;
    BubblegumOrbDripCore drip_;
    BubblegumOrbAudioEnvelopeCore audioEnvelope_;
    BubblegumOrbAudioReactiveCore audioReactive_;
    BubblegumOrbReactiveDripCore reactiveDrip_;

    float reactiveOuterGlow_  = 0.f;
    float reactiveInnerLight_ = 0.f;
    float reactiveScale_      = 0.f;

    juce::Point<int> dragStartPos_;
    bool dragging_ = false;

    void timerCallback() override
    {
        constexpr float dt = 1000.f / 60.f; // ~16.6ms

        // Motion physics
        motion_.tick(dt);
        drip_.tick(dt);

        // Audio reactivity (only when Bubblegum active + source exists)
        bool active = bgV2_ && bgV2_->isActive();
        bool hasSource = bgV2_ && bgV2_->sourceSync.getSourceTrackId().isNotEmpty();

        if (active && hasSource && audioReactive_.isEnabled())
        {
            float rawLevel = 0.f;
            if (getSourceTrackLevel)
                rawLevel = getSourceTrackLevel();

            audioEnvelope_.tick(dt);
            audioEnvelope_.feedLevel(rawLevel);

            float energy = audioEnvelope_.getEnergy();
            auto rv = audioReactive_.compute(energy);

            reactiveOuterGlow_  = rv.outerGlowDelta;
            reactiveInnerLight_ = rv.innerLightDelta;
            reactiveScale_      = rv.scaleDelta;
            drip_.setIntensity(reactiveDrip_.computeDripIntensity(energy, rv.dripDelta));
        }
        else
        {
            reactiveOuterGlow_  = 0.f;
            reactiveInnerLight_ = 0.f;
            reactiveScale_      = 0.f;
            drip_.setIntensity(BubblegumOrbReactiveDripCore::kBaseDripIntensity);
            audioEnvelope_.reset();
        }

        // Sync motion state with active
        if (active && motion_.getState() == BubblegumOrbMotionCore::State::Idle)
            motion_.setActive(true);
        else if (!active && motion_.getState() == BubblegumOrbMotionCore::State::Active)
            motion_.setIdle();

        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumOrbComponent)
};

} // namespace DAW
