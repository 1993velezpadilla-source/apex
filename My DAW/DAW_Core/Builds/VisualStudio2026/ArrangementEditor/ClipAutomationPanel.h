// ===========================================================================
// ClipAutomationPanel.h
// "Automate This Clip" floating panel — opened from clip right-click menu
// or via the AUTOMATE button on the Clip Properties window.
//
// Premium dark-glass design matching ClipFxFloatingPanel.
// Shows every automation target for the clip:
//   Track:  Volume, Pan, Tape Stop
//   Clip:   Pitch, Stretch
// Each row has Create / Show / Hide / Clear action buttons.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include "../../../Source/AutomationCore/AutomationLaneCore.h"
#include "../../../Source/AutomationCore/AutomationQuickCreateCore.h"
#include "../../../Source/AutomationCore/AutomationManagerCore.h"
#include <functional>
#include <vector>

namespace ArrangementEditor
{

// ===========================================================================
// Design tokens
// ===========================================================================
namespace AMP {
    inline juce::Colour C(uint32_t argb) { return juce::Colour(argb); }
    constexpr uint32_t kBg       = 0xFF0C0D10;
    constexpr uint32_t kSurface  = 0xFF181920;
    constexpr uint32_t kSurface2 = 0xFF202128;
    constexpr uint32_t kBorder   = 0xFF2A2B34;
    constexpr uint32_t kAccent   = 0xFFFFB347;   // warm amber/gold — automation identity
    constexpr uint32_t kGreen    = 0xFF52E09A;
    constexpr uint32_t kPink     = 0xFFFF6B9D;
    constexpr uint32_t kBlue     = 0xFF5BC8FF;
    constexpr uint32_t kRed      = 0xFFFF5555;
    constexpr uint32_t kText     = 0xFFE8E8EC;
    constexpr uint32_t kTextDim  = 0xFF666672;
}

// ---------------------------------------------------------------------------
// Callbacks provided by ArrangementViewCore
// ---------------------------------------------------------------------------
struct ClipAutomationPanelCallbacks
{
    std::function<void(const DAW::AutomationQuickCreateCore::ControlTarget&)> createLane;
    std::function<void(const DAW::AutomationQuickCreateCore::ControlTarget&)> showLane;
    std::function<void(const DAW::AutomationQuickCreateCore::ControlTarget&)> hideLane;
    std::function<void(const DAW::AutomationQuickCreateCore::ControlTarget&)> clearLane;
    std::function<bool(const DAW::TrackID&, const juce::String&)> laneExists;
};

// ===========================================================================
// AutomationParamRow — one parameter row inside the floating panel
// ===========================================================================
class AutomationParamRow : public juce::Component
{
public:
    AutomationParamRow() : juce::Component() {}

    juce::String displayName;
    DAW::AutomationQuickCreateCore::ControlTarget target;
    bool laneExists = false;

    std::function<void()> onCreate;
    std::function<void()> onShow;
    std::function<void()> onHide;
    std::function<void()> onClear;

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const bool hot = isMouseOver(true);

        // row bg
        g.setColour(AMP::C(hot ? AMP::kSurface2 : AMP::kSurface).withAlpha(0.85f));
        g.fillRoundedRectangle(b.reduced(2.f, 1.5f), 4.f);
        g.setColour(AMP::C(AMP::kBorder).withAlpha(laneExists ? 0.5f : 0.25f));
        g.drawRoundedRectangle(b.reduced(2.f, 1.5f), 4.f, 0.8f);

        // left accent bar if lane exists
        if (laneExists)
        {
            g.setColour(AMP::C(AMP::kGreen).withAlpha(0.7f));
            g.fillRoundedRectangle(b.reduced(2.f, 1.5f).withWidth(2.5f), 1.2f);
        }

