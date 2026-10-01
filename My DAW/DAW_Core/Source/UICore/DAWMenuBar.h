#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../ThemeCore/ApexPrimitives.h"
#include "../CommandCore/CommandManager.h"
#include "../ActionCore/ActionID.h"
#include "../KeyBindingCore/KeyBindingManager.h"

namespace DAW {

// ─── MenuBar ──────────────────────────────────────────────────────────────────
// Horizontal top menu bar with clickable/touchable menus
class DAWMenuBar : public juce::Component
{
public:
    struct MenuItem { juce::String label; std::function<void()> action; ActionID actionId = ActionID::None; };
    struct Menu     { juce::String title; std::vector<MenuItem> items; };

    DAWMenuBar()
    {
        menus_ = {
            { "File",     { {"New Project", nullptr, ActionID::ProjectNew},
                             {"Open Project", nullptr, ActionID::ProjectOpen},
                             {"Save Project", nullptr, ActionID::ProjectSave},
                             {"Save As...", nullptr, ActionID::ProjectSaveAs},
                             {"---"}, {"Import Audio...", nullptr, ActionID::ImportFiles},
                             {"---"}, {"Export Audio / Stems...", nullptr} } },
            { "Edit",     { {"Undo", nullptr, ActionID::EditUndo},
                             {"Redo", nullptr, ActionID::EditRedo},
                             {"---"}, {"Cut", nullptr}, {"Copy", nullptr}, {"Paste", nullptr},
                             {"---"}, {"Duplicate", nullptr, ActionID::ClipDuplicate},
                             {"Delete", nullptr, ActionID::ClipDelete},
                             {"---"}, {"Select All", nullptr, ActionID::EditSelectAll},
                             {"Deselect", nullptr},
                             {"---"}, {"History...", nullptr, ActionID::ViewToggleHistory} } },
{ "View",     { {"Mixer", nullptr, ActionID::ViewToggleMixer},
                              {"Track List", nullptr, ActionID::ViewToggleTrackList},
                              // DISABLED (product decision): beat-making
                              // surfaces are hidden — Piano Keyboard and Step
                              // Sequencer menu items removed (bindings stay).
                              {"---"}, {"Full Screen", nullptr, ActionID::ViewToggleFullScreen} } },
            { "Track",    { {"Add Audio Track", nullptr, ActionID::TrackAddAudio},
                             // DISABLED (product decision): Add MIDI Track is
                             // hidden (beat-making surfaces disabled).
                             {"---"}, {"Duplicate Track", nullptr, ActionID::TrackDuplicate},
                             {"Delete Track", nullptr, ActionID::TrackDeleteSelected},
                             {"---"}, {"Arm Track", nullptr, ActionID::TrackArmSelected},
                             {"Monitor Track", nullptr},
                             {"Mute Track", nullptr, ActionID::TrackMuteSelected},
                             {"Solo Track", nullptr, ActionID::TrackSoloSelected},
                             {"---"}, {"Track Color", nullptr},
                             {"Track Rename", nullptr, ActionID::TrackRenameSelected} } },
            { "Clip",     { {"Vocal Tune", nullptr} } },
            { "Settings", { {"Audio Device...", nullptr},
                             {"Control Room / Monitor...", nullptr},
                             {"---"},
                             {"Interface: Standard", nullptr},
                             {"Interface: Immersive 3D", nullptr},
                             {"---"},
                             {"Keybinding Profile", nullptr},
                             {"---"},
                             {"Preferences...", nullptr} } }
        };
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto& a = t.apex;
        g.fillAll(a.color.deepestB);

        // ── Cached 50/50 splash background ─────────────────────────────────
        // Blue/cyan energy enters from the left, purple/violet from the right,
        // merging organically near the centre (no hard line). Rebuilt only on
        // resize; paint() just draws the cached image.
        if (!headerBgCache_.isValid()
            || headerBgCache_.getWidth() != getWidth()
            || headerBgCache_.getHeight() != getHeight())
            rebuildHeaderBackground();
        if (headerBgCache_.isValid())
            g.drawImageAt(headerBgCache_, 0, 0, false);

        // Vibrant colour splash overlay — static, no animation timer.
        {
            ApexPrimitives::drawAnimatedHeaderSplatter(
                g, getLocalBounds().toFloat().withTrimmedBottom(2.0f), 0.0f);
        }

        // Bottom separator
        g.setColour(a.color.borderSoftA);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);

