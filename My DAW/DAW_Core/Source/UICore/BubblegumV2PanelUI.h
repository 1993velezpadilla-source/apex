#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../Bubblegum/BubblegumV2System.h"
#include "CursorThemeCore.h"
#include "ApexPresentationClock.h"

namespace DAW {

// ── BubblegumV2PanelUI ──────────────────────────────────────────────────────
// Floating send-only routing panel for the currently selected source track.
// Premium Bubblegum bubble material: dark translucent, pink glow, large radius.
// Feels like an extension of the Bubblegum orb — not a utility dialog.
class BubblegumV2PanelUI : public juce::Component,
                           public ApexPresentationClock::TickReceiver
{
public:
    static constexpr int kPanelW    = 260;   // wider to give right-side elements room
    static constexpr int kPanelH    = 380;
    static constexpr int kHeaderH   = 52;
    static constexpr int kRowH      = 34;
    static constexpr int kFullRowH  = 26;
    static constexpr int kFullSourceHeaderH = 28;
    static constexpr int kFullPanelGap = 10;
    static constexpr float kCircleR = 8.f;   // send-knob radius
    static constexpr float kRingR   = 11.f;  // send-knob ring radius
    static constexpr float kDotR    = 7.f;   // toggle dot radius (larger, prominent)
    static constexpr float kCorner  = 18.f;
    static constexpr float kCloseBtnSize = 30.f;
    static constexpr float kEyeBtnSize = 30.f;

    /** Callback: panel's X button was clicked — MainComponent closes Bubblegum. */
    std::function<void()> onCloseRequested;
    /** Callback: a send was toggled/changed — MainComponent refreshes feedback. */
    std::function<void()> onSendChanged;
    /** Callback: returns true when the master track is the currently selected
     *  track — the master row then shows the same selection language as a
     *  regular selected track. */
    std::function<bool()> onIsMasterSelected;

    BubblegumV2PanelUI()
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, true);
        viewport_.setScrollBarsShown(true, false);
        viewport_.setScrollBarThickness(6);
        viewport_.setViewedComponent(&content_, false);
        addAndMakeVisible(viewport_);
        ApexPresentationClock::instance().addReceiver(this);
        DBG("[BubblegumV2PanelUI] created (floating bubble style)");
    }

    ~BubblegumV2PanelUI() override
    {
        if (presentationUpdateActive_)
            ApexPresentationClock::instance().releaseContinuousUpdate(this);
        ApexPresentationClock::instance().removeReceiver(this);
    }

    void bind(BubblegumV2System* sys, TrackManager* tracks)
    {
        bgV2_   = sys;
        tracks_ = tracks;
        DBG("[BubblegumV2PanelUI] bound bgV2=" + juce::String(sys != nullptr ? "OK" : "NULL")
            + " tracks=" + juce::String(tracks != nullptr ? "OK" : "NULL"));
    }

    /** Show with open animation near anchor point. */
    void showNear(juce::Point<int> anchorCentre)
    {
        fullScreenMode_ = false;
        // Position above-left of anchor
        int x = anchorCentre.x - kPanelW - 16;
        int y = anchorCentre.y - kPanelH + 20;
        // Clamp to screen
        if (auto* parent = getParentComponent())
        {
            x = juce::jlimit(8, parent->getWidth() - kPanelW - 8, x);
            y = juce::jlimit(8, parent->getHeight() - kPanelH - 8, y);
        }
        setBounds(x, y, kPanelW, kPanelH);
        setVisible(true);
        toFront(true);
        // Start open animation
        animPhase_ = 0.f;
        animTarget_ = 1.f;
        requestPresentationUpdate();
        refresh();
        repaint();
        DBG("[BubblegumV2PanelUI] showNear anchor=" + anchorCentre.toString()
            + " bounds=" + getBounds().toString());
    }

    /** Hide with close animation. */
    void hideAnimated()
    {
        fullScreenMode_ = false;
        animTarget_ = 0.f;
        requestPresentationUpdate();
    }

    /** Refresh panel content from the current bgV2 state. */
    void refresh()
    {
        if (!bgV2_ || !tracks_) return;

        if (fullScreenMode_)
        {
            const int currentTrackCount = getFullTrackSignature();
            bool rebuiltRows = false;
            if (currentTrackCount != fullTrackCount_ || fullRows_.empty())
            {
                fullTrackCount_ = currentTrackCount;
                rebuildFullRows();
                lastSendStateHash_ = 0; // force repaint after rebuild
                rebuiltRows = true;
            }

            const int viewportW = viewport_.getWidth();
            const int viewportH = viewport_.getHeight();
            const bool viewportChanged = viewportW != lastFullViewportW_ || viewportH != lastFullViewportH_;
            if (rebuiltRows || viewportChanged || content_.getWidth() <= 0 || content_.getHeight() <= 0)
            {
                lastFullViewportW_ = viewportW;
                lastFullViewportH_ = viewportH;
                updateContentSize();
                repaint();
            }

            const std::size_t h = computeSendStateHash();
            if (h != lastSendStateHash_) { lastSendStateHash_ = h; content_.repaint(); }
            return;
        }

        auto newSourceId = bgV2_->sourceSync.getSourceTrackId();
        auto& newTargets = bgV2_->targetList.getTargets();

        bool needsRebuild = (newSourceId != sourceId_)
                         || (newTargets.size() != targets_.size());

        if (needsRebuild)
        {
            sourceId_ = newSourceId;
            targets_  = newTargets;

            targetNames_.clear();
            targetColors_.clear();
            isMasterFlags_.clear();
            for (auto& tid : targets_)
            {
                // Master track lives outside the regular tracks_ array — look it up separately
                auto* t = tracks_->getTrack(tid);
                if (t == nullptr && tracks_->hasMasterTrack() && tracks_->getMasterTrack()->getID() == tid)
                    t = tracks_->getMasterTrack();

                targetNames_.push_back(t ? t->getName() : tid);
                targetColors_.push_back(t ? t->getColor() : juce::Colours::grey);
                isMasterFlags_.push_back(t ? t->isMaster() : false);
            }

            auto* srcTrack = tracks_->getTrack(sourceId_);
            sourceName_ = srcTrack ? srcTrack->getName() : sourceId_;

            updateContentSize();
            lastSendStateHash_ = 0; // force repaint after rebuild
            repaint();
        }

        // Master-selected state: drives the master-row selection highlight.
        const bool masterSel = onIsMasterSelected ? onIsMasterSelected() : false;
        if (masterSel != masterSelected_)
        {
            masterSelected_ = masterSel;
            content_.repaint();
        }

        // Only repaint content when send/SC state actually changed
        const std::size_t h = computeSendStateHash();
        if (h != lastSendStateHash_) { lastSendStateHash_ = h; content_.repaint(); }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(2); // inset for shadow
        b.removeFromTop(kHeaderH);
        viewport_.setBounds(b);
        updateContentSize();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // ── Soft drop shadow ──
        for (int i = 3; i >= 1; --i)
        {
            float off = (float)i * 4.f;
            g.setColour(juce::Colours::black.withAlpha(0.08f * (4 - i)));
            g.fillRoundedRectangle(b.reduced(-off * 0.3f).translated(0.f, off * 0.5f), kCorner + off);
        }

        auto inner = b.reduced(2.f); // inset panel area

        // ── Layer 1: dark translucent base ──
        g.setColour(juce::Colour(0xF00F0F14));
        g.fillRoundedRectangle(inner, kCorner);

        // ── Layer 2: subtle vertical gradient ──
        juce::ColourGradient bodyGrad(juce::Colour(0x18FFFFFF), inner.getCentreX(), inner.getY(),
                                      juce::Colour(0x05000000), inner.getCentreX(), inner.getBottom(), false);
        g.setGradientFill(bodyGrad);
        g.fillRoundedRectangle(inner, kCorner);

        // ── Layer 3: inner pink glow ──
        juce::ColourGradient pinkGlow(juce::Colour(0x15FF4FA3), inner.getCentreX(), inner.getY(),
                                      juce::Colour(0x00FF4FA3), inner.getCentreX(), inner.getY() + inner.getHeight() * 0.4f, false);
        g.setGradientFill(pinkGlow);
        g.fillRoundedRectangle(inner, kCorner);

        // ── Soft pink outline glow (not hard border) ──
        g.setColour(juce::Colour(0x40FF4FA3));
        g.drawRoundedRectangle(inner, kCorner, 1.2f);
        g.setColour(juce::Colour(0x18FF7DB8));
        g.drawRoundedRectangle(inner.reduced(1.f), kCorner - 1.f, 0.8f);

        // ── Top-left specular highlight ──
        {
            juce::Path spec;
            spec.addRoundedRectangle(inner.getX() + 8.f, inner.getY() + 2.f,
                                     inner.getWidth() * 0.5f, 14.f, 7.f);
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillPath(spec);
        }

        // ── Header ──
        auto header = inner.removeFromTop((float)kHeaderH);

        // Header gradient strip (clipped to header bounds to prevent pink bleed into rows)
        {
            g.saveState();
            g.reduceClipRegion(header.toNearestIntEdges());
            juce::ColourGradient hdrGrad(juce::Colour(0x30FF4FA3), header.getX(), header.getY(),
                                         juce::Colour(0x08FF2D8F), header.getX(), header.getBottom(), false);
            g.setGradientFill(hdrGrad);
            g.fillRoundedRectangle(juce::Rectangle<float>(header.getX(), header.getY(),
                                   header.getWidth(), header.getHeight() + kCorner), kCorner);
            g.restoreState();
        }

        // ── Fullscreen eye button — premium gel orb style ──
        {
            auto eb = eyeBtnBounds();
            bool hov = eb.contains(getMouseXYRelative().toFloat());
            float cx = eb.getCentreX();
            float cy = eb.getCentreY();
            float s = eyeBtnPressed_ ? 0.92f : hov ? 1.06f : 1.0f;
            float hw = eb.getWidth() * 0.5f * s;
            float hh = eb.getHeight() * 0.5f * s;
            auto scaled = juce::Rectangle<float>(cx - hw, cy - hh, hw * 2.f, hh * 2.f);

            juce::ColourGradient btnGrad(
                juce::Colour(fullScreenMode_ ? 0x70FF4FA3 : hov ? 0x55FF4FA3 : 0x28FF4FA3), cx, scaled.getY(),
                juce::Colour(fullScreenMode_ ? 0x55C44E88 : hov ? 0x36C44E88 : 0x18C44E88), cx, scaled.getBottom(), false);
            g.setGradientFill(btnGrad);
            g.fillEllipse(scaled);
            g.setColour(juce::Colour(0xFFFF4FA3).withAlpha(fullScreenMode_ ? 0.52f : hov ? 0.36f : 0.16f));
            g.drawEllipse(scaled, 0.8f);
            g.setColour(juce::Colours::white.withAlpha(hov ? 0.14f : 0.06f));
            g.fillEllipse(cx - hw * 0.3f, cy - hh * 0.65f, hw * 0.7f, hh * 0.4f);

            auto eye = juce::Rectangle<float>(cx - 8.f, cy - 5.f, 16.f, 10.f);
            juce::Path eyePath;
            eyePath.startNewSubPath(eye.getX(), cy);
            eyePath.quadraticTo(cx, eye.getY() - 2.f, eye.getRight(), cy);
            eyePath.quadraticTo(cx, eye.getBottom() + 2.f, eye.getX(), cy);
            g.setColour(juce::Colour(0xFFFFD4E8).withAlpha(hov || fullScreenMode_ ? 0.92f : 0.58f));
            g.strokePath(eyePath, juce::PathStrokeType(1.3f));
            g.fillEllipse(cx - 2.4f, cy - 2.4f, 4.8f, 4.8f);
        }

        // ── Close (X) button — premium gel orb style ──
        {
            auto cb = closeBtnBounds();
            bool hov = cb.contains(getMouseXYRelative().toFloat());
            bool pressed = closeBtnPressed_;
            // Anchor center from the unscaled button (prevents drift on scale)
            float cx = cb.getCentreX();
            float cy = cb.getCentreY();
            float s = pressed ? 0.92f : hov ? 1.06f : 1.0f;
            float hw = cb.getWidth() * 0.5f * s;
            float hh = cb.getHeight() * 0.5f * s;
            auto scaled = juce::Rectangle<float>(cx - hw, cy - hh, hw * 2.f, hh * 2.f);

            // Soft dark-pink tinted base
            juce::ColourGradient btnGrad(
                juce::Colour(hov ? 0x60FF4FA3 : 0x30FF4FA3), cx, scaled.getY(),
                juce::Colour(hov ? 0x40C44E88 : 0x20C44E88), cx, scaled.getBottom(), false);
            g.setGradientFill(btnGrad);
            g.fillEllipse(scaled);

            // Subtle pink outline
            g.setColour(juce::Colour(0xFFFF4FA3).withAlpha(hov ? 0.4f : 0.18f));
            g.drawEllipse(scaled, 0.8f);

            // Specular highlight
            g.setColour(juce::Colours::white.withAlpha(hov ? 0.12f : 0.05f));
            g.fillEllipse(cx - hw * 0.3f, cy - hh * 0.65f,
                          hw * 0.7f, hh * 0.4f);

            // X icon — always centered on the fixed button center
            float arm = kCloseBtnSize * 0.20f;
            g.setColour(juce::Colour(0xFFFFD4E8).withAlpha(hov ? 0.95f : 0.6f));
            g.drawLine(cx - arm, cy - arm, cx + arm, cy + arm, 1.4f);
            g.drawLine(cx + arm, cy - arm, cx - arm, cy + arm, 1.4f);
        }

        // Header title — slightly pink-tinted white
        g.setColour(juce::Colour(0xFFFFD4E8));
        g.setFont(juce::Font(11.f, juce::Font::bold));
        g.drawText(fullScreenMode_ ? "BUBBLEGUM MATRIX" : "BUBBLEGUM SENDS",
                   header.reduced(14.f, 0.f).removeFromTop(24.f)
                          .withTrimmedRight((kCloseBtnSize + kEyeBtnSize) + 16.f + (masterSelected_ ? 28.f : 0.f)),
                   juce::Justification::centredLeft);

        // Source label
        g.setColour(juce::Colour(0xCCFFFFFF));
        g.setFont(juce::Font(10.f, juce::Font::plain));
        juce::String srcLabel = fullScreenMode_
            ? "All tracks: manage sends + sidechains without selecting a track"
            : "Source: " + (sourceName_.isEmpty() ? juce::String("<none>") : sourceName_);
        g.drawText(srcLabel, header.reduced(14.f, 0.f).withTrimmedTop(22.f).removeFromTop(18.f),
                   juce::Justification::centredLeft);

        // Separator
        g.setColour(juce::Colour(0x40FF4FA3));
        g.fillRect(inner.getX() + 10.f, (float)kHeaderH - 1.f + 2.f, inner.getWidth() - 20.f, 1.f);

        // ── Master-selected crown badge (top-right of the header) ──────────
        // A gold crown appears at the top-right whenever the master track is
        // the currently selected source track. Drawn last so it always sits
        // above the header title and source label.
        if (masterSelected_)
        {
            const auto eye = eyeBtnBounds();
            drawCrownBadge(g, juce::Rectangle<float>(eye.getX() - 8.f - 20.f,
                                                     eye.getCentreY() - 10.f,
                                                     20.f, 20.f));
        }
    }

    /** Paint the scrollable row area. */
    void paintContent(juce::Graphics& g, int width, int /*height*/)
    {
        if (fullScreenMode_)
        {
            paintFullScreenContent(g, width);
            return;
        }

        if (!bgV2_ || targets_.empty()) return;

        float alpha = bgV2_->transition.getAlpha();
        if (alpha < 0.01f) alpha = 1.f;

        // Right-side layout (no left dot — knob is now the send toggle):
        //   X     : right edge  →  tx = width - 14
        //   knob  : 30 px left of X centre
        //   SC    : 32 px left of knob centre
        //   name  : starts from left inset
        const float kTrashCX = (float)width - 14.f;
        const float kKnobCX  = kTrashCX - 30.f;
        const float kSCCX    = kKnobCX  - 30.f;
        const float kSCW     = 26.f;   // sidechain button — slightly larger
        const float kNameL   = 12.f;
        const float kNameR   = kSCCX - kSCW * 0.5f - 6.f;

        for (int i = 0; i < (int)targets_.size(); ++i)
        {
            auto& tid = targets_[i];
            float rowY  = (float)(i * kRowH);
            auto rowBounds = juce::Rectangle<float>(0.f, rowY, (float)width, (float)kRowH);
            float cy    = rowBounds.getCentreY();

            bool hasSend   = bgV2_->hasSendTo(tid);
            bool isActive  = hasSend && bgV2_->isSendActive(tid);
            float level    = hasSend ? bgV2_->getSendLevelTo(tid) : 0.f;
            bool isHovered = (hoveredRow_ == i);
            bool isDragging = (dragRow_ == i);
            bool isMaster  = (i < (int)isMasterFlags_.size()) && isMasterFlags_[i];
            const bool masterSel = isMaster && masterSelected_;

            // Row background
            if (masterSel)
            {
                // Master row selected — same near-black/magenta selection
                // language as a regular selected track.
                g.setColour(juce::Colour(0xFF1A0012).withAlpha(0.92f));
                g.fillRoundedRectangle(rowBounds.reduced(4.f, 1.f), 6.f);
                g.setColour(Theme::getInstance().apex.color.magenta.withAlpha(0.95f));
                g.drawRoundedRectangle(rowBounds.reduced(4.f, 1.f), 6.f, 1.0f);
            }
            else if (isDragging)
            {
                g.setColour(juce::Colour(0xFFFF4FA3).withAlpha(0.10f));
                g.fillRoundedRectangle(rowBounds.reduced(4.f, 1.f), 6.f);
            }
            else if (isHovered)
            {
                g.setColour(juce::Colour(0xFFFF7DB8).withAlpha(0.05f));
                g.fillRoundedRectangle(rowBounds.reduced(4.f, 1.f), 6.f);
            }
            if (i % 2 == 0)
            {
                g.setColour(juce::Colours::white.withAlpha(0.012f));
                g.fillRect(rowBounds);
            }

            // ── 1. TRACK NAME ───────────────────────────────────────────
            {
                juce::String name = (i < (int)targetNames_.size()) ? targetNames_[i] : tid;
                if (isMaster) name = juce::String(juce::CharPointer_UTF8("\xE2\x98\x85")) + " " + name;
                g.setColour(isMaster
                    ? (hasSend ? juce::Colour(0xFFFF2A91) : juce::Colour(0xFFE91572))
                    : (hasSend ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFFCCCCCC)));
                g.setFont(juce::Font(12.f, (hasSend || isMaster) ? juce::Font::bold : juce::Font::plain));
                float nameRight = kNameR;
                if (masterSel)
                    nameRight -= 96.f;   // reserve room for the MASTER SELECTED seal
                g.drawText(name,
                           juce::Rectangle<float>(kNameL, rowBounds.getY(),
                                                  nameRight - kNameL, (float)kRowH),
                           juce::Justification::centredLeft, true);

                if (masterSel)
                {
                    auto seal = juce::Rectangle<float>(kNameR - 92.f,
                                                      rowBounds.getY() + ((float)kRowH - 13.f) * 0.5f,
                                                      88.f, 13.f);
                    g.setColour(juce::Colour(0xFF0A0D15).withAlpha(0.95f));
                    g.fillRoundedRectangle(seal, 6.5f);
                    g.setColour(Theme::getInstance().apex.color.magenta.withAlpha(0.95f));
                    g.drawRoundedRectangle(seal, 6.5f, 1.0f);
                    g.setFont(Theme::getInstance().fonts.bold.withHeight(7.5f));
                    g.setColour(juce::Colours::white.withAlpha(0.92f));
                    g.drawText("MASTER SELECTED", seal, juce::Justification::centred);
                }
            }

            // ── 3. SC DOT (left of knob — blue dot replaces filled pill) ────
            {
                bool scActive  = bgV2_->hasSidechainTo(tid);
                bool scActived = scActive && bgV2_->isSidechainActive(tid);
                bool scHov     = scBtnHitTest(i, width, hoveredRow_, mousePos_);
                const float scHalfW = kSCW * 0.5f, scHalfH = 9.f;
                auto scRect = juce::Rectangle<float>(kSCCX - scHalfW, cy - scHalfH,
                                                     kSCW, scHalfH * 2.f);

                // Dot slightly enlarged so the sidechain button reads as a
                // proper button (still compact — "just enough").
                float dotR = 6.5f;
                auto dotB = juce::Rectangle<float>(scRect.getCentreX() - dotR,
                                                   scRect.getCentreY() - dotR,
                                                   dotR * 2.f, dotR * 2.f);

                if (scActived)
                {
                    g.setColour(juce::Colour(0xFF00C8FF).withAlpha(scHov ? 1.0f : 0.85f));
                    g.fillEllipse(dotB);
                    g.setColour(juce::Colour(0xFF00C8FF).withAlpha(0.35f));
                    g.drawEllipse(dotB.expanded(3.f), 1.5f);
                }
                else if (scActive)
                {
                    // Exists but inactive — hollow blue
                    g.setColour(juce::Colour(0xFF00C8FF).withAlpha(scHov ? 0.55f : 0.35f));
                    g.drawEllipse(dotB, 1.5f);
                }
                else
                {
                    // No SC — faint hollow white affordance
                    g.setColour(juce::Colours::white.withAlpha(scHov ? 0.35f : 0.15f));
                    g.drawEllipse(dotB, 1.2f);
                    // "SC" label — bigger + brighter so the zone is obvious
                    g.setColour(juce::Colours::white.withAlpha(scHov ? 0.80f : 0.45f));
                    g.setFont(juce::Font(8.5f, juce::Font::bold));
                    g.drawText("SC", scRect, juce::Justification::centred, false);
                }
            }

            // ── 4. SEND KNOB (right area) ────────────────────────────────
            {
                const auto circColTop = isMaster ? juce::Colour(0xFFFF2A91) : juce::Colour(0xFFFF7DB8);
                const auto circColBot = isMaster ? juce::Colour(0xFFE91572) : juce::Colour(0xFFC44E88);
                const auto ringCol    = isMaster ? juce::Colour(0xFF813CFF) : juce::Colour(0xFFFF7DB8);
                const auto glowCol    = isMaster ? juce::Colour(0xFFFF2A91) : juce::Colour(0xFFFF4FA3);
                const auto offOutline = isMaster ? juce::Colour(0xFF5A2DB8) : juce::Colour(0xFFFF4FA3);

                bool knobHov = isHovered && isKnobHit(mousePos_.toInt(), i, width);
                if (hasSend && isActive)
                {
                    // Active send — full coloured knob
                    g.setColour(glowCol.withAlpha(0.12f * alpha));
                    g.fillEllipse(kKnobCX - kRingR - 2.f, cy - kRingR - 2.f,
                                  (kRingR + 2.f) * 2.f, (kRingR + 2.f) * 2.f);

                    if (level > 0.01f)
                    {
                        float startAngle = juce::MathConstants<float>::pi;
                        float sweep = juce::jmin(level, 2.0f) * 0.5f
                                      * juce::MathConstants<float>::twoPi;
                        juce::Path ring;
                        ring.addArc(kKnobCX - kRingR, cy - kRingR,
                                    kRingR * 2.f, kRingR * 2.f,
                                    startAngle, startAngle + sweep, true);
                        g.setColour(ringCol.withAlpha(0.65f * alpha));
                        g.strokePath(ring, juce::PathStrokeType(2.5f));
                    }

                    juce::ColourGradient circGrad(circColTop, kKnobCX, cy - kCircleR,
                                                  circColBot, kKnobCX, cy + kCircleR, false);
                    g.setGradientFill(circGrad);
                    g.fillEllipse(kKnobCX - kCircleR, cy - kCircleR,
                                  kCircleR * 2.f, kCircleR * 2.f);

                    g.setColour(juce::Colours::white.withAlpha(0.3f));
                    g.fillEllipse(kKnobCX - kCircleR * 0.4f, cy - kCircleR * 0.7f,
                                  kCircleR * 0.7f, kCircleR * 0.45f);

                    // dB tooltip while dragging/hovering
                    if (isDragging || isHovered)
                    {
                        float db = (level <= 0.0001f) ? -60.f : 20.f * std::log10(level);
                        g.setColour(juce::Colours::white.withAlpha(0.8f));
                        g.setFont(juce::Font(8.f, juce::Font::bold));
                        g.drawText(juce::String(db, 1) + " dB",
                                   kKnobCX - 22.f, cy + kRingR + 2.f, 44.f, 10.f,
                                   juce::Justification::centred);
                    }

                    // Trash X — only when send exists
                    float tx = kTrashCX;
                    float arm = 4.f;
                    bool trashHov = isHovered && isNearTrash(mousePos_, i, width);
                    g.setColour(juce::Colour(0xFFFF6666).withAlpha(trashHov ? 0.85f : 0.35f));
                    g.drawLine(tx - arm, cy - arm, tx + arm, cy + arm, 1.4f);
                    g.drawLine(tx + arm, cy - arm, tx - arm, cy + arm, 1.4f);
                }
                else if (hasSend && !isActive)
                {
                    // Send exists but bypassed — dim outline, hover brightens as affordance to re-enable
                    g.setColour(offOutline.withAlpha((knobHov ? 0.55f : 0.28f) * alpha));
                    g.drawEllipse(kKnobCX - kCircleR, cy - kCircleR,
                                  kCircleR * 2.f, kCircleR * 2.f, 1.5f);
                    if (knobHov)
                    {
                        g.setColour(offOutline.withAlpha(0.12f));
                        g.fillEllipse(kKnobCX - kCircleR, cy - kCircleR,
                                      kCircleR * 2.f, kCircleR * 2.f);
                    }
                    // Trash X
                    float tx = kTrashCX;
                    float arm = 4.f;
                    bool trashHov = isHovered && isNearTrash(mousePos_, i, width);
                    g.setColour(juce::Colour(0xFFFF6666).withAlpha(trashHov ? 0.85f : 0.28f));
                    g.drawLine(tx - arm, cy - arm, tx + arm, cy + arm, 1.4f);
                    g.drawLine(tx + arm, cy - arm, tx - arm, cy + arm, 1.4f);
                }
                else
                {
                    // No send — very faint knob outline, brightens on hover as "click to create" affordance
                    g.setColour(juce::Colour(0xFF888888).withAlpha((knobHov ? 0.55f : 0.22f) * alpha));
                    g.drawEllipse(kKnobCX - kCircleR, cy - kCircleR,
                                  kCircleR * 2.f, kCircleR * 2.f, 1.5f);
                    if (knobHov)
                    {
                        g.setColour(juce::Colour(0xFF888888).withAlpha(0.10f));
                        g.fillEllipse(kKnobCX - kCircleR, cy - kCircleR,
                                      kCircleR * 2.f, kCircleR * 2.f);
                    }
                }
            }

            // Row separator
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.fillRect(10.f, rowBounds.getBottom() - 0.5f, (float)width - 20.f, 0.5f);
        }
    }

    // ── Mouse: close button + drag panel by header area ────────────────
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (eyeBtnBounds().contains(e.position))
        {
            eyeBtnPressed_ = true;
            repaint();
            return;
        }
        // Close button hit test
        if (closeBtnBounds().contains(e.position))
        {
            closeBtnPressed_ = true;
            repaint();
            return;
        }
        if (e.y <= kHeaderH)
        {
            isDragging_ = true;
            dragger_.startDraggingComponent(this, e);
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (isDragging_)
            dragger_.dragComponent(this, e, nullptr);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (eyeBtnPressed_)
        {
            eyeBtnPressed_ = false;
            if (eyeBtnBounds().contains(e.position))
                toggleFullScreenMode();
            repaint();
            return;
        }
        if (closeBtnPressed_)
        {
            closeBtnPressed_ = false;
            if (closeBtnBounds().contains(e.position))
            {
                DBG("[BubblegumV2PanelUI] X close button clicked");
                if (onCloseRequested) onCloseRequested();
            }
            repaint();
            return;
        }
        isDragging_ = false;
    }
    void mouseMove(const juce::MouseEvent& e) override
    {
        // Repaint header for close button hover state
        bool overClose = closeBtnBounds().contains(e.position);
        bool overEye = eyeBtnBounds().contains(e.position);
        if (overClose != closeBtnHovered_ || overEye != eyeBtnHovered_)
        {
            closeBtnHovered_ = overClose;
            eyeBtnHovered_ = overEye;
            repaint(juce::Rectangle<int>(0, 0, getWidth(), kHeaderH));
        }
        setMouseCursor(DAW::CursorThemeCore::getStandard(
            overClose || overEye || (e.y <= kHeaderH)
                ? juce::MouseCursor::PointingHandCursor
                : juce::MouseCursor::NormalCursor));
    }

    // ── Content mouse handling ───────────────────────────────────────────
    void contentMouseDown(const juce::MouseEvent& e)
    {
        if (fullScreenMode_)
        {
            fullContentMouseDown(e);
            return;
        }

        if (!bgV2_ || targets_.empty()) return;
        int row = e.getPosition().y / kRowH;
        if (row < 0 || row >= (int)targets_.size()) return;

        auto& tid = targets_[row];
        bool hasSend = bgV2_->hasSendTo(tid);
        int w = content_.getWidth();

        // Right-click SC button → delete sidechain; right-click knob/row → delete send
        if (e.mods.isRightButtonDown())
        {
            if (isSCBtnHit(e.getPosition(), row, w) && bgV2_->hasSidechainTo(tid))
            {
                DBG("[BubblegumV2PanelUI] right-click SC delete sidechain: " + tid);
                bgV2_->removeSidechain(tid);
                refresh();
                if (onSendChanged) onSendChanged();
            }
            return;
        }

        // SC button hit → toggle sidechain
        if (isSCBtnHit(e.getPosition(), row, w))
        {
            DBG("[BubblegumV2PanelUI] SC tap: " + tid);
            bgV2_->handleSidechainTap(tid);
            refresh();
            if (onSendChanged) onSendChanged();
            return;
        }

        // Trash X hit → delete send
        if (isNearTrash(e.getPosition().toFloat(), row, w) && hasSend)
        {
            DBG("[BubblegumV2PanelUI] trash delete: " + tid);
            bgV2_->removeSend(tid);
            refresh();
            if (onSendChanged) onSendChanged();
            return;
        }

        // Knob hit → create send (if none), toggle active (if exists), or start level drag (if active)
        if (isKnobHit(e.getPosition(), row, w))
        {
            if (!hasSend)
            {
                // No send — knob click creates it
                FORENSIC_LOG("[BUBBLEGUM CLICK] knob-create row=" << row
                    << " src=" << (bgV2_ ? bgV2_->sourceSync.getSourceTrackId() : juce::String())
                    << " dst=" << tid);
                DBG("[BubblegumV2PanelUI] knob create: " + tid);
                bgV2_->handleTargetTap(tid);
                refresh();
                if (onSendChanged) onSendChanged();
            }
            else if (bgV2_->isSendActive(tid))
            {
                // Active — start level drag; commit as toggle if no real drag
                bgV2_->beginContinuousTopologyGesture(isMasterFlags_[row] ? "Adjust Master Route Level" : "Adjust Send Level");
                dragRow_        = row;
                dragStartY_     = e.getPosition().y;
                dragStartLevel_ = bgV2_->getSendLevelTo(tid);
                dotClickRow_    = row;
            }
            else
            {
                // Inactive — knob click re-enables
                bgV2_->toggleSendActive(tid);
                refresh();
                if (onSendChanged) onSendChanged();
            }
            return;
        }

        // Clicking elsewhere on row does nothing
    }

    void contentMouseDrag(const juce::MouseEvent& e)
    {
        if (fullScreenMode_)
        {
            fullContentMouseDrag(e);
            return;
        }

        if (dragRow_ < 0 || !bgV2_) return;
        if (dragRow_ >= (int)targets_.size()) return;

        int deltaY = std::abs(e.getPosition().y - dragStartY_);
        if (deltaY >= 3)
            dotClickRow_ = -1; // real drag — cancel toggle

        auto& tid = targets_[dragRow_];
        float deltaYf = (float)(dragStartY_ - e.getPosition().y);
        float newLevel = juce::jlimit(0.f, 2.f, dragStartLevel_ + deltaYf * 0.005f);

        bgV2_->setSendLevel(tid, newLevel);
        content_.repaint();
    }

    void contentMouseUp(const juce::MouseEvent& e)
    {
        if (fullScreenMode_)
        {
            fullContentMouseUp(e);
            return;
        }

        if (dragRow_ >= 0)
        {
            const int committedRow = dragRow_;
            // Knob was clicked without dragging → toggle active
            if (dotClickRow_ >= 0 && dotClickRow_ < (int)targets_.size())
            {
                int deltaY = std::abs(e.getPosition().y - dragStartY_);
                if (deltaY < 3)
                {
                    auto& tid = targets_[dotClickRow_];
                    DBG("[BubblegumV2PanelUI] knob click toggle: " + tid);
                    if (bgV2_)
                    {
                        bgV2_->toggleSendActive(tid);
                        refresh();
                        if (onSendChanged) onSendChanged();
                    }
                }
            }
            if (bgV2_)
                bgV2_->commitContinuousTopologyGesture((committedRow >= 0 && committedRow < (int)isMasterFlags_.size() && isMasterFlags_[committedRow])
                    ? "Adjust Master Route Level"
                    : "Adjust Send Level");
            dragRow_     = -1;
            dotClickRow_ = -1;
            content_.repaint();
        }
    }

    void contentMouseMove(const juce::MouseEvent& e)
    {
        if (fullScreenMode_)
        {
            fullMousePos_ = e.getPosition().toFloat();
            fullHoveredRow_ = fullRowAt(e.getPosition());
            content_.repaint();
            return;
        }

        mousePos_ = e.getPosition().toFloat();
        int row = e.getPosition().y / kRowH;
        if (row != hoveredRow_)
        {
            hoveredRow_ = (row >= 0 && row < (int)targets_.size()) ? row : -1;
        }
        content_.repaint();
    }

    void contentMouseExit()
    {
        if (fullScreenMode_)
        {
            fullMousePos_ = { -1000.f, -1000.f };
            fullHoveredRow_ = -1;
            content_.repaint();
            return;
        }

        mousePos_   = { -1000.f, -1000.f };
        hoveredRow_ = -1;
        content_.repaint();
    }

    void contentMouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (!fullScreenMode_)
            return;

        const int cardIndex = fullCardAt(e.getPosition());
        if (cardIndex < 0 || cardIndex >= (int)fullCards_.size())
            return;

        auto& card = fullCards_[(size_t)cardIndex];
        if (card.maxScroll <= 0)
            return;

        const int delta = juce::roundToInt(-wheel.deltaY * 120.0f);
        const int fallbackDelta = wheel.deltaY < 0.0f ? 36 : -36;
        card.scrollY = juce::jlimit(0, card.maxScroll, card.scrollY + (delta != 0 ? delta : fallbackDelta));
        layoutFullCardRows(cardIndex);
        fullHoveredRow_ = fullRowAt(e.getPosition());
        content_.repaint(card.bounds.expanded(2));
    }