        // name
        g.setColour(AMP::C(laneExists ? AMP::kText : 0xFFCCCCD4));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f).withStyle("Bold")));
        g.drawText(displayName.toUpperCase(), 14, 0, getWidth() - 14 - 176, getHeight(),
                   juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        // Lay out four action buttons on the right
        const int h = getHeight();
        const int bH = h - 8;
        const int bY = 4;
        const int bW = 40;
        const int gap = 4;
        int x = getWidth() - (bW * 4 + gap * 3) - 6;
        if (createBtn_) createBtn_->setBounds(x, bY, bW, bH); x += bW + gap;
        if (showBtn_)   showBtn_->setBounds(x, bY, bW, bH);   x += bW + gap;
        if (hideBtn_)   hideBtn_->setBounds(x, bY, bW, bH);   x += bW + gap;
        if (clearBtn_)  clearBtn_->setBounds(x, bY, bW, bH);
    }

    // Build child buttons after construction
    void buildButtons(juce::Component& parent)
    {
        auto make = [&](const juce::String& txt, uint32_t bgCol) -> juce::Component*
        {
            auto* b = new PremiumBtn(txt, bgCol);
            parent.addAndMakeVisible(b);
            return b;
        };

        createBtn_.reset(static_cast<PremiumBtn*>(make("CREATE", AMP::kGreen)));
        showBtn_  .reset(static_cast<PremiumBtn*>(make("SHOW",   AMP::kBlue)));
        hideBtn_  .reset(static_cast<PremiumBtn*>(make("HIDE",   AMP::kTextDim)));
        clearBtn_ .reset(static_cast<PremiumBtn*>(make("CLEAR",  AMP::kRed)));

        createBtn_->onClick = [this] { if (onCreate) onCreate(); };
        showBtn_->onClick   = [this] { if (onShow)   onShow();   };
        hideBtn_->onClick   = [this] { if (onHide)   onHide();   };
        clearBtn_->onClick  = [this] { if (onClear)  onClear();  };

        updateButtonStates();
    }

    void updateButtonStates()
    {
        if (createBtn_) { createBtn_->setAlpha(laneExists ? 0.45f : 1.0f); }
        if (showBtn_)   { showBtn_->setEnabled(laneExists);  showBtn_->setAlpha(laneExists ? 1.0f : 0.38f); }
        if (hideBtn_)   { hideBtn_->setEnabled(laneExists);  hideBtn_->setAlpha(laneExists ? 1.0f : 0.38f); }
        if (clearBtn_)  { clearBtn_->setEnabled(laneExists); clearBtn_->setAlpha(laneExists ? 1.0f : 0.38f); }
        repaint();
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    // ── Micro action button ──────────────────────────────────────────────
    struct PremiumBtn : public juce::Component
    {
        juce::String label;
        uint32_t col;
        std::function<void()> onClick;
        bool enabled_ = true;

        PremiumBtn(const juce::String& l, uint32_t c) : label(l), col(c) {}

        void setEnabled(bool e) { enabled_ = e; repaint(); }

        void paint(juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced(0.5f);
            const bool hot = isMouseOver() && enabled_;
            g.setColour(AMP::C(col).withAlpha(hot ? 0.32f : 0.15f));
            g.fillRoundedRectangle(b, b.getHeight() * 0.45f);
            g.setColour(AMP::C(col).withAlpha(hot ? 0.90f : 0.52f));
            g.drawRoundedRectangle(b, b.getHeight() * 0.45f, 0.9f);
            g.setColour(AMP::C(hot ? AMP::kText : (enabled_ ? AMP::kText : AMP::kTextDim)));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.5f).withStyle("Bold")));
            g.drawText(label, getLocalBounds(), juce::Justification::centred, false);
        }
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDown (const juce::MouseEvent&) override { if (enabled_ && onClick) onClick(); }
        void mouseUp   (const juce::MouseEvent&) override { repaint(); }
    };

    std::unique_ptr<PremiumBtn> createBtn_, showBtn_, hideBtn_, clearBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationParamRow)
};

