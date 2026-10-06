#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../FloatingWindowCore/FloatingWindowBase.h"
#include "GlassMixerLookAndFeel.h"

namespace DAW {

/**
 * MixerWindow — Floating mixer chrome.
 * Toolbar buttons are embedded in the title bar right zone,
 * reclaiming the entire content area for track strips.
 */
class MixerWindow : public FloatingWindowBase
{
public:
    static constexpr int kMinTopY   = 28;
    static constexpr int kMixerMinH = 260;

    // Button state — kept in sync by whoever owns the MixerPanel.
    bool bgActive        = false;
    bool sidePanelOpen  = false;
    bool browserOpen    = false;
    bool cableVisible   = true;
    bool offscreenVisible = true;

    std::function<void()> onBubblegumClicked;
    std::function<void()> onSidePanelClicked;
    std::function<void()> onBrowserClicked;
    std::function<void()> onCableToggleClicked;
    std::function<void()> onOffscreenToggleClicked;
    std::function<void()> onSettingsClicked;
    std::function<void()> onDockRequested;

    MixerWindow() : FloatingWindowBase("MIXER")
    {
        setUseOpaqueBackdrop(true);
        setBufferedToImage(false);
        setLookAndFeel(&glassStyle_);
        setMinimumSize(kMinW, kMixerMinH);
        setHideTrafficLights(true);
        setSize(800, 340);

        titleBarRightPainter = [this](juce::Graphics& g, juce::Rectangle<float> zone)
        { paintToolbar(g, zone); };

        titleBarRightHitTest = [this](juce::Point<float> p) -> bool
        { return toolbarHitTest(p); };

        titleBarRightMouseDown = [this](juce::Point<float> p)
        { handleToolbarClick(p); };

        titleBarRightMouseMove = [this](juce::Point<float> p)
        {
            if (getTitleBarRightZone().contains(p))
                repaint(getTitleBarRightZone().toNearestInt());
        };
    }

    void setContent(juce::Component* content)
    {
        content_ = content;
        if (content_) addAndMakeVisible(content_);
        layoutContent();
    }

    void refreshToolbar() { repaint(getTitleBarRightZone().toNearestInt()); }