private:
    BubblegumV2System* bgV2_   = nullptr;
    TrackManager*      tracks_ = nullptr;

    struct FullRow
    {
        TrackID sourceId;
        TrackID targetId;
        juce::String sourceName;
        juce::String targetName;
        bool targetIsMaster = false;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> rawBounds;
        int cardIndex = -1;
        bool visible = true;
    };

    struct FullCard
    {
        TrackID sourceId;
        juce::String sourceName;
        juce::Rectangle<int> bounds;
        int firstRow = 0;
        int rowCount = 0;
        int scrollY = 0;
        int maxScroll = 0;
        int visibleRows = 0;
        juce::Rectangle<int> rowsBounds;
    };

    TrackID                   sourceId_;
    juce::String              sourceName_;
    std::vector<TrackID>      targets_;
    std::vector<juce::String> targetNames_;
    std::vector<juce::Colour> targetColors_;
    std::vector<bool>         isMasterFlags_;
    bool                      masterSelected_ = false;

    int   hoveredRow_     = -1;
    int   dragRow_        = -1;
    int   dotClickRow_    = -1;
    int   dragStartY_     = 0;
    float dragStartLevel_ = 0.f;
    juce::Point<float> mousePos_ { -1000.f, -1000.f };

    bool fullScreenMode_ = false;
    juce::Rectangle<int> compactBoundsBeforeFull_;
    TrackID compactSourceBeforeFull_;
    std::vector<FullRow> fullRows_;
    std::vector<FullCard> fullCards_;
    std::vector<std::pair<TrackID, int>> pendingFullCardScroll_;
    int fullContentH_ = 0;
    int fullTrackCount_ = -1;
    int fullHoveredRow_ = -1;
    int fullDragRow_ = -1;
    int fullDotClickRow_ = -1;
    int fullDragStartY_ = 0;
    float fullDragStartLevel_ = 0.f;
    juce::Point<float> fullMousePos_ { -1000.f, -1000.f };

    // Scrollbar drag state
    int scrollbarDragCardIndex_ = -1;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartScrollY_ = 0;

    // Drag-to-move
    bool isDragging_ = false;
    juce::ComponentDragger dragger_;

    // Close button state
    bool closeBtnPressed_ = false;
    bool closeBtnHovered_ = false;
    bool eyeBtnPressed_ = false;
    bool eyeBtnHovered_ = false;

    // ── Send-state hash for repaint gating ───────────────────────────────
    // Covers all values paintContent reads per-row. Only repaint when changed.
    std::size_t lastSendStateHash_ = 0;
    int lastFullViewportW_ = -1;
    int lastFullViewportH_ = -1;

    std::size_t computeSendStateHash() const noexcept
    {
        if (!bgV2_) return 0;
        if (fullScreenMode_)
        {
            std::size_t h = std::hash<int>{}((int)fullRows_.size());
            for (const auto& rowInfo : fullRows_)
            {
                if (!rowInfo.visible)
                    continue;

                const bool hasSend  = bgV2_->hasSendFromTo(rowInfo.sourceId, rowInfo.targetId);
                const bool active   = hasSend && bgV2_->isSendActiveFromTo(rowInfo.sourceId, rowInfo.targetId);
                const float level   = hasSend ? bgV2_->getSendLevelFromTo(rowInfo.sourceId, rowInfo.targetId) : 0.f;
                const bool hasSC    = bgV2_->hasSidechainFromTo(rowInfo.sourceId, rowInfo.targetId);
                const bool scActive = hasSC && bgV2_->isSidechainActiveFromTo(rowInfo.sourceId, rowInfo.targetId);
                const int levelQ    = (int)(level * 1000.f);

                std::size_t row = std::hash<std::string>{}(rowInfo.sourceId.toStdString());
                row ^= std::hash<std::string>{}(rowInfo.targetId.toStdString()) + 0x9e3779b9 + (row << 6) + (row >> 2);
                row ^= std::hash<int>{}((int)hasSend | ((int)active << 1) | ((int)hasSC << 2) | ((int)scActive << 3));
                row ^= std::hash<int>{}(levelQ);
                h ^= row + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            return h;
        }

        std::size_t h = std::hash<std::string>{}(sourceId_.toStdString());
        h ^= std::hash<size_t>{}(targets_.size()) + 0x9e3779b9 + (h << 6) + (h >> 2);
        for (const auto& tid : targets_)
        {
            bool hasSend  = bgV2_->hasSendTo(tid);
            bool active   = hasSend && bgV2_->isSendActive(tid);
            float level   = hasSend ? bgV2_->getSendLevelTo(tid) : 0.f;
            bool hasSC    = bgV2_->hasSidechainTo(tid);
            bool scActive = hasSC && bgV2_->isSidechainActive(tid);
            int levelQ    = (int)(level * 1000.f);
            std::size_t row = std::hash<std::string>{}(tid.toStdString());
            row ^= std::hash<int>{}((int)hasSend | ((int)active << 1) | ((int)hasSC << 2) | ((int)scActive << 3));
            row ^= std::hash<int>{}(levelQ);
            h ^= row + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }

    // ── Layout helpers (must match paintContent) ─────────────────────────
    static float trashCX(int width) noexcept { return (float)width - 14.f; }
    static float knobCX(int width)  noexcept { return trashCX(width) - 30.f; }
    static float scCX(int width)    noexcept { return knobCX(width)  - 30.f; }

    bool isDotHit(juce::Point<int> pt, int row, int /*w*/) const noexcept
    {
        float cy = (float)(row * kRowH) + kRowH * 0.5f;
        float dx = (float)pt.x - 14.f;
        float dy = (float)pt.y - cy;
        return (dx * dx + dy * dy) <= (kDotR + 4.f) * (kDotR + 4.f);
    }

    bool isKnobHit(juce::Point<int> pt, int row, int width) const noexcept
    {
        float cy = (float)(row * kRowH) + kRowH * 0.5f;
        float cx = knobCX(width);
        float dx = (float)pt.x - cx;
        float dy = (float)pt.y - cy;
        return (dx * dx + dy * dy) <= (kRingR + 4.f) * (kRingR + 4.f);
    }

    bool isSCBtnHit(juce::Point<int> pt, int row, int width) const noexcept
    {
        const float kSCW = 26.f, kSCH = 18.f;
        float cy = (float)(row * kRowH) + kRowH * 0.5f;
        float cx = scCX(width);
        return std::abs((float)pt.x - cx) <= kSCW * 0.5f + 3.f
            && std::abs((float)pt.y - cy) <= kSCH * 0.5f + 3.f;
    }

    bool isNearTrash(juce::Point<float> pt, int row, int width) const noexcept
    {
        float tx = trashCX(width);
        float ty = (float)(row * kRowH) + kRowH * 0.5f;
        float dx = pt.x - tx, dy = pt.y - ty;
        return (dx * dx + dy * dy) <= 10.f * 10.f;
    }

    // Helpers used by paintContent for hover detection
    bool dotHitTest(int row, int width, int hovRow) const noexcept
    {
        return (hovRow == row) && isDotHit(mousePos_.toInt(), row, width);
    }
    bool scBtnHitTest(int row, int width, int hovRow,
                       juce::Point<float> mp) const noexcept
    {
        return (hovRow == row) && isSCBtnHit(mp.toInt(), row, width);
    }

    void toggleFullScreenMode()
    {
        if (!fullScreenMode_)
        {
            compactBoundsBeforeFull_ = getBounds();
            compactSourceBeforeFull_ = bgV2_ ? bgV2_->sourceSync.getSourceTrackId() : TrackID();
            fullScreenMode_ = true;
            fullTrackCount_ = getFullTrackSignature();
            rebuildFullRows();
            if (auto* parent = getParentComponent())
                setBounds(parent->getLocalBounds().reduced(18));
            resized();
            toFront(true);
        }
        else
        {
            fullScreenMode_ = false;
            fullRows_.clear();
            fullCards_.clear();
            fullDragRow_ = -1;
            fullDotClickRow_ = -1;
            if (bgV2_ && compactSourceBeforeFull_.isNotEmpty())
                bgV2_->onTrackSelected(compactSourceBeforeFull_);
            if (!compactBoundsBeforeFull_.isEmpty())
                setBounds(compactBoundsBeforeFull_);
            refresh();
        }
        repaint();
    }

    void rebuildFullRows()
    {
        std::vector<std::pair<TrackID, int>> previousScroll;
        for (const auto& card : fullCards_)
            previousScroll.push_back({ card.sourceId, card.scrollY });

        fullRows_.clear();
        fullCards_.clear();
        fullContentH_ = 0;
        if (!tracks_) return;

        std::vector<Track*> sources;
        sources.reserve((size_t)tracks_->getNumTracks());
        for (int i = 0; i < tracks_->getNumTracks(); ++i)
            if (auto* source = tracks_->getTrack(i))
                sources.push_back(source);

        auto addTargetRows = [this](Track& source)
        {
            if (auto* master = tracks_->getMasterTrack())
            {
                if (master->getID() != source.getID())
                    fullRows_.push_back({ source.getID(), master->getID(), source.getName(), master->getName(), true });
            }

            for (int targetIndex = 0; targetIndex < tracks_->getNumTracks(); ++targetIndex)
            {
                auto* target = tracks_->getTrack(targetIndex);
                if (target && target->getID() != source.getID())
                    fullRows_.push_back({ source.getID(), target->getID(), source.getName(), target->getName(), false });
            }
        };

        for (auto* source : sources)
            if (source)
                addTargetRows(*source);

        fullTrackCount_ = getFullTrackSignature();
        pendingFullCardScroll_ = std::move(previousScroll);
    }

    void rebuildFullLayout(int width)
    {
        const int currentTrackCount = getFullTrackSignature();
        if (fullRows_.empty() || currentTrackCount != fullTrackCount_)
        {
            fullTrackCount_ = currentTrackCount;
            rebuildFullRows(); // saves scrollY into pendingFullCardScroll_
        }
        else
        {
            // Track count unchanged — preserve current scroll before clearing
            pendingFullCardScroll_.clear();
            for (const auto& card : fullCards_)
                pendingFullCardScroll_.push_back({ card.sourceId, card.scrollY });
        }

        fullCards_.clear();
        if (fullRows_.empty())
        {
            fullContentH_ = 0;
            return;
        }

        const int visibleRows = getVisibleRowsPerFullCard();
        const int cardW = 240;
        const int gapX = 16;
        const int gapY = 16;
        const int headerH = 40;
        const int rowH = kFullRowH;
        const int inX = 16;
        int currentX = inX;
        int currentY = 16;
        int maxH = 0;

        int i = 0;
        while (i < (int)fullRows_.size())
        {
            TrackID currentSource = fullRows_[(size_t)i].sourceId;
            int startRow = i;
            while (i < (int)fullRows_.size() && fullRows_[(size_t)i].sourceId == currentSource)
                i++;
            int rowCount = i - startRow;
            int shownRows = juce::jmin(rowCount, visibleRows);
            int cardH = headerH + shownRows * rowH + 12;

            if (currentX + cardW > width - 16 && currentX > inX)
            {
                currentX = inX;
                currentY += maxH + gapY;
                maxH = 0;
            }

            FullCard card;
            card.sourceId = currentSource;
            card.sourceName = fullRows_[(size_t)startRow].sourceName;
            card.bounds = { currentX, currentY, cardW, cardH };
            card.firstRow = startRow;
            card.rowCount = rowCount;
            card.visibleRows = shownRows;
            card.rowsBounds = { currentX, currentY + headerH, cardW, shownRows * rowH };
            card.maxScroll = juce::jmax(0, rowCount * rowH - shownRows * rowH);
            int preservedScroll = 0;
            for (const auto& storedScroll : pendingFullCardScroll_)
            {
                if (storedScroll.first == currentSource)
                {
                    preservedScroll = storedScroll.second;
                    break;
                }
            }
            card.scrollY = juce::jlimit(0, card.maxScroll, preservedScroll);
            fullCards_.push_back(card);

            layoutFullCardRows((int)fullCards_.size() - 1);

            currentX += cardW + gapX;
            maxH = juce::jmax(maxH, cardH);
        }

        pendingFullCardScroll_.clear();
        fullContentH_ = currentY + maxH + gapY + 16;
    }

    int fullRowAt(juce::Point<int> pt) const noexcept
    {
        for (int i = 0; i < (int)fullRows_.size(); ++i)
            if (fullRows_[(size_t)i].visible && fullRows_[(size_t)i].bounds.contains(pt))
                return i;
        return -1;
    }

    int fullCardAt(juce::Point<int> pt) const noexcept
    {
        for (int i = 0; i < (int)fullCards_.size(); ++i)
            if (fullCards_[(size_t)i].bounds.contains(pt))
                return i;
        return -1;
    }

    int getVisibleRowsPerFullCard() const noexcept
    {
        const int totalTracks = tracks_ ? tracks_->getNumTracks() : 0;
        if (totalTracks <= 9)  return 9;
        if (totalTracks <= 12) return 8;
        if (totalTracks <= 16) return 7;
        return 6;
    }

    int getFullTrackSignature() const noexcept
    {
        if (!tracks_)
            return 0;

        int signature = tracks_->getNumTracks() * 31;
        if (tracks_->hasMasterTrack())
            signature += 1;
        return signature;
    }

    void layoutFullCardRows(int cardIndex)
    {
        if (cardIndex < 0 || cardIndex >= (int)fullCards_.size())
            return;

        auto& card = fullCards_[(size_t)cardIndex];
        const int rowH = kFullRowH;
        const int visibleTop = card.rowsBounds.getY();
        const int visibleBottom = card.rowsBounds.getBottom();

        for (int r = card.firstRow; r < card.firstRow + card.rowCount && r < (int)fullRows_.size(); ++r)
        {
            const int localRow = r - card.firstRow;
            const int y = visibleTop + localRow * rowH - card.scrollY;
            auto& row = fullRows_[(size_t)r];
            row.cardIndex = cardIndex;
            row.rawBounds = { card.bounds.getX(), y, card.bounds.getWidth(), rowH };
            row.bounds = row.rawBounds.getIntersection(card.rowsBounds);
            row.visible = row.bounds.getHeight() >= rowH / 2 && y >= visibleTop - rowH && y < visibleBottom;
        }
    }

    void updateContentSize()
    {
        auto contentW = juce::jmax(1, viewport_.getWidth()
            - (viewport_.isVerticalScrollBarShown() ? viewport_.getScrollBarThickness() : 0));
        if (fullScreenMode_)
            rebuildFullLayout(contentW);

        int contentH = fullScreenMode_
            ? juce::jmax(fullContentH_, viewport_.getHeight())
            : juce::jmax((int)targets_.size() * kRowH, viewport_.getHeight());
        content_.setSize(contentW, contentH);
    }


    void paintFullScreenContent(juce::Graphics& g, int width)
    {
        if (!bgV2_ || !tracks_) return;
        if (fullRows_.empty() || fullCards_.empty())
            rebuildFullLayout(width);

        for (const auto& card : fullCards_)
        {
            auto inner = card.bounds.toFloat();

            g.setColour(juce::Colour(0xF00F0F14));
            g.fillRoundedRectangle(inner, kCorner);

            juce::ColourGradient bodyGrad(juce::Colour(0x18FFFFFF), inner.getCentreX(), inner.getY(),
                                          juce::Colour(0x05000000), inner.getCentreX(), inner.getBottom(), false);
            g.setGradientFill(bodyGrad);
            g.fillRoundedRectangle(inner, kCorner);

            juce::ColourGradient pinkGlow(juce::Colour(0x15FF4FA3), inner.getCentreX(), inner.getY(),
                                          juce::Colour(0x00FF4FA3), inner.getCentreX(), inner.getY() + inner.getHeight() * 0.4f, false);
            g.setGradientFill(pinkGlow);
            g.fillRoundedRectangle(inner, kCorner);

            g.setColour(juce::Colour(0x40FF4FA3));
            g.drawRoundedRectangle(inner, kCorner, 1.2f);
            g.setColour(juce::Colour(0x18FF7DB8));
            g.drawRoundedRectangle(inner.reduced(1.f), kCorner - 1.f, 0.8f);

            auto header = inner.removeFromTop(40.f);
            g.saveState();
            g.reduceClipRegion(header.toNearestIntEdges());
            juce::ColourGradient hdrGrad(juce::Colour(0x30FF4FA3), header.getX(), header.getY(),
                                         juce::Colour(0x08FF2D8F), header.getX(), header.getBottom(), false);
            g.setGradientFill(hdrGrad);
            g.fillRoundedRectangle(juce::Rectangle<float>(header.getX(), header.getY(),
                                   header.getWidth(), header.getHeight() + kCorner), kCorner);
            g.restoreState();

            g.setColour(juce::Colour(0xFFFFE8F4));
            g.setFont(juce::Font(11.5f, juce::Font::bold));
            g.drawText("SOURCE: " + card.sourceName.toUpperCase(), header.reduced(14.f, 0.f), juce::Justification::centredLeft);

            g.setColour(juce::Colour(0x40FF4FA3));
            g.fillRect(inner.getX() + 10.f, header.getBottom() - 1.f, inner.getWidth() - 20.f, 1.f);

            if (card.maxScroll > 0)
            {
                auto track = card.rowsBounds.toFloat().withTrimmedTop(4.f).withTrimmedBottom(4.f).withLeft((float)card.bounds.getRight() - 8.f).withWidth(3.f);
                g.setColour(juce::Colours::white.withAlpha(0.08f));
                g.fillRoundedRectangle(track, 1.5f);

                const float visibleFrac = (float)card.visibleRows / (float)juce::jmax(1, card.rowCount);
                const float thumbH = juce::jmax(18.f, track.getHeight() * visibleFrac);
                const float scrollFrac = (float)card.scrollY / (float)juce::jmax(1, card.maxScroll);
                auto thumb = track.withHeight(thumbH).translated(0.f, (track.getHeight() - thumbH) * scrollFrac);
                g.setColour(juce::Colour(0xFFFF7DB8).withAlpha(0.48f));
                g.fillRoundedRectangle(thumb, 1.5f);
            }
        }

        for (int i = 0; i < (int)fullRows_.size(); ++i)
        {
            const auto& row = fullRows_[(size_t)i];
            if (!row.visible)
                continue;

            juce::Graphics::ScopedSaveState rowSave(g);
            if (row.cardIndex >= 0 && row.cardIndex < (int)fullCards_.size())
                g.reduceClipRegion(fullCards_[(size_t)row.cardIndex].rowsBounds);

            auto rowBounds = row.bounds.toFloat();
            const float cy = rowBounds.getCentreY();

            const float kTrashCX = rowBounds.getRight() - 26.f;
            const float kKnobCX  = kTrashCX - 28.f;
            const float kSCCX    = kKnobCX - 32.f;
            const float kTargetL = rowBounds.getX() + 10.f;
            const float kTargetR = kSCCX - 12.f;

            const bool hasSend = bgV2_->hasSendFromTo(row.sourceId, row.targetId);
            const bool isActive = hasSend && bgV2_->isSendActiveFromTo(row.sourceId, row.targetId);
            const float level = hasSend ? bgV2_->getSendLevelFromTo(row.sourceId, row.targetId) : 0.f;
            const bool scActive = bgV2_->hasSidechainFromTo(row.sourceId, row.targetId);
            const bool scActived = scActive && bgV2_->isSidechainActiveFromTo(row.sourceId, row.targetId);
            const bool hovered = fullHoveredRow_ == i;
            const bool dragging = fullDragRow_ == i;

            if (dragging)
            {
                g.setColour(juce::Colour(0xFFFF4FA3).withAlpha(0.12f));
                g.fillRoundedRectangle(rowBounds.reduced(4.f, 1.f), 5.f);
            }
            else if (hovered)
            {
                g.setColour(juce::Colour(0xFFFF7DB8).withAlpha(0.06f));
                g.fillRoundedRectangle(rowBounds.reduced(4.f, 1.f), 5.f);
            }
            if (i % 2 == 0)
            {
                g.setColour(juce::Colours::white.withAlpha(0.012f));
                g.fillRect(rowBounds);
            }

            g.setColour(hasSend ? juce::Colours::white : juce::Colour(0xFFCCCCCC));
            g.setFont(juce::Font(10.5f, (hasSend || row.targetIsMaster) ? juce::Font::bold : juce::Font::plain));
            juce::String targetName = row.targetIsMaster ? juce::String(juce::CharPointer_UTF8("\xE2\x98\x85")) + " " + row.targetName : row.targetName;
            g.drawText(targetName, juce::Rectangle<float>(kTargetL, rowBounds.getY(), kTargetR - kTargetL, rowBounds.getHeight()), juce::Justification::centredLeft, true);

            const float scDotR = 6.5f;
            auto scDot = juce::Rectangle<float>(kSCCX - scDotR, cy - scDotR, scDotR * 2.f, scDotR * 2.f);
            if (scActived)
            {
                g.setColour(juce::Colour(0xFF00C8FF).withAlpha(0.88f));
                g.fillEllipse(scDot);
                g.setColour(juce::Colour(0xFF00C8FF).withAlpha(0.35f));
                g.drawEllipse(scDot.expanded(3.f), 1.4f);
            }
            else if (scActive)
            {
                g.setColour(juce::Colour(0xFF00C8FF).withAlpha(0.40f));
                g.drawEllipse(scDot, 1.4f);
            }
            else
            {
                g.setColour(juce::Colours::white.withAlpha(0.16f));
                g.drawEllipse(scDot, 1.2f);
                g.setFont(juce::Font(8.5f, juce::Font::bold));
                g.drawText("SC", kSCCX - 13.f, cy - 9.f, 26.f, 18.f, juce::Justification::centred, false);
            }

            const auto ringCol = row.targetIsMaster ? juce::Colour(0xFF813CFF) : juce::Colour(0xFFFF7DB8);
            const auto circColTop = row.targetIsMaster ? juce::Colour(0xFFFF2A91) : juce::Colour(0xFFFF7DB8);
            const auto circColBot = row.targetIsMaster ? juce::Colour(0xFFE91572) : juce::Colour(0xFFC44E88);
            bool knobHov = hovered && isFullKnobHit(fullMousePos_.toInt(), i, 0);
            if (hasSend && isActive)
            {
                if (level > 0.01f)
                {
                    juce::Path ring;
                    ring.addArc(kKnobCX - kRingR, cy - kRingR, kRingR * 2.f, kRingR * 2.f,
                                juce::MathConstants<float>::pi,
                                juce::MathConstants<float>::pi + juce::jmin(level, 2.0f) * 0.5f * juce::MathConstants<float>::twoPi,
                                true);
                    g.setColour(ringCol.withAlpha(0.66f));
                    g.strokePath(ring, juce::PathStrokeType(2.5f));
                }
                juce::ColourGradient cg(circColTop, kKnobCX, cy - kCircleR, circColBot, kKnobCX, cy + kCircleR, false);
                g.setGradientFill(cg);
                g.fillEllipse(kKnobCX - kCircleR, cy - kCircleR, kCircleR * 2.f, kCircleR * 2.f);
                g.setColour(juce::Colours::white.withAlpha(0.28f));
                g.fillEllipse(kKnobCX - kCircleR * 0.4f, cy - kCircleR * 0.7f, kCircleR * 0.7f, kCircleR * 0.45f);
                // Trash X
                g.setColour(juce::Colour(0xFFFF6666).withAlpha(hovered ? 0.72f : 0.32f));
                g.drawLine(kTrashCX - 4.f, cy - 4.f, kTrashCX + 4.f, cy + 4.f, 1.4f);
                g.drawLine(kTrashCX + 4.f, cy - 4.f, kTrashCX - 4.f, cy + 4.f, 1.4f);
            }
            else if (hasSend && !isActive)
            {
                g.setColour(ringCol.withAlpha(knobHov ? 0.50f : 0.26f));
                g.drawEllipse(kKnobCX - kCircleR, cy - kCircleR, kCircleR * 2.f, kCircleR * 2.f, 1.4f);
                if (knobHov) { g.setColour(ringCol.withAlpha(0.10f)); g.fillEllipse(kKnobCX - kCircleR, cy - kCircleR, kCircleR * 2.f, kCircleR * 2.f); }
                // Trash X
                g.setColour(juce::Colour(0xFFFF6666).withAlpha(hovered ? 0.72f : 0.26f));
                g.drawLine(kTrashCX - 4.f, cy - 4.f, kTrashCX + 4.f, cy + 4.f, 1.4f);
                g.drawLine(kTrashCX + 4.f, cy - 4.f, kTrashCX - 4.f, cy + 4.f, 1.4f);
            }
            else
            {
                // No send — faint affordance
                g.setColour(juce::Colour(0xFF888888).withAlpha(knobHov ? 0.52f : 0.20f));
                g.drawEllipse(kKnobCX - kCircleR, cy - kCircleR, kCircleR * 2.f, kCircleR * 2.f, 1.4f);
                if (knobHov) { g.setColour(juce::Colour(0xFF888888).withAlpha(0.10f)); g.fillEllipse(kKnobCX - kCircleR, cy - kCircleR, kCircleR * 2.f, kCircleR * 2.f); }
            }

            if (dragging || hovered)
            {
                float db = (level <= 0.0001f) ? -60.f : 20.f * std::log10(level);
                g.setColour(juce::Colours::white.withAlpha(0.74f));
                g.setFont(juce::Font(8.f, juce::Font::bold));
                g.drawText(juce::String(db, 1) + " dB", kKnobCX - 22.f, cy + kRingR + 2.f, 44.f, 10.f, juce::Justification::centred);
            }

            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.fillRect(rowBounds.getX() + 10.f, rowBounds.getBottom() - 0.5f, rowBounds.getWidth() - 20.f, 0.5f);
        }
    }

    bool isFullDotHit(juce::Point<int> pt, int row, int /*width*/) const noexcept
    {
        auto b = fullRows_[(size_t)row].bounds.toFloat();
        float cy = b.getCentreY();
        float cx = b.getX() + 16.f;
        float dx = (float)pt.x - cx;
        float dy = (float)pt.y - cy;
        return (dx * dx + dy * dy) <= (kDotR + 4.f) * (kDotR + 4.f);
    }

    bool isFullSCHit(juce::Point<int> pt, int row, int /*width*/) const noexcept
    {
        auto b = fullRows_[(size_t)row].bounds.toFloat();
        float cy = b.getCentreY();
        float cx = b.getRight() - 26.f - 28.f - 32.f;
        return std::abs((float)pt.x - cx) <= 15.f && std::abs((float)pt.y - cy) <= 12.f;
    }

    bool isFullKnobHit(juce::Point<int> pt, int row, int /*width*/) const noexcept
    {
        auto b = fullRows_[(size_t)row].bounds.toFloat();
        float cy = b.getCentreY();
        float cx = b.getRight() - 26.f - 28.f;
        float dx = (float)pt.x - cx;
        float dy = (float)pt.y - cy;
        return (dx * dx + dy * dy) <= (kRingR + 5.f) * (kRingR + 5.f);
    }

    // Returns card index if pt is over its scrollbar thumb, else -1
    int fullScrollbarCardAt(juce::Point<int> pt) const noexcept
    {
        for (int ci = 0; ci < (int)fullCards_.size(); ++ci)
        {
            const auto& card = fullCards_[(size_t)ci];
            if (card.maxScroll <= 0) continue;
            auto track = card.rowsBounds.toFloat().withTrimmedTop(4.f).withTrimmedBottom(4.f)
                             .withLeft((float)card.bounds.getRight() - 8.f).withWidth(10.f); // wider hit zone
            if (track.contains((float)pt.x, (float)pt.y))
                return ci;
        }
        return -1;
    }

    bool isFullTrashHit(juce::Point<float> pt, int row, int /*width*/) const noexcept
    {
        auto b = fullRows_[(size_t)row].bounds.toFloat();
        float cy = b.getCentreY();
        float cx = b.getRight() - 26.f;
        float dx = pt.x - cx;
        float dy = pt.y - cy;
        return (dx * dx + dy * dy) <= 11.f * 11.f;
    }

    void fullContentMouseDown(const juce::MouseEvent& e)
    {
        if (!bgV2_) return;

        // Scrollbar drag — must be checked before row logic
        int sbCard = fullScrollbarCardAt(e.getPosition());
        if (sbCard >= 0)
        {
            scrollbarDragCardIndex_ = sbCard;
            scrollbarDragStartY_ = e.getPosition().y;
            scrollbarDragStartScrollY_ = fullCards_[(size_t)sbCard].scrollY;
            return;
        }

        if (fullRows_.empty()) return;
        int rowIndex = fullRowAt(e.getPosition());
        if (rowIndex < 0 || rowIndex >= (int)fullRows_.size()) return;

        const auto row = fullRows_[(size_t)rowIndex];
        int w = content_.getWidth();
        const bool hasSend = bgV2_->hasSendFromTo(row.sourceId, row.targetId);

        if (e.mods.isRightButtonDown())
        {
            if (isFullSCHit(e.getPosition(), rowIndex, w) && bgV2_->hasSidechainFromTo(row.sourceId, row.targetId))
                bgV2_->removeSidechainFrom(row.sourceId, row.targetId);
            refreshAfterFullEdit();
            return;
        }

        if (isFullSCHit(e.getPosition(), rowIndex, w))
        {
            bgV2_->handleSidechainTapFrom(row.sourceId, row.targetId);
            refreshAfterFullEdit();
            return;
        }

        if (isFullTrashHit(e.getPosition().toFloat(), rowIndex, w) && hasSend)
        {
            bgV2_->removeSendFrom(row.sourceId, row.targetId);
            refreshAfterFullEdit();
            return;
        }

        // Knob = create / toggle active / start level drag
        if (isFullKnobHit(e.getPosition(), rowIndex, w))
        {
            if (!hasSend)
            {
                bgV2_->handleTargetTapFrom(row.sourceId, row.targetId);
                refreshAfterFullEdit();
            }
            else if (bgV2_->isSendActiveFromTo(row.sourceId, row.targetId))
            {
                bgV2_->beginContinuousTopologyGesture(row.targetIsMaster ? "Adjust Master Route Level" : "Adjust Send Level");
                fullDragRow_ = rowIndex;
                fullDotClickRow_ = rowIndex;
                fullDragStartY_ = e.getPosition().y;
                fullDragStartLevel_ = bgV2_->getSendLevelFromTo(row.sourceId, row.targetId);
            }
            else
            {
                bgV2_->toggleSendActiveFrom(row.sourceId, row.targetId);
                refreshAfterFullEdit();
            }
            return;
        }
    }

    void fullContentMouseDrag(const juce::MouseEvent& e)
    {
        // Scrollbar drag
        if (scrollbarDragCardIndex_ >= 0 && scrollbarDragCardIndex_ < (int)fullCards_.size())
        {
            auto& card = fullCards_[(size_t)scrollbarDragCardIndex_];
            if (card.maxScroll > 0)
            {
                auto track = card.rowsBounds.toFloat().withTrimmedTop(4.f).withTrimmedBottom(4.f);
                const float visibleFrac = (float)card.visibleRows / (float)juce::jmax(1, card.rowCount);
                const float thumbH = juce::jmax(18.f, track.getHeight() * visibleFrac);
                const float scrollableTrack = track.getHeight() - thumbH;
                if (scrollableTrack > 0.f)
                {
                    int deltaY = e.getPosition().y - scrollbarDragStartY_;
                    int newScroll = scrollbarDragStartScrollY_ + juce::roundToInt(deltaY * (float)card.maxScroll / scrollableTrack);
                    card.scrollY = juce::jlimit(0, card.maxScroll, newScroll);
                    layoutFullCardRows(scrollbarDragCardIndex_);
                    content_.repaint(card.bounds.expanded(2));
                }
            }
            return;
        }

        if (!bgV2_ || fullDragRow_ < 0 || fullDragRow_ >= (int)fullRows_.size()) return;
        if (std::abs(e.getPosition().y - fullDragStartY_) >= 3)
            fullDotClickRow_ = -1;
        const auto& row = fullRows_[(size_t)fullDragRow_];
        float deltaYf = (float)(fullDragStartY_ - e.getPosition().y);
        bgV2_->setSendLevelFrom(row.sourceId, row.targetId, juce::jlimit(0.f, 2.f, fullDragStartLevel_ + deltaYf * 0.005f));
        content_.repaint();
        if (onSendChanged) onSendChanged();
    }

    void fullContentMouseUp(const juce::MouseEvent& e)
    {
        if (scrollbarDragCardIndex_ >= 0)
        {
            scrollbarDragCardIndex_ = -1;
            return;
        }

        if (fullDragRow_ >= 0)
        {
            const int committedRow = fullDragRow_;
            if (fullDotClickRow_ >= 0 && fullDotClickRow_ < (int)fullRows_.size())
            {
                int deltaY = std::abs(e.getPosition().y - fullDragStartY_);
                if (deltaY < 3 && bgV2_)
                {
                    const auto& row = fullRows_[(size_t)fullDotClickRow_];
                    bgV2_->toggleSendActiveFrom(row.sourceId, row.targetId);
                    refreshAfterFullEdit();
                }
            }
            if (bgV2_)
                bgV2_->commitContinuousTopologyGesture((committedRow >= 0 && committedRow < (int)fullRows_.size() && fullRows_[(size_t)committedRow].targetIsMaster)
                    ? "Adjust Master Route Level"
                    : "Adjust Send Level");
            fullDragRow_ = -1;
            fullDotClickRow_ = -1;
            content_.repaint();
        }
    }

    void refreshAfterFullEdit()
    {
        content_.repaint();
        if (onSendChanged) onSendChanged();
    }

    juce::Rectangle<float> closeBtnBounds() const
    {
        // Top-right of panel, inset for the rounded corners
        float pad = 10.f;
        return { (float)getWidth() - kCloseBtnSize - pad, pad,
                 kCloseBtnSize, kCloseBtnSize };
    }

    // ── Master crown badge ─────────────────────────────────────────────────
    // Gold vector crown (the bubblegum "👑") drawn with a Path — JUCE's
    // default font cannot render emoji glyphs reliably on every machine, so a
    // vector crown guarantees the identical look everywhere.
    static void drawCrownBadge(juce::Graphics& g, juce::Rectangle<float> r)
    {
        const float cx = r.getCentreX();
        const float cy = r.getCentreY();
        const float s  = r.getWidth() / 20.0f; // design space is 20 units wide

        // Soft dark backing disc so the badge reads against the pink header.
        g.setColour(juce::Colours::black.withAlpha(0.32f));
        g.fillEllipse(r.expanded(2.5f, 1.5f));

        // Crown silhouette (three tips with two dips between).
        juce::Path crown;
        crown.startNewSubPath(cx - 8.f * s, cy + 6.f * s);
        crown.lineTo(cx - 8.f * s, cy - 1.f * s);
        crown.lineTo(cx - 4.f * s, cy + 2.f * s);
        crown.lineTo(cx,            cy - 6.f * s);
        crown.lineTo(cx + 4.f * s,  cy + 2.f * s);
        crown.lineTo(cx + 8.f * s,  cy - 1.f * s);
        crown.lineTo(cx + 8.f * s,  cy + 6.f * s);
        crown.closeSubPath();

        // Gold body gradient (matches the premium warm-gold APEX accents).
        juce::ColourGradient gold(juce::Colour(0xFFFFE08A), cx, r.getY(),
                                  juce::Colour(0xFFE08F2E), cx, r.getBottom(), false);
        g.setGradientFill(gold);
        g.fillPath(crown);

        // Warm rim + inner sheen
        g.setColour(juce::Colour(0xFFFFF6D8).withAlpha(0.92f));
        g.strokePath(crown, juce::PathStrokeType(juce::jmax(0.8f, 1.1f * s),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Jewels on the three tips (pink — the APEX creative accent).
        const float jewel = 3.2f * s;
        const float hl    = 1.7f * s;
        g.setColour(juce::Colour(0xFFFF5FA8).withAlpha(0.95f));
        g.fillEllipse(cx - 8.f * s - jewel * 0.5f, cy - 1.f * s - jewel * 0.5f, jewel, jewel);
        g.fillEllipse(cx - jewel * 0.5f,           cy - 6.f * s - jewel * 0.5f, jewel, jewel);
        g.fillEllipse(cx + 8.f * s - jewel * 0.5f, cy - 1.f * s - jewel * 0.5f, jewel, jewel);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(cx - 8.f * s - hl * 0.5f, cy - 1.f * s - hl * 0.5f, hl, hl);
        g.fillEllipse(cx - hl * 0.5f,           cy - 6.f * s - hl * 0.5f, hl, hl);
        g.fillEllipse(cx + 8.f * s - hl * 0.5f, cy - 1.f * s - hl * 0.5f, hl, hl);
    }

    juce::Rectangle<float> eyeBtnBounds() const
    {
        float pad = 10.f;
        return { (float)getWidth() - kCloseBtnSize - kEyeBtnSize - pad - 8.f, pad,
                 kEyeBtnSize, kEyeBtnSize };
    }

    // Open/close animation
    float animPhase_   = 0.f;  // 0 = closed, 1 = open
    float animTarget_  = 0.f;
    bool  animSettled_ = false;
    bool  presentationUpdateActive_ = false;

    void requestPresentationUpdate()
    {
        if (!presentationUpdateActive_)
        {
            ApexPresentationClock::instance().requestContinuousUpdate(this);
            presentationUpdateActive_ = true;
        }
    }

    void releasePresentationUpdate()
    {
        if (presentationUpdateActive_)
        {
            ApexPresentationClock::instance().releaseContinuousUpdate(this);
            presentationUpdateActive_ = false;
        }
    }

    void visibilityChanged() override
    {
        if (!isShowing())
            releasePresentationUpdate();
        else if (std::abs(animPhase_ - animTarget_) >= 0.01f)
            requestPresentationUpdate();
    }

    void onPresentationTick(double deltaSeconds) override
    {
        if (!isShowing())
        {
            releasePresentationUpdate();
            return;
        }

        // Animation interpolation
        constexpr float speed = 0.14f;
        const float frameScale = (float) juce::jlimit(0.0, 4.0, deltaSeconds * 60.0);
        const float blend = 1.0f - std::pow(1.0f - speed, frameScale);
        animPhase_ += (animTarget_ - animPhase_) * blend;

        bool animDone = std::abs(animPhase_ - animTarget_) < 0.01f;
        if (animDone)
        {
            animPhase_ = animTarget_;
            if (animTarget_ < 0.5f)
            {
                setVisible(false);
                setTransform({});
                setAlpha(1.f);
                animSettled_ = false;
                releasePresentationUpdate();
                return;
            }
            // Animation complete, open state. Content refresh is event-driven;
            // no settled presentation polling is needed.
            if (!animSettled_)
            {
                setTransform({});
                setAlpha(1.f);
                animSettled_ = true;
            }
            releasePresentationUpdate();
        }
        else
        {
            animSettled_ = false;
            // Still animating — apply scale + opacity
            setAlpha(animPhase_);
            auto s = 0.92f + 0.08f * animPhase_;
            setTransform(juce::AffineTransform::scale(s, s,
                (float)getWidth() * 0.5f, (float)getHeight()));
            repaint();
        }
    }

    // Scrollable content area
    struct ContentArea : public juce::Component
    {
        BubblegumV2PanelUI* owner = nullptr;
        explicit ContentArea(BubblegumV2PanelUI* o) : owner(o)
        {
            setInterceptsMouseClicks(true, true);
        }
        void paint(juce::Graphics& g) override { if (owner) owner->paintContent(g, getWidth(), getHeight()); }
        void mouseDown(const juce::MouseEvent& e) override { if (owner) owner->contentMouseDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { if (owner) owner->contentMouseDrag(e); }
        void mouseUp(const juce::MouseEvent& e) override { if (owner) owner->contentMouseUp(e); }
        void mouseMove(const juce::MouseEvent& e) override { if (owner) owner->contentMouseMove(e); }
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override { if (owner) owner->contentMouseWheelMove(e, wheel); }
        void mouseExit(const juce::MouseEvent&) override { if (owner) owner->contentMouseExit(); }
    };

    ContentArea    content_{ this };
    juce::Viewport viewport_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumV2PanelUI)
};

} // namespace DAW