// ===========================================================================
// ClipAutomateFloatingPanel — premium floating panel (FX-panel style)
// ===========================================================================
class ClipAutomateFloatingPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kPanelWidth     = 340;
    static constexpr int kHeaderHeight   = 36;
    static constexpr int kRowHeight      = 32;
    static constexpr int kFooterPad      = 10;

    std::function<void()> onClose;

    ClipAutomateFloatingPanel()
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, true);
    }

    ~ClipAutomateFloatingPanel() override = default;

    /** Engine/device sample rate for seconds<->samples conversions (pushed
        by the owning view; 44100 default is the legacy fallback). */
    void setEngineSampleRate (double sr) { engineSampleRate_ = (sr > 0.0) ? sr : 44100.0; }

    void setup(const ArrangementClipModel&        clip,
               const DAW::TrackID&                trackId,
               const ClipAutomationPanelCallbacks& cb,
               const juce::String&                explicitClipId = {})
    {
        clip_          = clip;
        trackId_       = trackId;
        cb_            = cb;
        explicitClipId_= explicitClipId;
        rebuildRows();
    }

    void show(juce::Point<int> anchorPos)
    {
        anchorPos_      = anchorPos;
        visible_        = true;
        animPhase_      = 0.0f;
        setVisible(true);
        startTimerHz(60);
    }

    void hide()
    {
        visible_   = false;
        animPhase_ = 1.0f;
        startTimerHz(60);
    }

    bool isShowing() const noexcept { return visible_ && animPhase_ >= 0.99f; }

    void refreshStates()
    {
        for (auto& r : paramRows_)
        {
            r->laneExists = cb_.laneExists
                ? cb_.laneExists(r->target.trackId, r->target.parameterId)
                : false;
            r->updateButtonStates();
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float a = juce::jlimit(0.f, 1.f, animPhase_);

        // Drop shadow
        juce::Path shadow;
        shadow.addRoundedRectangle(b.reduced(4.f), 10.f);
        g.setColour(juce::Colours::black.withAlpha(0.45f * a));
        for (int i = 0; i < 4; ++i)
            g.fillPath(shadow, juce::AffineTransform::translation(0.f, (float)i));

        // Main body gradient
        juce::ColourGradient body(AMP::C(AMP::kSurface).withAlpha(0.98f * a), b.getX(), b.getY(),
                                   AMP::C(AMP::kBg).withAlpha(0.98f * a), b.getX(), b.getBottom(), false);
        g.setGradientFill(body);
        g.fillRoundedRectangle(b, 10.f);

        // Amber accent border
        g.setColour(AMP::C(AMP::kAccent).withAlpha(0.55f * a));
        g.drawRoundedRectangle(b, 10.f, 1.2f);

        // Top amber glow strip
        juce::ColourGradient topGlow(AMP::C(AMP::kAccent).withAlpha(0.18f * a), b.getX(), b.getY(),
                                      juce::Colours::transparentBlack, b.getX(), b.getY() + 32.f, false);
        g.setGradientFill(topGlow);
        g.fillRoundedRectangle(b.withHeight(32.f), 10.f);

        // Idle shimmer
        const float shimX = std::fmod(idlePhase_ * 44.f, b.getWidth() + 80.f) - 40.f;
        juce::ColourGradient shimmer(juce::Colours::transparentBlack, shimX - 36.f, 0.f,
                                     AMP::C(AMP::kAccent).withAlpha(0.08f * a), shimX, 0.f, false);
        shimmer.addColour(1.0, juce::Colours::transparentBlack);
        g.setGradientFill(shimmer);
        g.fillRoundedRectangle(b.reduced(1.f), 10.f);

        // Header text
        auto hdr = juce::Rectangle<float>(b.getX(), b.getY(), b.getWidth(), (float)kHeaderHeight);
        g.setColour(AMP::C(AMP::kAccent).withAlpha(a));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.f).withStyle("Bold")));
        g.drawText("AUTOMATE CLIP", hdr.reduced(14.f, 0.f).withTrimmedRight(40.f),
                   juce::Justification::centredLeft);
        g.setColour(AMP::C(AMP::kTextDim).withAlpha(0.75f * a));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.f)));
        g.drawText(juce::String(paramRows_.size()) + " params",
                   hdr.reduced(14.f, 0.f).withTrimmedRight(22.f), juce::Justification::centredRight);

        // Header separator
        g.setColour(AMP::C(AMP::kBorder).withAlpha(0.6f * a));
        g.fillRect(juce::Rectangle<float>(b.getX(), hdr.getBottom() - 1.f, b.getWidth(), 1.f));

        // Close button
        auto cl = closeButtonBounds().toFloat();
        g.setColour(AMP::C(AMP::kPink).withAlpha(closeHot_ ? 0.30f : 0.15f));
        g.fillEllipse(cl);
        g.setColour(AMP::C(AMP::kPink).withAlpha(closeHot_ ? 1.0f : 0.75f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.f).withStyle("Bold")));
        g.drawText("x", cl, juce::Justification::centred);
    }

    void resized() override
    {
        int y = kHeaderHeight + 6;
        for (auto& r : paramRows_)
        {
            r->setBounds(6, y, getWidth() - 12, kRowHeight);
            y += kRowHeight + 3;
        }
    }

    int preferredHeight() const noexcept
    {
        return kHeaderHeight + 6 + (int)paramRows_.size() * (kRowHeight + 3) + kFooterPad;
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool now = closeButtonBounds().contains(e.position.toInt());
        if (now != closeHot_) { closeHot_ = now; repaint(); }
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (closeHot_) { closeHot_ = false; repaint(); }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (closeButtonBounds().contains(e.position.toInt()))
        {
            if (onClose) onClose();
        }
    }

