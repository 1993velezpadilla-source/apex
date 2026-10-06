// ===========================================================================
// SelectedTrackPeakBubbleComponent.h  -  TrackLens
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "../MeteringCore/TrackPeakMeterManagerCore.h"
#include "../StateCore/ApplicationState.h"
#include <functional>

namespace DAW {

class SelectedTrackPeakBubbleComponent
    : public juce::Component,
      private juce::Timer,
      private juce::Value::Listener
{
public:
    static constexpr bool kEnabled = true;
    void setCloseToBubble(bool enabled) noexcept { closeToBubble_ = enabled; }
    void requestShowForCurrentSelection() { showRequested_ = true; setVisible(true); toFront(false); }
    static constexpr float kRadius = 10.f;
    static int preferredWidth()      { return 160; }
    static int preferredHeight()     { return 160; }
    static int minimizedDiameter()   { return 48; }

    bool wasDraggedByUser() const noexcept { return dragged_; }

    void setMeterManager(TrackPeakMeterManagerCore* mgr) noexcept { manager_ = mgr; }

    void setApplicationState(ApplicationState* st)
    {
        if (appState_) appState_->selectedTrackID.removeListener(this);
        appState_ = st;
        if (appState_)
        {
            appState_->selectedTrackID.addListener(this);
            onSelectedTrackChanged(appState_->selectedTrackID.getValue().toString());
        }
    }

    void setTrackNameResolver(std::function<juce::String(const juce::String&)> fn)
    {
        trackNameResolver_ = std::move(fn);
    }

    // ---- Mouse -----------------------------------------------------------
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) { showResetMenu(); return; }
        // Minimize button hit-test
        if (minimizeBtnBounds_.contains(e.position.toInt())) { toggleMinimized(); return; }
        // CLIP badge reset
        if (!minimized_ && clipBadgeBounds_.contains(e.position.toInt())) { resetHold(); return; }
        dragStart_ = e.getScreenPosition() - getBoundsInParent().getPosition();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) return;
        dragged_ = true;
        if (auto* p = getParentComponent())
        {
            auto np = e.getScreenPosition() - dragStart_;
            auto pb = p->getLocalBounds();
            setTopLeftPosition(
                juce::jlimit(0, juce::jmax(0, pb.getWidth()  - getWidth()),  np.x),
                juce::jlimit(0, juce::jmax(0, pb.getHeight() - getHeight()), np.y));
        }
    }

    void mouseDoubleClick(const juce::MouseEvent&) override { resetHold(); }

    ~SelectedTrackPeakBubbleComponent() override
    {
        stopTimer();
        if (appState_) appState_->selectedTrackID.removeListener(this);
    }

    void visibilityChanged() override
    {
        if constexpr (!kEnabled) { stopTimer(); return; }
        if (isVisible()) startTimerHz(60);
        else             stopTimer();
    }

    // ---- Paint -----------------------------------------------------------
    void paint(juce::Graphics& g) override
    {
        if constexpr (!kEnabled) { juce::ignoreUnused(g); return; }
        const auto b = getLocalBounds().toFloat();

        if (minimized_)
        {
            paintMinimized(g, b);
            return;
        }

        // Shadow
        {
            juce::Path sh;
            sh.addRoundedRectangle(b.expanded(1.f).translated(0.f, 4.f), kRadius);
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.fillPath(sh);
        }

        // Body
        {
            juce::ColourGradient grad(
                juce::Colour(0xFF1E1E2C), 0.f, b.getY(),
                juce::Colour(0xFF0A0A12), 0.f, b.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(b, kRadius);
        }

        // Border
        const bool isClip = snap_.clipped;
        g.setColour(isClip ? juce::Colour(0xFFFF2244)
                           : juce::Colour(0xFFE84393).withAlpha(0.80f));
        g.drawRoundedRectangle(b.reduced(0.8f), kRadius, isClip ? 2.2f : 1.4f);

        // Gloss
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRoundedRectangle(b.reduced(2.f).withHeight(b.getHeight() * 0.35f), kRadius - 1.f);

        const float pad = 10.f;
        float y = b.getY() + 8.f;

        // ---- Header row --------------------------------------------------
        // Minimize button (top-right pill)
        const float btnW = 18.f, btnH = 12.f;
        minimizeBtnBounds_ = juce::Rectangle<int>(
            (int)(b.getRight() - pad - btnW), (int)y, (int)btnW, (int)btnH);
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.fillRoundedRectangle(minimizeBtnBounds_.toFloat(), 4.f);
        g.setColour(juce::Colours::white.withAlpha(0.60f));
        g.setFont(juce::Font("Segoe UI", 7.f, juce::Font::bold));
        g.drawText("x", minimizeBtnBounds_, juce::Justification::centred);

        g.setFont(juce::Font("Segoe UI", 8.f, juce::Font::bold));
        g.setColour(juce::Colour(0xFFE84393).withAlpha(0.70f));
        g.drawText("TRACKLENS", (int)pad, (int)y, 64, 13, juce::Justification::centredLeft);
        y += 15.f;

        // Track name
        const juce::String name = trackName_.isEmpty() ? activeTrackId_ : trackName_;
        g.setFont(juce::Font("Segoe UI", 13.f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.drawText(name, (int)pad, (int)y, (int)(b.getWidth() - pad * 2.f), 18,
                   juce::Justification::centredLeft, true);
        y += 20.f;

        // Separator
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRect(pad, y, b.getWidth() - pad * 2.f, 1.f);
        y += 5.f;

        // ---- Meter rows --------------------------------------------------
        drawMeterRow(g, "TP",   snap_.truePeakDb,   pad, y, b.getWidth()); y += 22.f;
        drawMeterRow(g, "PK",   snap_.samplePeakDb, pad, y, b.getWidth()); y += 22.f;
        drawMeterRow(g, "HOLD", snap_.peakHoldDb,   pad, y, b.getWidth()); y += 24.f;

        // ---- CLIP badge (clickable reset) --------------------------------
        clipBadgeBounds_ = juce::Rectangle<int>();
        if (snap_.clipped)
        {
            juce::Rectangle<float> badge(pad, y, 48.f, 16.f);
            clipBadgeBounds_ = badge.toNearestInt();
            g.setColour(juce::Colour(0xFFFF2244).withAlpha(0.90f));
            g.fillRoundedRectangle(badge, 4.f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font("Segoe UI", 9.f, juce::Font::bold));
            g.drawText("CLIP", badge, juce::Justification::centred);
            y += 18.f;
        }

        // ---- Mini bar meters ---------------------------------------------
        const float barMaxH = b.getBottom() - y - 6.f;
        if (barMaxH > 4.f)
        {
            const float bw   = 5.f;
            const float bx1  = b.getRight() - pad - bw * 2.f - 4.f;
            const float bx2  = bx1 + bw + 4.f;
            drawMiniBar(g, bx1, y, bw, barMaxH, dbToNorm(snap_.truePeakDb),   juce::Colour(0xFFE84393));
            drawMiniBar(g, bx2, y, bw, barMaxH, dbToNorm(snap_.samplePeakDb), juce::Colour(0xFF00D4CC));
            if (snap_.peakHoldDb > -120.f)
            {
                const float ty = y + barMaxH - dbToNorm(snap_.peakHoldDb) * barMaxH;
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.fillRect(bx2, ty, bw, 1.8f);
            }
        }
    }

    void resized() override {}

private:
    // ---- Minimized orb ---------------------------------------------------
    void paintMinimized(juce::Graphics& g, const juce::Rectangle<float>& b)
    {
        const bool isClip = snap_.clipped;
        const float lvl = dbToNorm(snap_.truePeakDb);
        const juce::Colour fill = isClip
            ? juce::Colour(0xFFFF2244)
            : juce::Colour(0xFFE84393).interpolatedWith(juce::Colour(0xFF00D4CC), lvl);

        juce::Path sh;
        sh.addEllipse(b.expanded(1.f).translated(0.f, 3.f));
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillPath(sh);

        juce::ColourGradient grad(fill.brighter(0.3f), b.getCentreX(), b.getY(),
                                   fill.darker(0.3f),  b.getCentreX(), b.getBottom(), false);
        g.setGradientFill(grad);
        g.fillEllipse(b.reduced(1.f));

        g.setColour(fill.brighter(0.5f).withAlpha(0.70f));
        g.drawEllipse(b.reduced(1.5f), 1.5f);

        // "TL" label
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.setFont(juce::Font("Segoe UI", 11.f, juce::Font::bold));
        g.drawText("TL", b, juce::Justification::centred);

        minimizeBtnBounds_ = b.toNearestInt();
    }

    void toggleMinimized()
    {
        if (closeToBubble_)
        {
            minimized_ = !minimized_;
            const int w = minimized_ ? minimizedDiameter() : preferredWidth();
            const int h = minimized_ ? minimizedDiameter() : preferredHeight();
            setSize(w, h);
            repaint();
        }
        else
        {
            minimized_ = false;
            setVisible(false);
            stopTimer();
        }
    }

    // ---- Value::Listener -------------------------------------------------
    void valueChanged(juce::Value& v) override
    {
        if constexpr (!kEnabled) return;
        if (appState_ && &v == &appState_->selectedTrackID)
            onSelectedTrackChanged(v.getValue().toString());
    }

    void onSelectedTrackChanged(const juce::String& newId)
    {
        if constexpr (!kEnabled) { juce::ignoreUnused(newId); setVisible(false); return; }
        if (newId == activeTrackId_) return;
        activeTrackId_ = newId;
        snap_    = TrackPeakMeterStateCore::Snapshot{};
        clipped_ = false;
        if (trackNameResolver_ && newId.isNotEmpty())
            trackName_ = trackNameResolver_(newId);
        else
            trackName_ = {};
        const bool show = showRequested_ && newId.isNotEmpty();
        setVisible(show);
        if (show) { toFront(false); startTimerHz(60); }
        repaint();
    }

    // ---- Timer -----------------------------------------------------------
    void timerCallback() override
    {
        if constexpr (!kEnabled) return;
        if (manager_ == nullptr) return;
        if (appState_)
        {
            const auto cur = appState_->selectedTrackID.getValue().toString();
            if (cur != activeTrackId_) onSelectedTrackChanged(cur);
        }
        if (activeTrackId_.isEmpty()) return;
        auto newSnap = manager_->getMeterSnapshot(activeTrackId_);
        const bool changed = (newSnap.samplePeakDb != snap_.samplePeakDb)
                          || (newSnap.truePeakDb   != snap_.truePeakDb)
                          || (newSnap.peakHoldDb   != snap_.peakHoldDb)
                          || (newSnap.clipped      != snap_.clipped);
        snap_    = newSnap;
        clipped_ = snap_.clipped;
        if (changed && isShowing()) repaint();
    }

    // ---- Actions ---------------------------------------------------------
    void resetHold()
    {
        if (manager_ && activeTrackId_.isNotEmpty())
        {
            manager_->resetPeakHold(activeTrackId_);
            snap_ = manager_->getMeterSnapshot(activeTrackId_);
            repaint();
        }
    }

    void showResetMenu()
    {
        juce::PopupMenu m;
        m.addItem(1, "Reset Peak Hold");
        juce::WeakReference<juce::Component> weak(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [weak](int r)
        {
            if (r == 1)
                if (auto* s = dynamic_cast<SelectedTrackPeakBubbleComponent*>(weak.get()))
                    s->resetHold();
        });
    }

    // ---- Draw helpers ----------------------------------------------------
    static float dbToNorm(float db) noexcept
    {
        return juce::jlimit(0.0f, 1.0f, (db + 60.f) / 66.f);
    }

    static juce::Colour dbColour(float db) noexcept
    {
        if (db >= 0.f)   return juce::Colour(0xFFFF2244);
        if (db >= -3.f)  return juce::Colour(0xFFFFAA00);
        if (db >= -18.f) return juce::Colour(0xFF00D4CC);
        return juce::Colour(0xFF6699CC);
    }

    void drawMeterRow(juce::Graphics& g, const char* label,
                      float db, float x, float y, float totalW)
    {
        // Label
        g.setFont(juce::Font("Segoe UI", 10.f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText(label, (int)x, (int)y, 36, 18, juce::Justification::centredLeft);
        // Value
        const juce::String val = (db <= -119.f)
            ? juce::String("---")
            : (juce::String(db, 1) + " dB");
        g.setFont(juce::Font("Segoe UI", 14.f, juce::Font::bold));
        g.setColour(dbColour(db));
        g.drawText(val, (int)(x + 38.f), (int)y,
                   (int)(totalW - x - 44.f), 18,
                   juce::Justification::centredLeft);
    }

    void drawMiniBar(juce::Graphics& g, float x, float y,
                     float w, float maxH, float norm, juce::Colour col)
    {
        g.setColour(juce::Colours::black.withAlpha(0.40f));
        g.fillRect(x, y, w, maxH);
        const float filled = norm * maxH;
        g.setColour(col.withAlpha(0.85f));
        g.fillRect(x, y + maxH - filled, w, filled);
    }

    // ---- Members ---------------------------------------------------------
    TrackPeakMeterManagerCore*  manager_  = nullptr;
    ApplicationState*           appState_ = nullptr;
    std::function<juce::String(const juce::String&)> trackNameResolver_;

    juce::String activeTrackId_;
    juce::String trackName_;
    TrackPeakMeterStateCore::Snapshot snap_;
    bool clipped_   = false;
    bool minimized_ = false;
    bool closeToBubble_ = true;
    bool showRequested_ = false;

    juce::Point<int>      dragStart_;
    bool                  dragged_ = false;
    mutable juce::Rectangle<int> minimizeBtnBounds_;
    mutable juce::Rectangle<int> clipBadgeBounds_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(SelectedTrackPeakBubbleComponent)
};

} // namespace DAW