        // Brand — APEX signal sigil + wordmark. The sigil communicates
        // "raw signal, seen, transformed, expanded" at 18px; subtle, not a logo flood.
        {
            if (brandSigil_.isEmpty() && getHeight() > 0)
                brandSigil_ = ApexPrimitives::buildSignalSigilPath(
                    juce::Rectangle<float>(10.f, (getHeight() - 18.f) * 0.5f, 18.f, 18.f));
            ApexPrimitives::drawSignalSigil(g, brandSigil_, 0.55f);
        }
        g.setFont(t.fonts.bold);
        g.setColour(a.color.magenta);
        g.drawText("APEX", 34, 0, 64, getHeight(), juce::Justification::centredLeft);

        // Menu titles
        g.setFont(t.fonts.regular);
        int x = 118;
        for (int i = 0; i < (int)menus_.size(); ++i)
        {
            menuRects_[i].setBounds(x, 0, 72, getHeight());
            bool hov  = (hoveredMenu_ == i);
            bool open = (openMenu_    == i);

            if (open)
            {
                g.setColour(a.color.magenta.withAlpha(a.state.selectedStrength * 0.65f));
                g.fillRect(menuRects_[i]);
                g.setColour(a.color.magenta);
                g.fillRect(menuRects_[i].getX(), getHeight() - 2, menuRects_[i].getWidth(), 2);
            }
            else if (hov)
            {
                g.setColour(a.color.panelB);
                g.fillRect(menuRects_[i]);
            }

            g.setColour(open ? a.color.textPrimary
                             : hov ? a.color.textPrimary : a.color.textSecondary);
            g.drawText(menus_[i].title, menuRects_[i], juce::Justification::centred);
            x += 72;
        }

        // ── Window controls (custom title bar, right edge) ────────────────────
        updateWindowControlRects();
        auto mouse = getMouseXYRelative().toFloat();

        // ── Right-aligned Undo / Redo buttons (left of the window controls) ───
        float btnW = 28.f, btnH = 20.f, btnGap = 2.f, margin = 12.f;
        float btnY = (getHeight() - btnH) * 0.5f;
        redoBtnBounds_  = { minBtnBounds_.getX() - margin - btnW, btnY, btnW, btnH };
        undoBtnBounds_  = { redoBtnBounds_.getX() - btnGap - btnW, btnY, btnW, btnH };

        bool canUndo = CommandManager::getInstance().canUndo();
        bool canRedo = CommandManager::getInstance().canRedo();

        // Undo button
        {
            bool bHov = (hoveredButton_ == 1);
            auto col  = canUndo ? (bHov ? a.color.textPrimary : a.color.textSecondary)
                                : a.color.textMuted.withAlpha(0.45f);
            if (bHov && canUndo)
            {
                g.setColour(a.color.panelB);
                g.fillRoundedRectangle(undoBtnBounds_, a.metric.radiusControl);
            }
            drawUndoIcon(g, undoBtnBounds_, col);
        }
        // Redo button
        {
            bool bHov = (hoveredButton_ == 2);
            auto col  = canRedo ? (bHov ? a.color.textPrimary : a.color.textSecondary)
                                : a.color.textMuted.withAlpha(0.45f);
            if (bHov && canRedo)
            {
                g.setColour(a.color.panelB);
                g.fillRoundedRectangle(redoBtnBounds_, a.metric.radiusControl);
            }
            drawRedoIcon(g, redoBtnBounds_, col);
        }