private:
    void timerCallback() override
    {
        if (visible_)
        {
            animPhase_ += 0.15f;
            if (animPhase_ >= 1.0f) animPhase_ = 1.0f;
        }
        else
        {
            animPhase_ -= 0.18f;
            if (animPhase_ <= 0.0f)
            {
                animPhase_ = 0.0f;
                setVisible(false);
                stopTimer();
            }
        }
        idlePhase_ += 0.022f;
        if (idlePhase_ > juce::MathConstants<float>::twoPi)
            idlePhase_ -= juce::MathConstants<float>::twoPi;
        const float ease = animPhase_ * animPhase_ * (3.0f - 2.0f * animPhase_);
        setAlpha(ease);
        repaint();
    }

    juce::Rectangle<int> closeButtonBounds() const noexcept
    {
        return { getWidth() - 24, 10, 14, 14 };
    }

    void rebuildRows()
    {
        paramRows_.clear();

        const DAW::TrackID effectiveTrackId = trackId_.isEmpty()
            ? DAW::TrackID("track_" + juce::String(clip_.trackIndex))
            : trackId_;

        const juce::String clipIdForAutomation = explicitClipId_.isNotEmpty()
            ? explicitClipId_ : clip_.id.toString();

        // Build targets
        std::vector<DAW::AutomationQuickCreateCore::ControlTarget> targets;
        auto addT = [&](DAW::AutomationQuickCreateCore::ControlTarget t)
        {
            t.regionStartSample   = (int64_t)(clip_.startTime * engineSampleRate_);
            t.regionLengthSamples = (int64_t)(clip_.length    * engineSampleRate_);
            t.sampleRate          = engineSampleRate_;
            targets.push_back(std::move(t));
        };
        addT(DAW::AutomationQuickCreateCore::makeTrackVolumeTarget(effectiveTrackId));
        addT(DAW::AutomationQuickCreateCore::makeTrackPanTarget(effectiveTrackId));
        addT(DAW::AutomationQuickCreateCore::makeClipTapeStopTarget(effectiveTrackId, clipIdForAutomation));
        addT(DAW::AutomationQuickCreateCore::makeClipPitchTarget(effectiveTrackId, clipIdForAutomation));

        for (const auto& tgt : targets)
        {
            auto row = std::make_unique<AutomationParamRow>();
            row->displayName = tgt.displayName;
            row->target      = tgt;
            row->laneExists  = cb_.laneExists
                ? cb_.laneExists(tgt.trackId, tgt.parameterId) : false;

            const DAW::AutomationQuickCreateCore::ControlTarget capT = tgt;

            row->onCreate = [this, capT]() mutable
            {
                if (cb_.createLane) cb_.createLane(capT);
                refreshStates();
            };
            row->onShow = [this, capT]() mutable { if (cb_.showLane) cb_.showLane(capT); };
            row->onHide = [this, capT]() mutable { if (cb_.hideLane) cb_.hideLane(capT); };
            row->onClear = [this, capT]() mutable
            {
                if (cb_.clearLane) cb_.clearLane(capT);
                refreshStates();
            };

            row->buildButtons(*this);
            addAndMakeVisible(*row);
            paramRows_.push_back(std::move(row));
        }

        const int ph = preferredHeight();
        setSize(kPanelWidth, ph);
        resized();
    }

    ArrangementClipModel clip_;
    DAW::TrackID         trackId_;
    ClipAutomationPanelCallbacks cb_;
    juce::String         explicitClipId_;
    juce::Point<int>     anchorPos_;
    double               engineSampleRate_ = 44100.0;
    std::vector<std::unique_ptr<AutomationParamRow>> paramRows_;
    float animPhase_ = 0.f;
    float idlePhase_ = 0.f;
    bool  visible_   = false;
    bool  closeHot_  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAutomateFloatingPanel)
};

