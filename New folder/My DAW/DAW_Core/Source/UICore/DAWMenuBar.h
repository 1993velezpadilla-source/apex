#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
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
                             {"---"}, {"Export WAV...", nullptr} } },
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
                             {"Piano Keyboard", nullptr, ActionID::ViewToggleKeyboard},
                             {"---"}, {"Full Screen", nullptr, ActionID::ViewToggleFullScreen} } },
            { "Track",    { {"Add Audio Track", nullptr, ActionID::TrackAddAudio},
                             {"Add MIDI Track", nullptr},
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
        g.fillAll(t.colors.backgroundDark);

        // Bottom separator
        g.setColour(t.colors.border);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);

        // Brand — accent letter + neutral "CORE"
        g.setFont(t.fonts.bold);
        g.setColour(t.colors.accent);
        g.drawText("DAW", 12, 0, 46, getHeight(), juce::Justification::centredLeft);
        g.setColour(t.colors.textSecondary);
        g.drawText("CORE", 50, 0, 54, getHeight(), juce::Justification::centredLeft);

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
                g.setColour(t.colors.accent.withAlpha(0.20f));
                g.fillRect(menuRects_[i]);
                g.setColour(t.colors.accent);
                g.fillRect(menuRects_[i].getX(), getHeight() - 2, menuRects_[i].getWidth(), 2);
            }
            else if (hov)
            {
                g.setColour(t.colors.surfaceHover);
                g.fillRect(menuRects_[i]);
            }

            g.setColour(open ? juce::Colours::white
                             : hov ? t.colors.text : t.colors.textSecondary);
            g.drawText(menus_[i].title, menuRects_[i], juce::Justification::centred);
            x += 72;
        }

        // ── Right-aligned Undo / Redo buttons ─────────────────────────────────
        float btnW = 28.f, btnH = 20.f, btnGap = 2.f, margin = 12.f;
        float btnY = (getHeight() - btnH) * 0.5f;
        redoBtnBounds_  = { (float)getWidth() - margin - btnW, btnY, btnW, btnH };
        undoBtnBounds_  = { redoBtnBounds_.getX() - btnGap - btnW, btnY, btnW, btnH };

        bool canUndo = CommandManager::getInstance().canUndo();
        bool canRedo = CommandManager::getInstance().canRedo();

        // Undo button
        {
            bool bHov = (hoveredButton_ == 1);
            auto col  = canUndo ? (bHov ? t.colors.text : t.colors.textSecondary)
                                : t.colors.textSecondary.withAlpha(0.25f);
            if (bHov && canUndo)
            {
                g.setColour(t.colors.surfaceHover);
                g.fillRoundedRectangle(undoBtnBounds_, 4.f);
            }
            drawUndoIcon(g, undoBtnBounds_, col);
        }
        // Redo button
        {
            bool bHov = (hoveredButton_ == 2);
            auto col  = canRedo ? (bHov ? t.colors.text : t.colors.textSecondary)
                                : t.colors.textSecondary.withAlpha(0.25f);
            if (bHov && canRedo)
            {
                g.setColour(t.colors.surfaceHover);
                g.fillRoundedRectangle(redoBtnBounds_, 4.f);
            }
            drawRedoIcon(g, redoBtnBounds_, col);
        }
    }

    void resized() override
    {
        menuRects_.resize(menus_.size());
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

private:
    std::vector<Menu> menus_;
    std::vector<juce::Rectangle<int>> menuRects_;
    int hoveredMenu_   = -1;
    int openMenu_      = -1;
    int hoveredButton_ = 0;  // 0=none, 1=undo, 2=redo
    mutable juce::Rectangle<float> undoBtnBounds_, redoBtnBounds_;

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