        // ── Custom APEX window controls: minimize, maximize/restore, close ────
        // Drawn to belong to the dark violet/magenta shell — never the white
        // Windows appearance. Clicks are handled by the OS shell (the top-level
        // window maps these rects to HTMINBUTTON/HTMAXBUTTON/HTCLOSE via
        // findControlAtPoint), so no mouseDown handling is needed here.
        {
            const auto drawWindowBtn = [&](juce::Rectangle<float> r, int id)
            {
                const bool hov = r.contains(mouse);
                if (hov)
                {
                    g.setColour(id == 5 ? a.color.magentaDeep.withAlpha(0.9f)
                                        : a.color.panelC.brighter(0.10f));
                    g.fillRect(r);
                }
                const juce::Colour glyph = hov ? a.color.textPrimary
                                               : a.color.textSecondary;
                g.setColour(glyph);
                const float cx = r.getCentreX(), cy = r.getCentreY();

                if (id == 3) // minimize: single bar
                {
                    g.fillRoundedRectangle(cx - 7.f, cy - 0.75f, 14.f, 1.5f, 0.75f);
                }
                else if (id == 4) // maximize / restore
                {
                    if (windowMaximized_)
                    {
                        // Restore glyph: two overlapping squares
                        g.drawRoundedRectangle(cx - 6.f, cy - 5.f, 10.f, 8.f, 1.5f, 1.2f);
                        g.drawRoundedRectangle(cx - 3.f, cy - 2.f, 10.f, 8.f, 1.5f, 1.2f);
                        g.setColour(a.color.deepestB);
                        g.fillRect(juce::Rectangle<float>(cx - 3.f, cy - 2.f, 2.5f, 2.f));
                    }
                    else
                    {
                        g.drawRoundedRectangle(cx - 6.f, cy - 5.f, 12.f, 10.f, 1.5f, 1.2f);
                    }
                }
                else // id == 5: close: X
                {
                    g.drawLine(cx - 5.f, cy - 5.f, cx + 5.f, cy + 5.f, 1.4f);
                    g.drawLine(cx - 5.f, cy + 5.f, cx + 5.f, cy - 5.f, 1.4f);
                }
            };

            drawWindowBtn(minBtnBounds_, 3);
            drawWindowBtn(maxBtnBounds_, 4);
            drawWindowBtn(closeBtnBounds_, 5);
        }
    }

    void resized() override
    {
        menuRects_.resize(menus_.size());
        updateWindowControlRects();
        brandSigil_.clear();  // geometry depends on height; rebuild on next paint
        repaint();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        int prev = hoveredMenu_;
        hoveredMenu_ = -1;
        for (int i = 0; i < (int)menus_.size(); ++i)
            if (menuRects_[i].contains(e.getPosition())) { hoveredMenu_ = i; break; }

        int prevBtn = hoveredButton_;
        hoveredButton_ = 0;
        if (undoBtnBounds_.contains(e.position)) hoveredButton_ = 1;
        else if (redoBtnBounds_.contains(e.position)) hoveredButton_ = 2;
        else if (minBtnBounds_.contains(e.position)) hoveredButton_ = 3;
        else if (maxBtnBounds_.contains(e.position)) hoveredButton_ = 4;
        else if (closeBtnBounds_.contains(e.position)) hoveredButton_ = 5;

        if (hoveredMenu_ != prev || hoveredButton_ != prevBtn) repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoveredMenu_ = -1;
        hoveredButton_ = 0;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (undoBtnBounds_.contains(e.position) && CommandManager::getInstance().canUndo())
        {
            ActionManager::getInstance().dispatch(ActionID::EditUndo);
            return;
        }
        if (redoBtnBounds_.contains(e.position) && CommandManager::getInstance().canRedo())
        {
            ActionManager::getInstance().dispatch(ActionID::EditRedo);
            return;
        }

        for (int i = 0; i < (int)menus_.size(); ++i)
        {
            if (menuRects_[i].contains(e.getPosition()))
            {
                openMenu_ = (openMenu_ == i) ? -1 : i;
                if (openMenu_ >= 0) showPopup(openMenu_);
                repaint();
                return;
            }
        }
        openMenu_ = -1;
        repaint();
    }