// ===========================================================================
// ClipAutomateCableOverlay — amber cable connecting Automate button to panel
// ===========================================================================
class ClipAutomateCableOverlay : public juce::Component
{
public:
    ClipAutomateCableOverlay()
    {
        setOpaque(false);
        setInterceptsMouseClicks(false, false);
    }

    void setCable(juce::Point<int> panelAnchor, juce::Point<int> buttonAnchor, float alpha)
    {
        prevPanelAnchor_ = panelAnchor_;
        panelAnchor_     = panelAnchor;
        buttonAnchor_    = buttonAnchor;
        alpha_           = juce::jlimit(0.f, 1.f, alpha);
        const float dx   = (float)(panelAnchor_.x - prevPanelAnchor_.x);
        const float dy   = (float)(panelAnchor_.y - prevPanelAnchor_.y);
        tension_         = juce::jlimit(0.f, 1.f, tension_ + std::sqrt(dx*dx + dy*dy) * 0.035f);
        repaint();
    }

    void advanceAnimation()
    {
        flowPhase_ += 0.18f;
        tension_   *= 0.975f;
        if (flowPhase_ > juce::MathConstants<float>::twoPi * 8.f)
            flowPhase_ = 0.f;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (alpha_ <= 0.01f) return;

        auto start = getLocalPoint(getParentComponent(), panelAnchor_).toFloat();
        auto end   = getLocalPoint(getParentComponent(), buttonAnchor_).toFloat();

        juce::Path cable;
        cable.startNewSubPath(start);
        const float ctrl  = juce::jmax(70.f, std::abs(end.x - start.x) * 0.48f);
        const float sag   = (22.f + 55.f * tension_) * std::sin(flowPhase_ * 0.72f + tension_ * 2.4f);
        const float snap  = tension_ * 68.f;
        cable.cubicTo({ start.x + ctrl, start.y + sag },
                      { end.x - ctrl,   end.y - sag * 0.35f + snap }, end);

        // Glow halo
        g.setColour(AMP::C(AMP::kAccent).withAlpha(0.18f * alpha_));
        g.strokePath(cable, juce::PathStrokeType(8.f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        // Core cable
        juce::ColourGradient grad(AMP::C(AMP::kAccent).withAlpha(0.95f * alpha_), start.x, start.y,
                                   AMP::C(AMP::kAccent).brighter(0.35f).withAlpha(0.95f * alpha_),
                                   end.x, end.y, false);
        g.setGradientFill(grad);
        g.strokePath(cable, juce::PathStrokeType(3.f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        // Specular highlight
        g.setColour(juce::Colours::white.withAlpha(0.30f * alpha_));
        g.strokePath(cable, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Travelling pulse dot
        const float t   = std::fmod(flowPhase_ * 0.12f, 1.0f);
        const float pulse = 0.5f + 0.5f * std::sin(flowPhase_);
        juce::Point<float> pp = cable.getPointAlongPath(cable.getLength() * t);
        g.setColour(AMP::C(AMP::kAccent).brighter(0.45f).withAlpha((0.35f + 0.35f * pulse) * alpha_));
        g.fillEllipse(pp.x - 7.f, pp.y - 7.f, 14.f, 14.f);

        // End-point anchor dots
        for (auto pt : { start, end })
        {
            g.setColour(AMP::C(AMP::kAccent).withAlpha(0.30f * alpha_));
            g.fillEllipse(pt.x - 9.f, pt.y - 9.f, 18.f, 18.f);
            g.setColour(AMP::C(AMP::kAccent).withAlpha(0.95f * alpha_));
            g.fillEllipse(pt.x - 4.f, pt.y - 4.f, 8.f, 8.f);
        }
    }

private:
    juce::Point<int> panelAnchor_, prevPanelAnchor_, buttonAnchor_;
    float alpha_     = 0.f;
    float flowPhase_ = 0.f;
    float tension_   = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAutomateCableOverlay)
};

// ===========================================================================
// ClipAutomationPanel — legacy inline panel (kept for embedded / context-menu
// usage from ArrangementViewCore without the Properties window)
// ===========================================================================
class ClipAutomationPanel : public juce::Component
{
public:
    ClipAutomationPanel(const ArrangementClipModel&          clip,
                        const DAW::TrackID&                  trackId,
                        const ClipAutomationPanelCallbacks&  callbacks,
                        bool                                 embedded = false,
                        const juce::String&                  explicitClipId = {})
        : clip_(clip), trackId_(trackId), cb_(callbacks), embedded_(embedded), explicitClipId_(explicitClipId)
    {
        setName("Automate: " + juce::String(clip.clipName));
        buildTargets();
        buildRows();
        setSize(kPanelWidth, preferredHeight());
    }

    ~ClipAutomationPanel() override = default;

    std::function<void()> onClose;

    /** Engine/device sample rate for seconds<->samples conversions (pushed
        by the owning view; 44100 default is the legacy fallback). */
    void setEngineSampleRate (double sr) { engineSampleRate_ = (sr > 0.0) ? sr : 44100.0; }

    int getPreferredHeight() const noexcept { return preferredHeight(); }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        // Premium glass body
        juce::ColourGradient body(AMP::C(AMP::kSurface).withAlpha(0.98f), b.getX(), b.getY(),
                                   AMP::C(AMP::kBg).withAlpha(0.98f), b.getX(), b.getBottom(), false);
        g.setGradientFill(body);
        g.fillRoundedRectangle(b, 10.f);

        // Amber border
        g.setColour(AMP::C(AMP::kAccent).withAlpha(0.55f));
        g.drawRoundedRectangle(b, 10.f, 1.2f);

        // Top amber glow
        juce::ColourGradient topGlow(AMP::C(AMP::kAccent).withAlpha(0.16f), b.getX(), b.getY(),
                                      juce::Colours::transparentBlack, b.getX(), b.getY() + 32.f, false);
        g.setGradientFill(topGlow);
        g.fillRoundedRectangle(b.withHeight(32.f), 10.f);

        // Header
        const float hdrH = (float)kHeaderH;
        g.setColour(AMP::C(AMP::kAccent));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.f).withStyle("Bold")));
        g.drawText("AUTOMATE CLIP", juce::Rectangle<float>(14.f, 0.f, b.getWidth() - 52.f, hdrH),
                   juce::Justification::centredLeft);
        // Clip name subtitle
        g.setColour(AMP::C(AMP::kTextDim));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.f)));
        g.drawText(juce::String(clip_.clipName),
                   juce::Rectangle<float>(14.f, 0.f, b.getWidth() - 52.f, hdrH),
                   juce::Justification::centredRight);

        // Header separator
        g.setColour(AMP::C(AMP::kBorder).withAlpha(0.7f));
        g.fillRect(juce::Rectangle<float>(0.f, hdrH - 1.f, b.getWidth(), 1.f));
    }

    void resized() override
    {
        int y = kHeaderH + 4;
        for (auto& r : rows_)
        {
            r.row->setBounds(6, y, getWidth() - 12, kRowH);
            y += kRowH + 3;
        }
        if (!embedded_)
        {
            closeBtn_.setBounds(getWidth() - 76, getHeight() - 32, 66, 22);
        }
    }

