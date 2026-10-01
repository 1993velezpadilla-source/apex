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
    std::function<void()> onResetSizeClicked;

    // Toolbar IDs are public so the responsive layout can be tested without
    // constructing a native window.  The order is also the priority order:
    // earlier actions remain visible longer; hidden actions move to overflow.
    enum ToolbarButtonId
    {
        kSettings = 0,
        kSidePanel,
        kBrowser,
        kCable,
        kOffscreen,
        kBG,
        kReset,
        kOverflow
    };

    struct ToolbarButtonLayout
    {
        juce::Rectangle<float> bounds;
        int id = kSettings;
    };

    /**
     * Computes the protected title-bar right zone.  At narrow widths the
     * toolbar starts after a title guard instead of crossing the centered
     * MIXER label; the responsive button layout then uses overflow.
     */
    static juce::Rectangle<float> computeResponsiveToolbarZone(int windowWidth) noexcept
    {
        const float width = (float) juce::jmax(0, windowWidth);
        const float right = juce::jmax(0.0f, width - 10.0f);
        const float requestedLeft = width - 220.0f - 10.0f;
        const float titleGuardLeft = width * 0.5f + 42.0f;
        const float left = juce::jlimit(0.0f, right,
                                        juce::jmax(requestedLeft, titleGuardLeft));
        return { left, 0.0f, right - left,
                 (float) FloatingWindowBase::kTitleH };
    }

    juce::Rectangle<float> getTitleBarRightZone() const override
    {
        return computeResponsiveToolbarZone(getWidth());
    }

    /** Pure responsive one-row layout used by paint, hit-testing, and tests. */
    static std::vector<ToolbarButtonLayout> computeToolbarLayout(
        juce::Rectangle<float> zone)
    {
        struct Spec { int id; float width; };
        constexpr Spec specs[] = {
            { kSettings, 22.0f }, { kSidePanel, 22.0f },
            { kBrowser, 22.0f },  { kCable, 22.0f },
            { kOffscreen, 22.0f }, { kBG, 30.0f }, { kReset, 30.0f }
        };
        constexpr float leftPad = 4.0f;
        constexpr float rightPad = 4.0f;
        constexpr float overflowW = 22.0f;
        constexpr float overflowGap = 3.0f;

        const auto gapBefore = [](int index) -> float
        {
            if (index == 0) return 0.0f;
            if (index == 1 || index == 5 || index == 6) return 10.0f;
            return 3.0f;
        };

        const auto requiredWidth = [&](int count, bool withOverflow) -> float
        {
            float result = leftPad + rightPad;
            for (int i = 0; i < count; ++i)
                result += specs[i].width + gapBefore(i);
            if (withOverflow)
                result += (count > 0 ? overflowGap : 0.0f) + overflowW;
            return result;
        };

        if (zone.getWidth() <= 0.0f)
            return {};

        constexpr int numSpecs = (int) (sizeof (specs) / sizeof (specs[0]));
        int visibleCount = numSpecs;
        if (requiredWidth(visibleCount, false) > zone.getWidth())
        {
            visibleCount = 0;
            for (int count = 1; count <= numSpecs; ++count)
                if (requiredWidth(count, true) <= zone.getWidth())
                    visibleCount = count;
                else
                    break;
        }

        const bool hasOverflow = visibleCount < numSpecs;
        if (hasOverflow && requiredWidth(visibleCount, true) > zone.getWidth())
            return {};

        std::vector<ToolbarButtonLayout> result;
        result.reserve((size_t) visibleCount + (hasOverflow ? 1u : 0u));
        float x = zone.getX() + leftPad;
        const float y = zone.getY() + (zone.getHeight() - 20.0f) * 0.5f;
        for (int i = 0; i < visibleCount; ++i)
        {
            x += gapBefore(i);
            result.push_back({ { x, y, specs[i].width, 20.0f }, specs[i].id });
            x += specs[i].width;
        }

        if (hasOverflow)
        {
            if (visibleCount > 0)
                x += overflowGap;
            result.push_back({ { x, y, overflowW, 20.0f }, kOverflow });
        }
        return result;
    }

    MixerWindow() : FloatingWindowBase("MIXER")
    {
        setUseOpaqueBackdrop(true);
        setBufferedToImage(false);
        setLookAndFeel(&glassStyle_);
        setMinimumSize(kMinW, kMixerMinH);
        setHideTrafficLights(true);
        setSize(800, 340);

        // ── APEX mixer identity ─────────────────────────────────────────────
        // Violet-dominant chrome (deep ultraviolet wash) so the expanded /
        // full-screen mixer reads as "Mixer = Violet with Blue/Cyan energy",
        // not as a blue surface.
        chromaBodyTint_ = juce::Colour(0xFF2B1B4A);

        // Expand (maximize) button: high-visibility pink-magenta so it pops
        // against the blue/cyan header energy instead of disappearing into it.
        accentMaxColour_ = juce::Colour(0xFFFF3D9F); // apex pink

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
    void paint(juce::Graphics& g) override
    {
        // Chrome first, then the blue/cyan header energy as a GUARANTEED final
        // pass of this window's own paint — no base-chrome layer can cover it.
        FloatingWindowBase::paint(g);
        paintHeaderEnergy(g, juce::Rectangle<float>(0.f, 0.f,
                                                    (float)getWidth(),
                                                    (float)kTitleH));
    }

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
                    int panW = panel->getWidth();
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

    // ── Cached blue/cyan header energy splash ──────────────────────────────
    // Built once per size; painted as a single image so the decoration never
    // allocates or rebuilds geometry inside paint().
    juce::Image headerEnergyCache_;
    juce::Path  headerEnergyVeins_;

    void paintHeaderEnergy(juce::Graphics& g, juce::Rectangle<float> bar)
    {
        auto& a = Theme::getInstance().apex;

        if (!headerEnergyCache_.isValid()
            || headerEnergyCache_.getWidth() != getWidth()
            || headerEnergyCache_.getHeight() != juce::jmax(1, (int)bar.getHeight()))
        {
            const int w = juce::jmax(1, getWidth());
            const int h = juce::jmax(1, (int)bar.getHeight());
            headerEnergyCache_ = juce::Image(juce::Image::ARGB, w, h, false);
            juce::Graphics ig(headerEnergyCache_);
            const auto fb = headerEnergyCache_.getBounds().toFloat();
            const float hw = fb.getWidth(), hh = fb.getHeight();

            DBG("[APEX-MIXER-HDR] header energy cache rebuilt w=" << w << " h=" << h);

            // Cyan signal bloom entering from the LEFT — smooth multi-stop
            // falloff for a premium bloom instead of a hard radial.
            {
                juce::ColourGradient c(
                    a.color.cyan.withAlpha(0.30f), hw * 0.12f, hh * 0.45f,
                    juce::Colours::transparentBlack, hw * 0.42f, hh * 0.55f, true);
                c.addColour(0.55, a.color.cyan.withAlpha(0.10f));
                ig.setGradientFill(c);
                ig.fillRect(fb);
            }
            // Deep blue energy bloom from the RIGHT — behind the toolbar zone.
            {
                juce::ColourGradient c(
                    a.color.blueBright.withAlpha(0.26f), hw * 0.90f, hh * 0.40f,
                    juce::Colours::transparentBlack, hw * 0.62f, hh * 0.55f, true);
                c.addColour(0.55, a.color.blueBright.withAlpha(0.08f));
                ig.setGradientFill(c);
                ig.fillRect(fb);
            }
            // Full-width directional wash — the energy travels across the bar.
            {
                juce::ColourGradient c(
                    a.color.cyan.withAlpha(0.08f), 0.f, hh * 0.15f,
                    a.color.blueBright.withAlpha(0.06f), hw, hh * 0.90f, false);
                ig.setGradientFill(c);
                ig.fillRect(fb);
            }
            // Magenta creative micro-accent (restrained, APEX creative energy)
            {
                juce::ColourGradient c(
                    a.color.magenta.withAlpha(0.07f), hw * 0.40f, hh * 0.80f,
                    juce::Colours::transparentBlack, hw * 0.52f, hh * 0.30f, true);
                ig.setGradientFill(c);
                ig.fillRect(fb);
            }
            // Luminous top edge line — violet transformation travelling into
            // cyan signal clarity across the header crown.
            {
                juce::ColourGradient edge(
                    a.color.violetBright.withAlpha(0.26f), 0.f, 0.f,
                    a.color.cyan.withAlpha(0.34f), hw, 0.f, false);
                edge.addColour(0.5, a.color.blueBright.withAlpha(0.20f));
                ig.setGradientFill(edge);
                ig.fillRect(fb.withHeight(1.f));
            }

            // Refined flowing veins: thin bright core + soft halo (elegant)
            headerEnergyVeins_.clear();
            headerEnergyVeins_.startNewSubPath(hw * 0.05f, hh * 0.80f);
            headerEnergyVeins_.cubicTo(hw * 0.20f, hh * 0.35f, hw * 0.38f, hh * 0.72f, hw * 0.52f, hh * 0.40f);
            headerEnergyVeins_.startNewSubPath(hw * 0.55f, hh * 0.85f);
            headerEnergyVeins_.cubicTo(hw * 0.70f, hh * 0.45f, hw * 0.85f, hh * 0.70f, hw * 0.98f, hh * 0.35f);

            ig.setColour(a.color.cyan.withAlpha(0.16f));
            ig.strokePath(headerEnergyVeins_, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            ig.setColour(a.color.cyan.withAlpha(0.42f));
            ig.strokePath(headerEnergyVeins_, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.drawImageAt(headerEnergyCache_, 0, (int)bar.getY(), false);
    }

    using Btn = ToolbarButtonLayout;

    std::vector<Btn> buildBtns(juce::Rectangle<float> zone) const
    {
        return computeToolbarLayout(zone);
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
        // Keep every hit target on the same single row.  The old fixed-index
        // grouping assumed all seven actions were present, which made the
        // background pills inaccurate as soon as a narrow window hid actions.
        for (const auto& btn : btns)
            pillBg(btn.bounds);

        for (auto& btn : btns)
        {
            bool hov = btn.bounds.contains(mouse);
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
            g.fillRoundedRectangle(btn.bounds, 4.f);

            // Border
            if (on)
            {
                g.setColour(accentOn.withAlpha(0.65f));
                g.drawRoundedRectangle(btn.bounds, 4.f, 1.f);
                // Glow
                g.setColour(accentOn.withAlpha(0.12f));
                g.fillRoundedRectangle(btn.bounds.expanded(2.f), 5.f);
            }
            else if (hov)
            {
                g.setColour(juce::Colours::white.withAlpha(0.20f));
                g.drawRoundedRectangle(btn.bounds, 4.f, 0.8f);
            }

            // Icon colour
            juce::Colour ic = on  ? juce::Colours::white
                            : hov ? juce::Colours::white.withAlpha(0.88f)
                                  : juce::Colours::white.withAlpha(0.48f);
            g.setColour(ic);
            drawIcon(g, btn.id, btn.bounds.getCentre(), ic);
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
            case kReset:
            {
                // Reset icon: circular arrow (arc + arrowhead) — restores the
                // default mixer size/position.
                const float r = 4.6f;
                juce::Path circ;
                circ.startNewSubPath(c.x + r, c.y);
                circ.addArc(c.x - r, c.y - r, r * 2.f, r * 2.f, 0.f, juce::MathConstants<float>::pi * 1.5f);
                g.strokePath(circ, juce::PathStrokeType(1.3f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                // Arrowhead at the arc end (top of the circle)
                juce::Path head;
                head.startNewSubPath(c.x - 1.2f, c.y - r - 1.2f);
                head.lineTo(c.x, c.y - r);
                head.lineTo(c.x + 1.2f, c.y - r - 1.2f);
                g.strokePath(head, juce::PathStrokeType(1.2f,
                    juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
                break;
            }
            case kOverflow:
            {
                g.setFont(juce::Font(13.0f, juce::Font::bold));
                g.drawText("...", juce::Rectangle<float>(c.x - 9.0f, c.y - 8.0f,
                                                           18.0f, 16.0f),
                           juce::Justification::centred);
                break;
            }
        }
    }

    bool toolbarHitTest(juce::Point<float> p) const
    {
        for (auto& btn : buildBtns(getTitleBarRightZone()))
            if (btn.bounds.contains(p)) return true;
        return false;
    }

    static juce::String toolbarActionName(int id)
    {
        switch (id)
        {
            case kSettings:  return "Mixer Settings";
            case kSidePanel: return "FX Chain";
            case kBrowser:   return "Plugin Browser";
            case kCable:     return "Bubblegum Cables";
            case kOffscreen: return "Offscreen Endpoints";
            case kBG:        return "Bubblegum Mode";
            case kReset:     return "Reset Mixer Size";
            default:         return {};
        }
    }

    void showToolbarOverflowMenu()
    {
        const auto visible = buildBtns(getTitleBarRightZone());
        const auto isVisible = [&visible](int id)
        {
            for (const auto& btn : visible)
                if (btn.id == id)
                    return true;
            return false;
        };

        juce::PopupMenu menu;
        constexpr int menuBase = 100;
        bool addedAny = false;
        for (int id = kSettings; id <= kReset; ++id)
        {
            if (isVisible(id))
                continue;

            menu.addItem(menuBase + id, toolbarActionName(id), true, isOn(id));
            addedAny = true;
        }

        if (!addedAny)
            return;

        juce::Component::SafePointer<MixerWindow> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [safeThis, menuBase](int result)
                           {
                               if (auto* self = safeThis.getComponent())
                                   if (result >= menuBase && result <= menuBase + kReset)
                                       self->invokeToolbarAction(result - menuBase);
                           });
    }

    void invokeToolbarAction(int id)
    {
        switch (id)
        {
            case kSettings:  if (onSettingsClicked) onSettingsClicked(); break;
            case kSidePanel: if (onSidePanelClicked) onSidePanelClicked(); break;
            case kBrowser:   if (onBrowserClicked) onBrowserClicked(); break;
            case kCable:     if (onCableToggleClicked) onCableToggleClicked(); break;
            case kOffscreen: if (onOffscreenToggleClicked) onOffscreenToggleClicked(); break;
            case kBG:        if (onBubblegumClicked) onBubblegumClicked(); break;
            case kReset:     if (onResetSizeClicked) onResetSizeClicked(); break;
            case kOverflow:  showToolbarOverflowMenu(); break;
            default:         break;
        }
        repaint(getTitleBarRightZone().toNearestInt());
    }

    void handleToolbarClick(juce::Point<float> p)
    {
        for (auto& btn : buildBtns(getTitleBarRightZone()))
        {
            if (!btn.bounds.contains(p)) continue;
            invokeToolbarAction(btn.id);
            return;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerWindow)
};

} // namespace DAW