    // Connect menu item actions after construction
    void setMenuAction(const juce::String& menuTitle, const juce::String& itemLabel,
                       std::function<void()> action)
    {
        for (auto& m : menus_)
            if (m.title == menuTitle)
                for (auto& item : m.items)
                    if (item.label == itemLabel) { item.action = std::move(action); return; }
    }

    void setMenuItemActionID(const juce::String& menuTitle, const juce::String& itemLabel,
                             ActionID id)
    {
        for (auto& m : menus_)
            if (m.title == menuTitle)
                for (auto& item : m.items)
                    if (item.label == itemLabel) { item.actionId = id; return; }
    }

    // ── Integrated window controls (this header IS the custom title bar) ──
    // The top-level window shell routes these rects to the OS through
    // Component::findControlAtPoint (caption drag / native button clicks), so
    // the header only needs to draw them and report their geometry.
    enum class WindowControlId { None, Minimize, Maximize, Close };

    /** The window control under the given header-local point (None otherwise). */
    WindowControlId findWindowControlAt(juce::Point<int> p) const noexcept
    {
        if (minBtnBounds_.contains(p.toFloat()))   return WindowControlId::Minimize;
        if (maxBtnBounds_.contains(p.toFloat()))   return WindowControlId::Maximize;
        if (closeBtnBounds_.contains(p.toFloat())) return WindowControlId::Close;
        return WindowControlId::None;
    }

    /** True when the point is over an interactive control (menu title,
     *  undo/redo or window control). Empty header areas must NOT be
     *  interactive — they act as the draggable caption region. */
    bool isInteractiveAt(juce::Point<int> p) const noexcept
    {
        if (findWindowControlAt(p) != WindowControlId::None)
            return true;
        for (const auto& r : menuRects_)
            if (r.contains(p)) return true;
        if (undoBtnBounds_.contains(p.toFloat())) return true;
        if (redoBtnBounds_.contains(p.toFloat())) return true;
        return false;
    }

    /** Reflect the top-level window's maximize / exclusive-fullscreen state so
     *  the maximize button can draw the restore glyph. */
    void setWindowMaximized(bool m)
    {
        if (windowMaximized_ != m)
        {
            windowMaximized_ = m;
            repaint();
        }
    }

private:
    std::vector<Menu> menus_;
    std::vector<juce::Rectangle<int>> menuRects_;
    int hoveredMenu_   = -1;
    int openMenu_      = -1;
    int hoveredButton_ = 0;  // 0=none, 1=undo, 2=redo, 3=minimize, 4=maximize, 5=close
    mutable juce::Rectangle<float> undoBtnBounds_, redoBtnBounds_;
    mutable juce::Rectangle<float> minBtnBounds_, maxBtnBounds_, closeBtnBounds_;
    bool windowMaximized_ = false;
    juce::Path brandSigil_;  // cached; rebuilt on next paint after resize

    // ── Window-control geometry (right edge, Win11 order: min, max, close) ──
    void updateWindowControlRects()
    {
        constexpr float winBtnW = 40.f;
        closeBtnBounds_ = { (float)getWidth() - winBtnW, 0.f, winBtnW, (float)getHeight() };
        maxBtnBounds_   = { closeBtnBounds_.getX() - winBtnW, 0.f, winBtnW, (float)getHeight() };
        minBtnBounds_   = { maxBtnBounds_.getX() - winBtnW, 0.f, winBtnW, (float)getHeight() };
    }

    // ── Cached splash background (built once per size) ─────────────────────
    juce::Image headerBgCache_;
    juce::Path  headerVeinsL_;  // cyan signal veins (left system)
    juce::Path  headerVeinsR_;  // violet transformation veins (right system)

    void rebuildHeaderBackground()
    {
        const int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0)
        {
            headerBgCache_ = {};
            return;
        }

        headerBgCache_ = juce::Image(juce::Image::ARGB, w, h, false);
        juce::Graphics ig(headerBgCache_);
        const auto fb = headerBgCache_.getBounds().toFloat();
        auto& a = Theme::getInstance().apex;