private:
    static constexpr int kHeaderH = 32;
    static constexpr int kRowH    = 32;
    static constexpr int kFooterH = 36;

    int preferredHeight() const noexcept
    {
        return kHeaderH + 4 + (int)rows_.size() * (kRowH + 3)
               + (embedded_ ? 8 : kFooterH);
    }

    struct RowEntry
    {
        DAW::AutomationQuickCreateCore::ControlTarget target;
        std::unique_ptr<AutomationParamRow> row;
    };

    ArrangementClipModel clip_;
    DAW::TrackID         trackId_;
    ClipAutomationPanelCallbacks cb_;
    std::vector<DAW::AutomationQuickCreateCore::ControlTarget> targets_;
    std::vector<RowEntry> rows_;
    juce::TextButton closeBtn_;
    bool             embedded_       = false;
    juce::String     explicitClipId_;
    double           engineSampleRate_ = 44100.0;

    static constexpr int kPanelWidth = ClipAutomateFloatingPanel::kPanelWidth;

    void buildTargets()
    {
        targets_.clear();
        const DAW::TrackID effectiveTrackId = trackId_.isEmpty()
            ? DAW::TrackID("track_" + juce::String(clip_.trackIndex)) : trackId_;

        auto add = [&](DAW::AutomationQuickCreateCore::ControlTarget t)
        {
            t.regionStartSample   = (int64_t)(clip_.startTime * engineSampleRate_);
            t.regionLengthSamples = (int64_t)(clip_.length    * engineSampleRate_);
            t.sampleRate          = engineSampleRate_;
            targets_.push_back(std::move(t));
        };

        const juce::String clipId = explicitClipId_.isNotEmpty()
            ? explicitClipId_ : clip_.id.toString();

        add(DAW::AutomationQuickCreateCore::makeTrackVolumeTarget(effectiveTrackId));
        add(DAW::AutomationQuickCreateCore::makeTrackPanTarget(effectiveTrackId));
        add(DAW::AutomationQuickCreateCore::makeClipTapeStopTarget(effectiveTrackId, clipId));
        add(DAW::AutomationQuickCreateCore::makeClipPitchTarget(effectiveTrackId, clipId));
        {
            DAW::AutomationQuickCreateCore::ControlTarget t;
            t.trackId      = effectiveTrackId;
            t.parameterId  = DAW::AutomationLaneCore::makeClipStretchParameterId(clipId);
            t.displayName  = "Clip Stretch";
            t.defaultValue = 1.0f;
            add(std::move(t));
        }
    }

    void buildRows()
    {
        rows_.clear();
        for (const auto& tgt : targets_)
        {
            RowEntry entry;
            entry.target = tgt;
            entry.row = std::make_unique<AutomationParamRow>();
            entry.row->displayName = tgt.displayName;
            entry.row->target      = tgt;
            entry.row->laneExists  = cb_.laneExists
                ? cb_.laneExists(tgt.trackId, tgt.parameterId) : false;

            const DAW::AutomationQuickCreateCore::ControlTarget capT = tgt;
            entry.row->onCreate = [this, capT]() mutable
            {
                if (cb_.createLane) cb_.createLane(capT);
                refreshStates();
            };
            entry.row->onShow  = [this, capT]() mutable { if (cb_.showLane)  cb_.showLane(capT);  };
            entry.row->onHide  = [this, capT]() mutable { if (cb_.hideLane)  cb_.hideLane(capT);  };
            entry.row->onClear = [this, capT]() mutable
            {
                if (cb_.clearLane) cb_.clearLane(capT);
                refreshStates();
            };
            entry.row->buildButtons(*this);
            addAndMakeVisible(*entry.row);
            rows_.push_back(std::move(entry));
        }

        if (!embedded_)
        {
            closeBtn_.setButtonText("Close");
            closeBtn_.onClick = [this] { if (onClose) onClose(); };
            addAndMakeVisible(closeBtn_);
        }
    }

    void refreshStates()
    {
        for (auto& entry : rows_)
        {
            entry.row->laneExists = cb_.laneExists
                ? cb_.laneExists(entry.target.trackId, entry.target.parameterId) : false;
            entry.row->updateButtonStates();
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAutomationPanel)
};