    ~MixerWindow() override
    {
        setLookAndFeel(nullptr);
    }

protected:
    void layoutContent() override
    {
        if (content_) content_->setVisible(!isMinimized());
        if (content_ && !isMinimized())
        {
            auto ca = getContentArea();
            content_->setBounds(ca);
            if (auto* vp = dynamic_cast<juce::Viewport*>(content_))
            {
                if (auto* panel = vp->getViewedComponent())
                {
                    int sbH  = vp->getScrollBarThickness();
                    int panH = juce::jmax(1, ca.getHeight() - sbH);
                    int panW = juce::jmax(ca.getWidth(), panel->getWidth());
                    panel->setSize(panW, panH);
                }
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override { FloatingWindowBase::mouseDrag(e); clampTop(); }
    void mouseUp  (const juce::MouseEvent& e) override { FloatingWindowBase::mouseUp(e);   clampTop(); }

private:
    GlassMixerLookAndFeel glassStyle_;
    juce::Component* content_ = nullptr;
    void clampTop() { if (getBounds().getY() < kMinTopY) setTopLeftPosition(getBounds().getX(), kMinTopY); }

    // ── Button ids ────────────────────────────────────────────────────────
    enum BtnId { kSettings=0, kSidePanel, kBrowser, kCable, kOffscreen, kBG };

    struct Btn { juce::Rectangle<float> r; int id; };

    std::vector<Btn> buildBtns(juce::Rectangle<float> zone) const
    {
        constexpr float bh=20.f, bwN=22.f, bwBG=30.f, gap=3.f, groupGap=10.f;
        float y  = zone.getY() + (zone.getHeight() - bh) * 0.5f;
        float x  = zone.getX() + 4.f;
        std::vector<Btn> out;
        out.push_back({ { x, y, bwN,  bh }, kSettings });   x += bwN  + groupGap;
        out.push_back({ { x, y, bwN,  bh }, kSidePanel });   x += bwN  + gap;
        out.push_back({ { x, y, bwN,  bh }, kBrowser   });   x += bwN  + gap;
        out.push_back({ { x, y, bwN,  bh }, kCable     });   x += bwN  + gap;
        out.push_back({ { x, y, bwN,  bh }, kOffscreen });   x += bwN  + groupGap;
        out.push_back({ { x, y, bwBG, bh }, kBG        });
        return out;
    }

    bool isOn(int id) const
    {
        if (id == kBG)        return bgActive;
        if (id == kSidePanel) return sidePanelOpen;
        if (id == kBrowser)   return browserOpen;
        if (id == kCable)     return cableVisible;
        if (id == kOffscreen) return offscreenVisible;
        return false;
    }

    // ── Paint ─────────────────────────────────────────────────────────────
    void paintToolbar(juce::Graphics& g, juce::Rectangle<float> zone)
    {
        auto& t    = Theme::getInstance();
        auto mouse = getMouseXYRelative().toFloat();
        auto btns  = buildBtns(zone);

        // Group pill backgrounds
        auto pillBg = [&](juce::Rectangle<float> r)
        {
            g.setColour(juce::Colours::black.withAlpha(0.28f));
            g.fillRoundedRectangle(r.expanded(4.f, 2.f), 6.f);
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.drawRoundedRectangle(r.expanded(4.f, 2.f), 6.f, 0.8f);
        };
        if (btns.size() >= 1) pillBg(btns[0].r);
        if (btns.size() >= 5) pillBg(btns[1].r.getUnion(btns[4].r));
        if (btns.size() >= 6) pillBg(btns[5].r);

        for (auto& btn : btns)
        {
            bool hov = btn.r.contains(mouse);
            bool on  = isOn(btn.id);

            juce::Colour accentOn = (btn.id == kBG)
                ? juce::Colour(0xFFFF2D78) : t.colors.accent;

            // Fill
            if (on)
                g.setColour(accentOn.withAlpha(0.28f));
            else if (hov)
                g.setColour(juce::Colours::white.withAlpha(0.09f));
            else
                g.setColour(juce::Colour(0x00000000)); // transparent
            g.fillRoundedRectangle(btn.r, 4.f);

            // Border
            if (on)
            {
                g.setColour(accentOn.withAlpha(0.65f));
                g.drawRoundedRectangle(btn.r, 4.f, 1.f);
                // Glow
                g.setColour(accentOn.withAlpha(0.12f));
                g.fillRoundedRectangle(btn.r.expanded(2.f), 5.f);
            }
            else if (hov)
            {
                g.setColour(juce::Colours::white.withAlpha(0.20f));
                g.drawRoundedRectangle(btn.r, 4.f, 0.8f);
            }

            // Icon colour
            juce::Colour ic = on  ? juce::Colours::white
                            : hov ? juce::Colours::white.withAlpha(0.88f)
                                  : juce::Colours::white.withAlpha(0.48f);
            g.setColour(ic);
            drawIcon(g, btn.id, btn.r.getCentre(), ic);
        }
    }

    void drawIcon(juce::Graphics& g, int id, juce::Point<float> c, juce::Colour col)
    {
        g.setColour(col);
        switch (id)
        {
            case kSettings:
            {
                // Gear: centre circle + 6 teeth
                const float ri = 2.2f, ro = 4.8f;
                g.drawEllipse(c.x - ri, c.y - ri, ri * 2.f, ri * 2.f, 1.1f);
                for (int i = 0; i < 6; ++i)
                {
                    float a = juce::MathConstants<float>::twoPi * i / 6.f;
                    float ca_ = std::cos(a), sa_ = std::sin(a);
                    float halfW = 1.0f;
                    float ax = c.x + ca_ * (ri + 0.5f);
                    float ay = c.y + sa_ * (ri + 0.5f);
                    float bx = c.x + ca_ * ro;
                    float by = c.y + sa_ * ro;
                    g.drawLine(ax, ay, bx, by, 1.6f);
                    (void)halfW;
                }
                break;
            }
            case kSidePanel:
            {
                // Outer rect + left divider (FX panel layout icon)
                g.drawRoundedRectangle(c.x - 6.f, c.y - 5.f, 12.f, 10.f, 1.5f, 1.2f);
                g.drawLine(c.x - 1.5f, c.y - 5.f, c.x - 1.5f, c.y + 5.f, 1.2f);
                // Three small horizontal lines on right section (suggests plugin list)
                for (int i = 0; i < 3; ++i)
                {
                    float ly = c.y - 2.5f + i * 2.5f;
                    g.drawLine(c.x + 0.f, ly, c.x + 4.5f, ly, 0.9f);
                }
                break;
            }
            case kBrowser:
            {
                // Magnifying glass
                g.drawEllipse(c.x - 4.5f, c.y - 5.f, 7.5f, 7.5f, 1.3f);
                g.drawLine(c.x + 1.8f, c.y + 1.8f, c.x + 4.8f, c.y + 4.8f, 1.6f);
                break;
            }
            case kCable:
            {
                // Two anchor nodes connected by a sagging arc
                const float nr = 1.8f;
                g.fillEllipse(c.x - 6.5f, c.y - 1.5f - nr, nr * 2.f, nr * 2.f);
                g.fillEllipse(c.x + 6.5f - nr * 2.f, c.y - 1.5f - nr, nr * 2.f, nr * 2.f);
                juce::Path cable;
                cable.startNewSubPath(c.x - 4.5f, c.y - 1.5f);
                cable.cubicTo(c.x - 2.f, c.y + 4.f, c.x + 2.f, c.y + 4.f, c.x + 4.5f, c.y - 1.5f);
                g.strokePath(cable, juce::PathStrokeType(1.4f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;
            }
            case kOffscreen:
            {
                // Offscreen bubble indicator: circle with left/right arrows
                g.drawEllipse(c.x - 4.5f, c.y - 4.5f, 9.f, 9.f, 1.3f);
                // Left arrow
                juce::Path la;
                la.startNewSubPath(c.x - 1.5f, c.y - 2.f);
                la.lineTo(c.x - 4.f, c.y);
                la.lineTo(c.x - 1.5f, c.y + 2.f);
                g.strokePath(la, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
                // Right arrow
                juce::Path ra;
                ra.startNewSubPath(c.x + 1.5f, c.y - 2.f);
                ra.lineTo(c.x + 4.f, c.y);
                ra.lineTo(c.x + 1.5f, c.y + 2.f);
                g.strokePath(ra, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
                break;
            }
            case kBG:
            {
                // Send-wave arc above "BG" text — looks like a broadcast/routing icon
                juce::Path arc;
                arc.startNewSubPath(c.x - 5.f, c.y - 1.5f);
                arc.quadraticTo(c.x, c.y - 6.5f, c.x + 5.f, c.y - 1.5f);
                g.strokePath(arc, juce::PathStrokeType(1.3f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setFont(juce::Font(7.5f, juce::Font::bold));
                g.drawText("BG", juce::Rectangle<float>(c.x - 8.f, c.y + 0.5f, 16.f, 9.f),
                           juce::Justification::centred);
                break;
            }
        }
    }

    bool toolbarHitTest(juce::Point<float> p) const
    {
        for (auto& btn : buildBtns(getTitleBarRightZone()))
            if (btn.r.contains(p)) return true;
        return false;
    }

    void handleToolbarClick(juce::Point<float> p)
    {
        for (auto& btn : buildBtns(getTitleBarRightZone()))
        {
            if (!btn.r.contains(p)) continue;
            switch (btn.id)
            {
                case kSettings:  if (onSettingsClicked)   onSettingsClicked();   break;
                case kSidePanel: if (onSidePanelClicked)  onSidePanelClicked();  break;
                case kBrowser:   if (onBrowserClicked)    onBrowserClicked();    break;
                case kCable:     if (onCableToggleClicked)    onCableToggleClicked();    break;
                case kOffscreen: if (onOffscreenToggleClicked) onOffscreenToggleClicked(); break;
                case kBG:        if (onBubblegumClicked)       onBubblegumClicked();       break;
            }
            repaint(getTitleBarRightZone().toNearestInt());
            return;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerWindow)
};

} // namespace DAW