        // ── Left energy system: blue/cyan (signal, clarity, fidelity) ─────
        {
            juce::ColourGradient c(a.color.cyan.withAlpha(0.14f),      w * 0.12f, h * 0.55f,
                                   juce::Colours::transparentBlack,    w * 0.44f, h * 0.55f, true);
            ig.setGradientFill(c);
            ig.fillRect(fb);
        }
        {
            juce::ColourGradient c(a.color.blueBright.withAlpha(0.10f), w * 0.30f, h * 0.22f,
                                   juce::Colours::transparentBlack,     w * 0.56f, h * 0.55f, true);
            ig.setGradientFill(c);
            ig.fillRect(fb);
        }

        // ── Right energy system: purple/violet (transformation) ───────────
        {
            juce::ColourGradient c(a.color.violet.withAlpha(0.16f),       w * 0.90f, h * 0.50f,
                                   juce::Colours::transparentBlack,       w * 0.60f, h * 0.55f, true);
            ig.setGradientFill(c);
            ig.fillRect(fb);
        }
        {
            juce::ColourGradient c(a.color.violetBright.withAlpha(0.09f), w * 0.72f, h * 0.20f,
                                   juce::Colours::transparentBlack,       w * 0.52f, h * 0.55f, true);
            ig.setGradientFill(c);
            ig.fillRect(fb);
        }

        // ── Organic centre merge: soft diagonal bridge, no hard line ──────
        {
            juce::ColourGradient bridge(
                a.color.cyan.withAlpha(0.06f),          w * 0.28f, h * 0.08f,
                a.color.violet.withAlpha(0.06f),        w * 0.82f, h * 0.95f, false);
            bridge.addColour(0.5, a.color.violetBright.withAlpha(0.05f));
            ig.setGradientFill(bridge);
            ig.fillRect(fb);
        }

        // ── Magenta creative micro-accents (restrained) ────────────────────
        {
            juce::ColourGradient c(a.color.magenta.withAlpha(0.05f), w * 0.40f, h * 0.85f,
                                   juce::Colours::transparentBlack,  w * 0.52f, h * 0.85f, true);
            ig.setGradientFill(c);
            ig.fillRect(fb);
        }
        {
            juce::ColourGradient c(a.color.pink.withAlpha(0.05f), w * 0.64f, h * 0.12f,
                                   juce::Colours::transparentBlack, w * 0.76f, h * 0.32f, true);
            ig.setGradientFill(c);
            ig.fillRect(fb);
        }

        // ── Flowing signal contours (cached paths) ─────────────────────────
        headerVeinsL_.clear();
        headerVeinsL_.startNewSubPath(w * 0.02f, h * 0.78f);
        headerVeinsL_.cubicTo(w * 0.16f, h * 0.28f, w * 0.32f, h * 0.72f, w * 0.47f, h * 0.38f);
        headerVeinsR_.clear();
        headerVeinsR_.startNewSubPath(w * 0.55f, h * 0.30f);
        headerVeinsR_.cubicTo(w * 0.68f, h * 0.64f, w * 0.84f, h * 0.32f, w * 0.99f, h * 0.62f);

        ig.setColour(a.color.cyan.withAlpha(0.13f));
        ig.strokePath(headerVeinsL_, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        ig.setColour(a.color.violet.withAlpha(0.14f));
        ig.strokePath(headerVeinsR_, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawUndoIcon(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);
        float cx = b.getCentreX(), cy = b.getCentreY();
        juce::Path p;
        p.startNewSubPath(cx + 5.f, cy + 3.f);
        p.quadraticTo(cx + 5.f, cy - 5.f, cx - 3.f, cy - 5.f);
        g.strokePath(p, juce::PathStrokeType(1.6f));
        juce::Path arrow;
        arrow.addTriangle(cx - 5.f, cy - 5.f, cx - 1.f, cy - 8.f, cx - 1.f, cy - 2.f);
        g.fillPath(arrow);
    }

    void drawRedoIcon(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);
        float cx = b.getCentreX(), cy = b.getCentreY();
        juce::Path p;
        p.startNewSubPath(cx - 5.f, cy + 3.f);
        p.quadraticTo(cx - 5.f, cy - 5.f, cx + 3.f, cy - 5.f);
        g.strokePath(p, juce::PathStrokeType(1.6f));
        juce::Path arrow;
        arrow.addTriangle(cx + 5.f, cy - 5.f, cx + 1.f, cy - 8.f, cx + 1.f, cy - 2.f);
        g.fillPath(arrow);
    }