// ===========================================================================
// ClipAutomationWindow — standalone window using ClipAutomationPanel
// ===========================================================================
class ClipAutomationWindow : public juce::DocumentWindow
{
public:
    ClipAutomationWindow(const ArrangementClipModel&         clip,
                         const DAW::TrackID&                 trackId,
                         const ClipAutomationPanelCallbacks& callbacks,
                         juce::Component*                    centreNear = nullptr,
                         const juce::String&                 explicitClipId = {})
        : juce::DocumentWindow(
              juce::String("Automate: ") + juce::String(clip.clipName),
              AMP::C(AMP::kBg),
              juce::DocumentWindow::closeButton,
              true)
    {
        panel_ = std::make_unique<ClipAutomationPanel>(clip, trackId, callbacks, false, explicitClipId);
        panel_->onClose = [this] { closeButtonPressed(); };

        setContentNonOwned(panel_.get(), true);
        setResizable(false, false);
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0); // panel has its own header

        if (centreNear)
        {
            auto area = centreNear->getScreenBounds();
            setCentrePosition(area.getCentreX(), area.getCentreY());
        }
        else
        {
            centreWithSize(getWidth(), getHeight());
        }
        setVisible(true);
        toFront(true);
    }

    void closeButtonPressed() override { setVisible(false); }
    ~ClipAutomationWindow() override = default;

private:
    std::unique_ptr<ClipAutomationPanel> panel_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipAutomationWindow)
};

} // namespace ArrangementEditor