    void showPopup(int menuIdx)
    {
        auto& m   = menus_[menuIdx];
        auto& km  = KeyBindingManager::getInstance();
        auto& t   = Theme::getInstance();
        bool isSettings = (m.title == "Settings");

        juce::PopupMenu pm;
        for (auto& item : m.items)
        {
            if (item.label == "---") { pm.addSeparator(); continue; }

            // Settings menu — special handling for depth mode ticks + profile submenu
            if (isSettings)
            {
                if (item.label == "Interface: Standard")
                {
                    juce::PopupMenu::Item pi("Interface: Standard");
                    pi.isTicked = (t.depthMode == Theme::InterfaceDepthMode::Standard);
                    pi.isEnabled = true;
                    pi.action = item.action;
                    pm.addItem(pi);
                    continue;
                }
                if (item.label == "Interface: Immersive 3D")
                {
                    juce::PopupMenu::Item pi("Interface: Immersive 3D");
                    pi.isTicked = (t.depthMode == Theme::InterfaceDepthMode::Immersive3D);
                    pi.isEnabled = true;
                    pi.action = item.action;
                    pm.addItem(pi);
                    continue;
                }
                if (item.label == "Keybinding Profile")
                {
                    // Add profile items directly (avoids JUCE 8 submenu action issues)
                    auto profileNames = km.getProfileNames();
                    auto activeProfile = km.getActiveProfile().name;
                    pm.addSeparator();
                    for (auto& name : profileNames)
                    {
                        juce::PopupMenu::Item pi(name);
                        pi.isTicked = name.equalsIgnoreCase(activeProfile);
                        pi.isEnabled = true;
                        pi.action = [name] {
                            KeyBindingManager::getInstance().loadProfile(name);
                        };
                        pm.addItem(pi);
                    }
                    pm.addSeparator();
                    continue;
                }
            }

            // Standard item with optional shortcut hint
            juce::PopupMenu::Item pi(item.label);

            // Resolve the effective action: prefer explicit action, otherwise dispatch via ActionManager
            auto effectiveAction = item.action;
            if (!effectiveAction && item.actionId != ActionID::None)
            {
                auto id = item.actionId;
                effectiveAction = [id]() { ActionManager::getInstance().dispatch(id); };
            }
            pi.isEnabled = (effectiveAction != nullptr);
            pi.action    = effectiveAction;

            // Live Undo/Redo entries - pro-DAW Edit menus show WHAT the next
            // step is ("Undo Delete Clip", "Redo Move Clip") and grey out
            // when that side of the history is empty.
            if (item.actionId == ActionID::EditUndo)
            {
                auto& cm = CommandManager::getInstance();
                const auto d = cm.getUndoDescription();
                pi.text = d.isEmpty() ? juce::String("Undo") : "Undo " + d;
                pi.isEnabled = pi.isEnabled && cm.canUndo();
            }
            else if (item.actionId == ActionID::EditRedo)
            {
                auto& cm = CommandManager::getInstance();
                const auto d = cm.getRedoDescription();
                pi.text = d.isEmpty() ? juce::String("Redo") : "Redo " + d;
                pi.isEnabled = pi.isEnabled && cm.canRedo();
            }

            if (item.actionId != ActionID::None)
            {
                auto key = km.getKeyDescription(item.actionId);
                if (key.isNotEmpty())
                    pi.shortcutKeyDescription = key;
            }
            pm.addItem(pi);
        }

        auto rect = menuRects_[menuIdx];
        pm.showMenuAsync(juce::PopupMenu::Options()
                             .withTargetComponent(this)
                             .withTargetScreenArea(localAreaToGlobal(rect)));
        openMenu_ = -1;
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWMenuBar)
};

} // namespace DAW
