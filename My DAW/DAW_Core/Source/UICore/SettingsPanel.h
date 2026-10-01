#pragma once
#include <JuceHeader.h>
#include <array>
#include "../ThemeCore/Theme.h"
#include "../Bubblegum/BubblegumAppearanceSettings.h"
#include "../ControlsCore/ModernControls.h"
#include "../GamepadCore/GamepadManager.h"
#include "../InteractionModeCore/InteractionModeManager.h"
#include "../KeyBindingCore/KeyBindingManager.h"
#include "../ShortcutCore/ShortcutEngineCore.h"
#include "../StateCore/ApplicationState.h"
#include "../DeviceCore/AudioDeviceSettingsUI.h"
#include "ControlRoomSettingsUI.h"
#include "BubblegumAppearancePanel.h"

namespace DAW {

// ─── Mode selector button ─────────────────────────────────────────────────────
class ModeButton : public juce::Component
{
public:
    ModeButton(const juce::String& label, InteractionMode mode)
        : label_(label), mode_(mode) {}

    void setSelected(bool s) { selected_ = s; repaint(); }
    bool isSelected() const  { return selected_; }
    InteractionMode getMode() const { return mode_; }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto  b = getLocalBounds().toFloat();

        // Background
        g.setColour(selected_ ? t.colors.accent :
                    hovered_  ? t.colors.surfaceHover : t.colors.surface);
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(selected_ ? t.colors.accent.brighter(0.3f) : t.colors.border);
        g.drawRoundedRectangle(b, 6.f, 1.5f);

        // Icon area
        auto iconArea = b.removeFromTop(b.getHeight() * 0.55f).reduced(6.f);
        juce::Colour ic = selected_ ? juce::Colours::white
                                    : t.colors.textSecondary;
        switch (mode_)
        {
            case InteractionMode::Touchscreen:
                Icons::drawTouchFinger(g, iconArea, ic); break;
            case InteractionMode::Mouse:
                Icons::drawMouse(g, iconArea, ic); break;
            case InteractionMode::Hybrid:
                // Split: touch left, mouse right
                Icons::drawTouchFinger(g, iconArea.removeFromLeft(iconArea.getWidth()*0.5f), ic);
                Icons::drawMouse(g, iconArea, ic);
                break;
        }

        // Label
        g.setColour(selected_ ? juce::Colours::white : t.colors.text);
        g.setFont(t.fonts.small);
        g.drawText(label_, getLocalBounds().removeFromBottom(18),
                   juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override { hovered_ = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovered_ = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(mode_); }

    std::function<void(InteractionMode)> onClick;

private:
    juce::String    label_;
    InteractionMode mode_;
    bool selected_ = false;
    bool hovered_  = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModeButton)
};

    class SettingsContentComponent : public juce::Component
    {
    public:
        struct CardVisual
        {
            juce::Rectangle<int> bounds;
            bool highlighted = false;
        };

        void setCards(const std::vector<CardVisual>& cards)
        {
            cards_ = cards;
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            for (const auto& card : cards_)
            {
                auto bounds = card.bounds.toFloat();
                g.setColour(t.colors.shadow.withAlpha(0.22f));
                g.fillRoundedRectangle(bounds.translated(0.0f, 5.0f), 14.0f);

                juce::ColourGradient fill(t.colors.graphite.withAlpha(0.97f), bounds.getTopLeft(),
                                          t.colors.charcoal.withAlpha(0.95f), bounds.getBottomRight(), false);
                g.setGradientFill(fill);
                g.fillRoundedRectangle(bounds, 14.0f);

                auto sheen = bounds.reduced(1.0f);
                g.setColour(juce::Colours::white.withAlpha(0.04f));
                g.fillRoundedRectangle(sheen.removeFromTop(16.0f), 14.0f);

                g.setColour(card.highlighted ? t.colors.champagne.withAlpha(0.72f)
                                             : t.colors.pewter.withAlpha(0.72f));
                g.drawRoundedRectangle(bounds, 14.0f, 1.1f);

                g.setColour((card.highlighted ? t.colors.copper : t.colors.champagne).withAlpha(card.highlighted ? 0.12f : 0.06f));
                g.drawRoundedRectangle(bounds.reduced(4.0f), 10.0f, 1.0f);
            }
        }

    private:
        std::vector<CardVisual> cards_;
    };

// ─── Section label helper ─────────────────────────────────────────────────────
static void drawSectionLabel(juce::Graphics& g, const juce::String& text,
                             juce::Rectangle<int> bounds)
{
    auto& t = Theme::getInstance();
    g.setColour(t.colors.textSecondary);
    g.setFont(t.fonts.small);
    g.drawText(text.toUpperCase(), bounds, juce::Justification::centredLeft);
    // Line
    int lineY = bounds.getBottom() - 1;
    g.setColour(t.colors.border);
    g.fillRect(bounds.getX(), lineY, bounds.getWidth(), 1);
}

// ─── SettingsPanel ────────────────────────────────────────────────────────────
class SettingsPanel : public juce::Component,
                      public juce::Timer
{
public:
    class BubblegumColourButton : public juce::Component
    {
    public:
        void setColour(juce::Colour c, const juce::String& text)
        {
            colour_ = c;
            label_ = text;
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto r = getLocalBounds().toFloat();
            const bool hov = getLocalBounds().contains(getMouseXYRelative());

            g.setColour(hov ? t.colors.surfaceHover : t.colors.surface);
            g.fillRoundedRectangle(r, 5.f);
            g.setColour(t.colors.border.withAlpha(0.6f));
            g.drawRoundedRectangle(r, 5.f, 1.f);

            auto swatch = r.reduced(6.f).removeFromLeft(24.f);
            g.setColour(colour_);
            g.fillRoundedRectangle(swatch, 4.f);
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.drawRoundedRectangle(swatch, 4.f, 1.f);

            g.setColour(t.colors.text);
            g.setFont(t.fonts.small);
            g.drawText(label_, getLocalBounds().withTrimmedLeft(36), juce::Justification::centredLeft, true);
        }

        void mouseDown(const juce::MouseEvent&) override
        {
            if (onClick)
                onClick();
        }

        std::function<void()> onClick;

    private:
        juce::Colour colour_ = juce::Colours::white;
        juce::String label_;
    };

    class SettingsTabButton : public juce::Component
    {
    public:
        enum class Icon { General, Bubblegum, Workflow, Shortcuts, About };

        SettingsTabButton(const juce::String& label, Icon icon) : label_(label), icon_(icon) {}

        void setSelected(bool selected)
        {
            selected_ = selected;
            repaint();
        }

        std::function<void()> onClick;

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto r = getLocalBounds().toFloat().reduced(0.75f);

            juce::Colour top = selected_ ? t.colors.champagne.withAlpha(0.22f)
                                         : hovered_ ? t.colors.smoke.withAlpha(0.98f)
                                                    : t.colors.graphite.withAlpha(0.96f);
            juce::Colour bottom = selected_ ? t.colors.copper.withAlpha(0.16f)
                                            : hovered_ ? t.colors.charcoal.withAlpha(0.97f)
                                                       : t.colors.charcoal.withAlpha(0.99f);

            g.setGradientFill(juce::ColourGradient(top, r.getTopLeft(), bottom, r.getBottomLeft(), false));
            g.fillRoundedRectangle(r, 10.0f);

            g.setColour(juce::Colours::white.withAlpha(selected_ ? 0.10f : 0.05f));
            g.fillRoundedRectangle(r.withHeight(10.0f), 10.0f);

            g.setColour(selected_ ? t.colors.champagne.withAlpha(0.90f)
                                  : hovered_ ? t.colors.copper.withAlpha(0.52f)
                                             : t.colors.pewter.withAlpha(0.78f));
            g.drawRoundedRectangle(r, 10.0f, selected_ ? 1.2f : 1.0f);

            auto content = r.toNearestInt().reduced(8, 4);
            auto iconArea = content.removeFromLeft(22).toFloat().reduced(1.5f);
            drawIcon(g, iconArea, selected_ ? t.colors.pearl : t.colors.steel.brighter(0.1f));

            content.removeFromLeft(4);
            g.setColour(selected_ ? t.colors.pearl : t.colors.text.withAlpha(0.92f));
            g.setFont(selected_ ? t.fonts.bold : t.fonts.small);
            g.drawText(label_, content, juce::Justification::centredLeft, true);

            if (selected_)
            {
                auto accentBar = r.toNearestInt().removeFromBottom(3).reduced(12, 0).toFloat();
                g.setColour(t.colors.champagne.withAlpha(0.95f));
                g.fillRoundedRectangle(accentBar, 1.5f);
            }
        }

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }
        void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }

    private:
        void drawIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) const
        {
            g.setColour(colour);
            switch (icon_)
            {
                case Icon::General:
                    Icons::drawGear(g, area, colour);
                    break;
                case Icon::Bubblegum:
                    g.fillEllipse(area.reduced(area.getWidth() * 0.15f));
                    break;
                case Icon::Workflow:
                {
                    juce::Path p;
                    p.startNewSubPath(area.getX(), area.getCentreY());
                    p.lineTo(area.getCentreX(), area.getY());
                    p.lineTo(area.getRight(), area.getBottom());
                    g.strokePath(p, juce::PathStrokeType(1.5f));
                    break;
                }
                case Icon::Shortcuts:
                {
                    g.drawRoundedRectangle(area.reduced(1.f), 3.f, 1.3f);
                    g.drawLine(area.getCentreX(), area.getY() + 2.f, area.getCentreX(), area.getBottom() - 2.f, 1.2f);
                    break;
                }
                case Icon::About:
                    g.drawEllipse(area.reduced(1.f), 1.3f);
                    g.setFont(juce::Font(10.f, juce::Font::bold));
                    g.drawText("i", area.toNearestInt(), juce::Justification::centred, false);
                    break;
            }
        }

        juce::String label_;
        Icon icon_;
        bool selected_ = false;
        bool hovered_ = false;
    };

    SettingsPanel()
    {
        auto& t = Theme::getInstance();

        auto styleActionButton = [&](juce::TextButton& button, bool accent = false)
        {
            button.setColour(juce::TextButton::buttonColourId,
                             accent ? t.colors.champagne.withAlpha(0.16f)
                                    : t.colors.smoke.withAlpha(0.96f));
            button.setColour(juce::TextButton::buttonOnColourId,
                             accent ? t.colors.copper.withAlpha(0.22f)
                                    : t.colors.charcoal.withAlpha(0.98f));
            button.setColour(juce::TextButton::textColourOffId,
                             accent ? t.colors.pearl : t.colors.text);
            button.setColour(juce::TextButton::textColourOnId, t.colors.pearl);
        };

        auto styleComboBox = [&](juce::ComboBox& combo)
        {
            combo.setColour(juce::ComboBox::backgroundColourId, t.colors.graphite.withAlpha(0.98f));
            combo.setColour(juce::ComboBox::outlineColourId, t.colors.pewter.withAlpha(0.82f));
            combo.setColour(juce::ComboBox::textColourId, t.colors.text);
            combo.setColour(juce::ComboBox::arrowColourId, t.colors.champagne.withAlpha(0.88f));
            combo.setColour(juce::ComboBox::buttonColourId, t.colors.smoke.withAlpha(0.95f));
        };

        auto styleCollapseButton = [&](juce::TextButton& button)
        {
            button.setColour(juce::TextButton::buttonColourId, t.colors.surface.withAlpha(0.01f));
            button.setColour(juce::TextButton::buttonOnColourId, t.colors.surface.withAlpha(0.01f));
            button.setColour(juce::TextButton::textColourOffId, t.colors.champagne.withAlpha(0.82f));
            button.setColour(juce::TextButton::textColourOnId, t.colors.pearl);
        };

        auto addContentComponent = [this](juce::Component* c)
        {
            content_.addAndMakeVisible(c);
            contentComponents_.push_back(c);
        };

        generalTabBtn_ = std::make_unique<SettingsTabButton>("General", SettingsTabButton::Icon::General);
        bubblegumTabBtn_ = std::make_unique<SettingsTabButton>("Bubblegum", SettingsTabButton::Icon::Bubblegum);
        workflowTabBtn_ = std::make_unique<SettingsTabButton>("Workflow", SettingsTabButton::Icon::Workflow);
        shortcutsTabBtn_ = std::make_unique<SettingsTabButton>("Shortcuts", SettingsTabButton::Icon::Shortcuts);
        aboutTabBtn_ = std::make_unique<SettingsTabButton>("About", SettingsTabButton::Icon::About);

        generalTabBtn_->onClick = [this]() { setCurrentTab(Tab::General); };
        bubblegumTabBtn_->onClick = [this]() { setCurrentTab(Tab::Bubblegum); };
        workflowTabBtn_->onClick = [this]() { setCurrentTab(Tab::Workflow); };
        shortcutsTabBtn_->onClick = [this]() { setCurrentTab(Tab::Shortcuts); };
        aboutTabBtn_->onClick = [this]() { setCurrentTab(Tab::About); };

        addAndMakeVisible(generalTabBtn_.get());
        addAndMakeVisible(bubblegumTabBtn_.get());
        addAndMakeVisible(workflowTabBtn_.get());
        addAndMakeVisible(shortcutsTabBtn_.get());
        addAndMakeVisible(aboutTabBtn_.get());

        contentViewport_.setViewedComponent(&content_, false);
        contentViewport_.setScrollBarsShown(true, false);
        contentViewport_.setScrollBarThickness(8);
        contentViewport_.setColour(juce::ScrollBar::thumbColourId, t.colors.steel.withAlpha(0.70f));
        contentViewport_.setColour(juce::ScrollBar::trackColourId, t.colors.panelDepth1.withAlpha(0.92f));
        addAndMakeVisible(contentViewport_);

        searchEditor_ = std::make_unique<juce::TextEditor>();
        searchEditor_->setTextToShowWhenEmpty("Search settings", t.colors.textSecondary);
        searchEditor_->setColour(juce::TextEditor::backgroundColourId, t.colors.graphite.withAlpha(0.99f));
        searchEditor_->setColour(juce::TextEditor::outlineColourId, t.colors.pewter.withAlpha(0.82f));
        searchEditor_->setColour(juce::TextEditor::focusedOutlineColourId, t.colors.champagne.withAlpha(0.92f));
        searchEditor_->setColour(juce::TextEditor::textColourId, t.colors.text);
        searchEditor_->setColour(juce::TextEditor::highlightColourId, t.colors.champagne.withAlpha(0.24f));
        searchEditor_->setColour(juce::TextEditor::highlightedTextColourId, t.colors.pearl);
        searchEditor_->setColour(juce::CaretComponent::caretColourId, t.colors.champagne.brighter(0.12f));
        searchEditor_->setFont(t.fonts.small.withHeight(13.0f));
        searchEditor_->setIndents(10, 0);
        searchEditor_->onTextChange = [this]()
        {
            searchQuery_ = searchEditor_->getText().trim();
            contentAlpha_ = 0.90f;
            contentTargetAlpha_ = 1.0f;
            contentViewport_.setAlpha(contentAlpha_);
            layoutCurrentTab();
        };
        addAndMakeVisible(searchEditor_.get());

        touchBtn_   = std::make_unique<ModeButton>("Touch",   InteractionMode::Touchscreen);
        hybridBtn_  = std::make_unique<ModeButton>("Hybrid",  InteractionMode::Hybrid);
        desktopBtn_ = std::make_unique<ModeButton>("Desktop", InteractionMode::Mouse);
        touchBtn_->onClick = hybridBtn_->onClick = desktopBtn_->onClick = [this](InteractionMode m) { setMode(m); };

        gamepadToggle_ = std::make_unique<ToggleButton>("Gamepad Support");
        addContentComponent(gamepadToggle_.get());

        touchBtn_->setInterceptsMouseClicks(true, true);
        hybridBtn_->setInterceptsMouseClicks(true, true);
        desktopBtn_->setInterceptsMouseClicks(true, true);
        addContentComponent(touchBtn_.get());
        addContentComponent(hybridBtn_.get());
        addContentComponent(desktopBtn_.get());

        audioDeviceBtn_ = std::make_unique<juce::TextButton>("Audio Device");
        audioDeviceBtn_->onClick = [this]() { if (onOpenAudioDevice) onOpenAudioDevice(); };
        styleActionButton(*audioDeviceBtn_, true);
        addContentComponent(audioDeviceBtn_.get());

        controlRoomBtn_ = std::make_unique<juce::TextButton>("Control Room / Monitor");
        controlRoomBtn_->onClick = [this]() { if (onOpenControlRoom) onOpenControlRoom(); };
        styleActionButton(*controlRoomBtn_);
        addContentComponent(controlRoomBtn_.get());

        waveformScrollToggle_ = std::make_unique<ToggleButton>("Keep waveforms visible while scrolling");
        waveformScrollToggle_->setToggleState(true);
        addContentComponent(waveformScrollToggle_.get());

        allTracksFullDepthToggle_ = std::make_unique<ToggleButton>("Full 3D depth + spheres on all tracks");
        allTracksFullDepthToggle_->setToggleState(false);
        allTracksFullDepthToggle_->onClick = [this](bool v)
        {
            if (onAllTracksFullDepthChanged) onAllTracksFullDepthChanged(v);
        };
        addContentComponent(allTracksFullDepthToggle_.get());

        bubblegumAutoScrollToggle_ = std::make_unique<ToggleButton>("Auto-scroll to send target");
        bubblegumAutoScrollToggle_->setToggleState(true);
        bubblegumAutoScrollToggle_->onClick = [this](bool) { applyBubblegumRoutingSettings(); };
        addContentComponent(bubblegumAutoScrollToggle_.get());

        bubblegumAutoCloseToggle_ = std::make_unique<ToggleButton>("Auto-close offscreen panel");
        bubblegumAutoCloseToggle_->setToggleState(false);
        bubblegumAutoCloseToggle_->onClick = [this](bool) { applyBubblegumRoutingSettings(); };
        addContentComponent(bubblegumAutoCloseToggle_.get());

        bubblegumKeepCablesVisibleToggle_ = std::make_unique<ToggleButton>("Keep cables visible when Bubblegum is closed");
        bubblegumKeepCablesVisibleToggle_->setToggleState(true);
        bubblegumKeepCablesVisibleToggle_->onClick = [this](bool) { applyBubblegumRoutingSettings(); };
        addContentComponent(bubblegumKeepCablesVisibleToggle_.get());

        bubblegumKeepOffscreenVisibleToggle_ = std::make_unique<ToggleButton>("Keep offscreen panel visible when Bubblegum is closed");
        bubblegumKeepOffscreenVisibleToggle_->setToggleState(true);
        bubblegumKeepOffscreenVisibleToggle_->onClick = [this](bool) { applyBubblegumRoutingSettings(); };
        addContentComponent(bubblegumKeepOffscreenVisibleToggle_.get());

        bubblegumAppearancePanel_ = std::make_unique<BubblegumAppearancePanel>();
        bubblegumAppearancePanel_->onSelectedGlassColourChanged = [this](juce::Colour colour)
        {
            selectedTrackColour_ = colour;
            if (onBubblegumSelectedTrackColourChanged)
                onBubblegumSelectedTrackColourChanged(selectedTrackFamily_, selectedTrackPresetIndex_, colour);
        };
        bubblegumAppearancePanel_->onBubblegumTargetColourChanged = [this](juce::Colour colour)
        {
            routingTargetColour_ = colour;
            if (onBubblegumRoutingTargetColourChanged)
                onBubblegumRoutingTargetColourChanged(colour);
        };
        bubblegumAppearancePanel_->onCableBodyBottomChanged = [this](juce::Colour colour)
        {
            cableColour_ = colour;
            if (onBubblegumCableColourChanged)
                onBubblegumCableColourChanged(cableFamily_, cablePresetIndex_, colour);
        };
        bubblegumAppearancePanel_->onCableThicknessChanged = [this](float thickness)
        {
            cableThickness_ = thickness;
            if (onBubblegumCableThicknessChanged)
                onBubblegumCableThicknessChanged(thickness);
        };
        bubblegumAppearancePanel_->onMasterTrackColourChanged = [this](juce::Colour colour)
        {
            masterTrackColour_ = colour;
            if (onMasterTrackAccentColourChanged)
                onMasterTrackAccentColourChanged(colour);
        };
        bubblegumAppearancePanel_->onSendPillModeChanged = [this](BubblegumAppearancePanel::SendPillMode mode)
        {
            if (onBubblegumSendPillModeChanged)
                onBubblegumSendPillModeChanged((int) mode);
        };
        bubblegumAppearancePanel_->onShowSendBadgesChanged = [this](bool show)
        {
            if (onShowSendBadgesChanged)
                onShowSendBadgesChanged(show);
        };
        bubblegumAppearancePanel_->onShowSidechainBadgesChanged = [this](bool show)
        {
            if (onShowSidechainBadgesChanged)
                onShowSidechainBadgesChanged(show);
        };
        bubblegumAppearancePanel_->onSidechainCableThicknessChanged = [this](float thickness)
        {
            if (onSidechainCableThicknessChanged)
                onSidechainCableThicknessChanged(thickness);
        };
        addContentComponent(bubblegumAppearancePanel_.get());

        folderDropModeCombo_ = std::make_unique<juce::ComboBox>();
        folderDropModeCombo_->addItem("Always convert destination track", 1);
        folderDropModeCombo_->addItem("Create a new folder bus (currently active)", 2);
        folderDropModeCombo_->addItem("Always ask on drop", 3);
        folderDropModeCombo_->setSelectedId(2, juce::dontSendNotification);
        folderDropModeCombo_->onChange = [this]() { applyFolderDropModeSetting(); };
        styleComboBox(*folderDropModeCombo_);
        addContentComponent(folderDropModeCombo_.get());

        autoFadeOnSplitToggle_ = std::make_unique<ToggleButton>("Auto fade when splitting clips");
        autoFadeOnSplitToggle_->setToggleState(true);
        autoFadeOnSplitToggle_->onClick = [this](bool enabled)
        {
            if (onAutoFadeOnSplitChanged)
                onAutoFadeOnSplitChanged(enabled);
        };
        addContentComponent(autoFadeOnSplitToggle_.get());

        profileCombo_ = std::make_unique<juce::ComboBox>();
        auto profileNames = KeyBindingManager::getInstance().getProfileNames();
        for (int i = 0; i < profileNames.size(); ++i)
            profileCombo_->addItem(profileNames[i], i + 1);
        profileCombo_->setSelectedItemIndex(0, juce::dontSendNotification);
        styleComboBox(*profileCombo_);
        addContentComponent(profileCombo_.get());

        applyProfileBtn_ = std::make_unique<juce::TextButton>("Apply");
        applyProfileBtn_->onClick = [this]()
        {
            int idx = profileCombo_->getSelectedItemIndex();
            auto names = KeyBindingManager::getInstance().getProfileNames();
            if (idx >= 0 && idx < names.size())
            {
                KeyBindingManager::getInstance().loadProfile(names[idx]);
                ShortcutProfileManager::getInstance().setActiveByName(names[idx]);
                profileAppliedName_ = names[idx];
                profileAppliedTimer_ = 90;
                updateProfileAppliedLabel();
            }
            if (onProfileApplied) onProfileApplied();
        };
        styleActionButton(*applyProfileBtn_, true);
        addContentComponent(applyProfileBtn_.get());

        shortcutHelpBtn_ = std::make_unique<juce::TextButton>("?");
        shortcutHelpBtn_->onClick = [this]() { if (onOpenShortcutHelp) onOpenShortcutHelp(); };
        styleActionButton(*shortcutHelpBtn_);
        addContentComponent(shortcutHelpBtn_.get());

        shortcutStyleCombo_ = std::make_unique<juce::ComboBox>();
        shortcutStyleCombo_->addItem("Windows", 1);
        shortcutStyleCombo_->addItem("Apple", 2);
        shortcutStyleCombo_->setSelectedItemIndex(0, juce::dontSendNotification);
        shortcutStyleCombo_->onChange = [this]()
        {
            auto& psm = PlatformStyleManager::getInstance();
            psm.setShortcutStyle(shortcutStyleCombo_->getSelectedItemIndex() == 1
                ? PlatformStyleManager::ShortcutStyle::Apple
                : PlatformStyleManager::ShortcutStyle::Windows);
        };
        styleComboBox(*shortcutStyleCombo_);
        addContentComponent(shortcutStyleCombo_.get());

        guiStyleCombo_ = std::make_unique<juce::ComboBox>();
        guiStyleCombo_->addItem("Windows", 1);
        guiStyleCombo_->addItem("Apple", 2);
        guiStyleCombo_->setSelectedItemIndex(0, juce::dontSendNotification);
        guiStyleCombo_->onChange = [this]()
        {
            auto& psm = PlatformStyleManager::getInstance();
            psm.setGUIStyle(guiStyleCombo_->getSelectedItemIndex() == 1
                ? PlatformStyleManager::GUIStyle::Apple
                : PlatformStyleManager::GUIStyle::Windows);
        };
        styleComboBox(*guiStyleCombo_);
        addContentComponent(guiStyleCombo_.get());

        createLabel(generalHeaderLabel_, "Hardware / Configuration", true, addContentComponent);
        createLabel(generalDescriptionLabel_, "Device and global visual behaviour", false, addContentComponent);
        createLabel(scrollingHeaderLabel_, "Scrolling / Performance", true, addContentComponent);
        createLabel(scrollingDescriptionLabel_, "Timeline and viewport performance options", false, addContentComponent);
        createLabel(mixerVisualHeaderLabel_, "Mixer Visual", true, addContentComponent);
        createLabel(mixerVisualDescriptionLabel_, "Enable premium 3D on every strip (selected strip gets a bright outline)", false, addContentComponent);
        createLabel(bubblegumRoutingHeaderLabel_, "Bubblegum Routing", true, addContentComponent);
        createLabel(bubblegumRoutingDescriptionLabel_, "Controls offscreen panel and cable visibility", false, addContentComponent);
        createLabel(bubblegumAppearanceHeaderLabel_, "Bubblegum Appearance", true, addContentComponent);
        createLabel(bubblegumAppearanceDescriptionLabel_, "Live colour preview for selected tracks and cables", false, addContentComponent);
        createLabel(folderHeaderLabel_, "Folder Bus Drag & Drop", true, addContentComponent);
        createLabel(folderDescriptionLabel_, "Choose what happens when one track is dropped onto another", false, addContentComponent);
        createLabel(folderDropComboLabel_, "Drop Behavior", false, addContentComponent);
        createLabel(shortcutsHeaderLabel_, "Key Binding Profile", true, addContentComponent);
        createLabel(shortcutsDescriptionLabel_, "Match shortcuts to your previous DAW", false, addContentComponent);
        createLabel(profileAppliedLabel_, {}, false, addContentComponent);
        createLabel(interfaceHeaderLabel_, "Interface & Shortcuts", true, addContentComponent);
        createLabel(shortcutStyleLabel_, "Shortcut Platform Style", false, addContentComponent);
        createLabel(guiStyleLabel_, "GUI Visual Style", false, addContentComponent);
        createLabel(aboutHeaderLabel_, "About", true, addContentComponent);
        createLabel(aboutTextLabel_, "DAW Core v0.1 | Modular Architecture", false, addContentComponent);
        createLabel(noResultsLabel_, "No settings match this search.", false, addContentComponent);

        createCollapseButton(mixerBubblesCollapseBtn_, SectionId::MixerBubbles, addContentComponent);
        styleCollapseButton(*mixerBubblesCollapseBtn_);

        createLabel(mixerBubblesHeaderLabel_, "Mixer Bubbles Colour", true, addContentComponent);
        createLabel(mixerBubblesDescLabel_, "Background floating sphere palette", false, addContentComponent);

        const auto& presets = getBubblePresets();
        for (int i = 0; i < kBubblePresetCount; ++i)
        {
            bubblePresetBtns_[i] = std::make_unique<juce::TextButton>(presets[i].name);
            bubblePresetBtns_[i]->setColour(juce::TextButton::buttonColourId, presets[i].deep.withAlpha(0.85f));
            bubblePresetBtns_[i]->setColour(juce::TextButton::buttonOnColourId, presets[i].near.withAlpha(0.90f));
            bubblePresetBtns_[i]->setColour(juce::TextButton::textColourOffId, presets[i].near.brighter(0.6f));
            bubblePresetBtns_[i]->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            bubblePresetBtns_[i]->setClickingTogglesState(true);
            bubblePresetBtns_[i]->setToggleState(i == 0, juce::dontSendNotification);
            const int idx = i;
            bubblePresetBtns_[i]->onClick = [this, idx]()
            {
                selectedBubblePreset_ = idx;
                for (int j = 0; j < kBubblePresetCount; ++j)
                    if (bubblePresetBtns_[j])
                        bubblePresetBtns_[j]->setToggleState(j == idx, juce::dontSendNotification);
                const auto& p = getBubblePresets()[idx];
                if (onMixerBubbleColourChanged)
                    onMixerBubbleColourChanged(p.deep, p.near);
            };
            addContentComponent(bubblePresetBtns_[i].get());
        }
        createCollapseButton(generalCollapseBtn_,     SectionId::GeneralHardware,   addContentComponent);
        createCollapseButton(scrollingCollapseBtn_,   SectionId::GeneralScrolling,  addContentComponent);
        createCollapseButton(mixerVisualCollapseBtn_, SectionId::GeneralMixerVisual, addContentComponent);
        createCollapseButton(bubblegumRoutingCollapseBtn_, SectionId::BubblegumRouting, addContentComponent);
        createCollapseButton(bubblegumAppearanceCollapseBtn_, SectionId::BubblegumAppearance, addContentComponent);
        createCollapseButton(workflowCollapseBtn_, SectionId::WorkflowFolder, addContentComponent);
        createCollapseButton(shortcutsCollapseBtn_, SectionId::ShortcutsProfile, addContentComponent);
        createCollapseButton(interfaceCollapseBtn_, SectionId::ShortcutsInterface, addContentComponent);
        createCollapseButton(aboutCollapseBtn_, SectionId::AboutInfo, addContentComponent);

        styleCollapseButton(*generalCollapseBtn_);
        styleCollapseButton(*mixerBubblesCollapseBtn_);
        styleCollapseButton(*scrollingCollapseBtn_);
        styleCollapseButton(*mixerVisualCollapseBtn_);
        styleCollapseButton(*bubblegumRoutingCollapseBtn_);
        styleCollapseButton(*bubblegumAppearanceCollapseBtn_);
        styleCollapseButton(*workflowCollapseBtn_);
        styleCollapseButton(*shortcutsCollapseBtn_);
        styleCollapseButton(*interfaceCollapseBtn_);
        styleCollapseButton(*aboutCollapseBtn_);

        setSize(344, 760);
        startTimerHz(60);
        updateModeButtons(InteractionMode::Mouse);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
        setCurrentTab(Tab::General);
    }

    ~SettingsPanel() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        juce::ColourGradient shellFill(t.colors.obsidian, bounds.getTopLeft(),
                                       t.colors.graphite.brighter(0.05f), bounds.getBottomRight(), false);
        g.setGradientFill(shellFill);
        g.fillRoundedRectangle(bounds, 16.0f);

        auto topSheen = bounds;
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRoundedRectangle(topSheen.removeFromTop(20.0f), 16.0f);

        g.setColour(t.colors.pewter.withAlpha(0.90f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 16.0f, 1.0f);

        g.setColour(t.colors.champagne.withAlpha(0.10f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(5.0f), 12.0f, 1.0f);

        auto shell = getLocalBounds().reduced(10);
        auto titleRow = shell.removeFromTop(40);
        auto searchRow = shell.removeFromTop(42);
        auto tabRow = shell.removeFromTop(88);
        auto content = shell.reduced(0, 4);

        auto titleText = titleRow.removeFromLeft(titleRow.getWidth() - 88);
        g.setColour(t.colors.text);
        g.setFont(t.fonts.bold.withHeight(18.0f));
        g.drawText("Settings", titleText.removeFromTop(22), juce::Justification::centredLeft, false);

        g.setColour(t.colors.textSecondary.withAlpha(0.82f));
        g.setFont(t.fonts.small);
        g.drawText("Studio settings for workflow and Bubblegum.",
                   titleText, juce::Justification::centredLeft, true);

        auto pill = titleRow.removeFromRight(78).reduced(0, 6).toFloat();
        g.setColour(t.colors.smoke.withAlpha(0.98f));
        g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
        g.setColour(t.colors.champagne.withAlpha(0.62f));
        g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.0f);
        g.setColour(t.colors.champagne.withAlpha(0.90f));
        g.setFont(t.fonts.small);
        g.drawText(getTabTitle(currentTab_), pill.toNearestInt(), juce::Justification::centred, false);

        auto searchFrame = searchRow.reduced(0, 4).toFloat();
        g.setColour(t.colors.graphite.withAlpha(0.98f));
        g.fillRoundedRectangle(searchFrame, 10.0f);
        g.setColour(t.colors.pewter.withAlpha(0.78f));
        g.drawRoundedRectangle(searchFrame, 10.0f, 1.0f);

        g.setColour(t.colors.graphite.withAlpha(0.98f));
        g.fillRoundedRectangle(tabRow.toFloat(), 12.0f);
        g.setColour(t.colors.pewter.withAlpha(0.58f));
        g.drawRoundedRectangle(tabRow.toFloat(), 12.0f, 1.0f);

        g.setColour(t.colors.charcoal.withAlpha(0.94f));
        g.fillRoundedRectangle(content.toFloat(), 12.0f);
        g.setColour(t.colors.pewter.withAlpha(0.42f));
        g.drawRoundedRectangle(content.toFloat(), 12.0f, 1.0f);

        g.setColour(t.colors.textSecondary.withAlpha(0.78f));
        g.setFont(t.fonts.small);
        g.drawText(getTabDescription(currentTab_),
                   searchRow.withTrimmedLeft(8).removeFromBottom(14),
                   juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto shell = getLocalBounds().reduced(10);
        auto titleRow = shell.removeFromTop(40);
        auto searchRow = shell.removeFromTop(42);
        auto tabsArea = shell.removeFromTop(88);

        searchEditor_->setBounds(searchRow.reduced(2, 6));

        const int gap = 6;
        auto tabFrame = tabsArea.reduced(6);
        auto topTabs = tabFrame.removeFromTop(34);
        tabFrame.removeFromTop(6);
        auto bottomTabs = tabFrame.removeFromTop(34);
        const int topW = (topTabs.getWidth() - gap * 2) / 3;
        const int bottomW = (bottomTabs.getWidth() - gap) / 2;

        generalTabBtn_->setBounds(topTabs.removeFromLeft(topW));
        topTabs.removeFromLeft(gap);
        bubblegumTabBtn_->setBounds(topTabs.removeFromLeft(topW));
        topTabs.removeFromLeft(gap);
        workflowTabBtn_->setBounds(topTabs);

        shortcutsTabBtn_->setBounds(bottomTabs.removeFromLeft(bottomW));
        bottomTabs.removeFromLeft(gap);
        aboutTabBtn_->setBounds(bottomTabs);

        auto contentArea = shell.reduced(2, 8);
        contentViewport_.setBounds(contentArea);
        layoutCurrentTab();
    }

    // ── public API ───────────────────────────────────────────────────────────
    void setGamepadManager(GamepadManager* m)          { gamepadManager_ = m; }
    void setInteractionManager(InteractionModeManager* m)
    {
        interactionManager_ = m;
        if (m) updateModeButtons(m->getMode());
    }

    ToggleButton* getGamepadToggle()      { return gamepadToggle_.get(); }
    ToggleButton* getWaveformScrollToggle() { return waveformScrollToggle_.get(); }
    ToggleButton* getBubblegumAutoScrollToggle()       { return bubblegumAutoScrollToggle_.get(); }
    ToggleButton* getBubblegumAutoCloseToggle()        { return bubblegumAutoCloseToggle_.get(); }
    ToggleButton* getBubblegumKeepCablesVisibleToggle(){ return bubblegumKeepCablesVisibleToggle_.get(); }
    ToggleButton* getBubblegumKeepOffscreenVisibleToggle(){ return bubblegumKeepOffscreenVisibleToggle_.get(); }
    InteractionModeManager* getInteractionManager() { return interactionManager_; }

    void setBubblegumRoutingSettings(bool autoScroll, bool autoClose, bool keepCablesVisible, bool keepOffscreenVisible = true)
    {
        suppressBubblegumRoutingCallback_ = true;
        if (bubblegumAutoScrollToggle_)
            bubblegumAutoScrollToggle_->setToggleState(autoScroll);
        if (bubblegumAutoCloseToggle_)
            bubblegumAutoCloseToggle_->setToggleState(autoClose);
        if (bubblegumKeepCablesVisibleToggle_)
            bubblegumKeepCablesVisibleToggle_->setToggleState(keepCablesVisible);
        if (bubblegumKeepOffscreenVisibleToggle_)
            bubblegumKeepOffscreenVisibleToggle_->setToggleState(keepOffscreenVisible);
        suppressBubblegumRoutingCallback_ = false;
    }

    void setBubblegumSelectedTrackColourState(BubblegumAppearanceSettings::PaletteFamily family,
                                              int presetIndex,
                                              juce::Colour colour)
    {
        suppressBubblegumAppearanceCallback_ = true;
        selectedTrackFamily_ = family;
        selectedTrackPresetIndex_ = BubblegumAppearanceSettings::clampPresetIndex(false, family, presetIndex);
        selectedTrackColour_ = colour;
        refreshColourButtons();
        suppressBubblegumAppearanceCallback_ = false;
    }

    void setBubblegumSidechainCableColour(juce::Colour colour)
    {
        suppressBubblegumAppearanceCallback_ = true;
        sidechainCableColour_ = colour;
        refreshColourButtons();
        suppressBubblegumAppearanceCallback_ = false;
    }

    void setBubblegumCableColourState(BubblegumAppearanceSettings::PaletteFamily family,
                                      int presetIndex,
                                      juce::Colour colour)
    {
        suppressBubblegumAppearanceCallback_ = true;
        cableFamily_ = family;
        cablePresetIndex_ = BubblegumAppearanceSettings::clampPresetIndex(true, family, presetIndex);
        cableColour_ = colour;
        refreshColourButtons();
        suppressBubblegumAppearanceCallback_ = false;
    }

    BubblegumAppearanceSettings::PaletteFamily getBubblegumSelectedTrackPaletteFamily() const noexcept { return selectedTrackFamily_; }
    int getBubblegumSelectedTrackPresetIndex() const noexcept { return selectedTrackPresetIndex_; }
    juce::Colour getBubblegumSelectedTrackColour() const noexcept { return selectedTrackColour_; }
    BubblegumAppearanceSettings::PaletteFamily getBubblegumCablePaletteFamily() const noexcept { return cableFamily_; }
    int getBubblegumCablePresetIndex() const noexcept { return cablePresetIndex_; }
    juce::Colour getBubblegumCableColour() const noexcept { return cableColour_; }
    juce::Colour getBubblegumSidechainCableColour() const noexcept { return sidechainCableColour_; }
    BubblegumAppearancePanel* getBubblegumAppearancePanel() const noexcept { return bubblegumAppearancePanel_.get(); }
    int getSelectedTabIndex() const noexcept { return static_cast<int>(currentTab_); }
    void setSelectedTabIndex(int index)
    {
        suppressTabChangeCallback_ = true;
        setCurrentTab((Tab) juce::jlimit(0, 4, index));
        suppressTabChangeCallback_ = false;
    }

    void setFolderDropMode(int mode)
    {
        suppressFolderDropModeCallback_ = true;
        if (folderDropModeCombo_)
            folderDropModeCombo_->setSelectedId(mode + 1, juce::dontSendNotification);
        suppressFolderDropModeCallback_ = false;
    }

    void setAutoFadeOnSplitEnabled(bool enabled)
    {
        if (autoFadeOnSplitToggle_)
            autoFadeOnSplitToggle_->setToggleState(enabled);
    }

    /**
     * Callback fired whenever any Bubblegum Routing toggle changes.
     * Parameters: (autoScroll, autoClose, keepCablesVisible, keepOffscreenVisible).
     */
    std::function<void(bool, bool, bool, bool)> onBubblegumRoutingSettingsChanged;
    std::function<void(int)> onFolderDropModeChanged;
    std::function<void(bool)> onAutoFadeOnSplitChanged;
    /** Fired when the "Full 3D on all tracks" toggle changes. */
    std::function<void(bool)> onAllTracksFullDepthChanged;
    std::function<void(BubblegumAppearanceSettings::PaletteFamily, int, juce::Colour)> onBubblegumSelectedTrackColourChanged;
    std::function<void(BubblegumAppearanceSettings::PaletteFamily, int, juce::Colour)> onBubblegumCableColourChanged;
    std::function<void(juce::Colour)> onBubblegumRoutingTargetColourChanged;
    std::function<void(float)> onBubblegumCableThicknessChanged;
    std::function<void(juce::Colour)> onMasterTrackAccentColourChanged;
    std::function<void(int)> onBubblegumSendPillModeChanged;
    std::function<void(bool)> onShowSendBadgesChanged;
    std::function<void(bool)> onShowSidechainBadgesChanged;
    std::function<void(float)> onSidechainCableThicknessChanged;
    std::function<void(int)> onSelectedTabChanged;

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getModifiers().isCommandDown() && (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F'))
        {
            if (searchEditor_)
            {
                searchEditor_->grabKeyboardFocus();
                searchEditor_->selectAll();
                return true;
            }
        }

        if (key == juce::KeyPress::escapeKey && searchEditor_ != nullptr && searchEditor_->hasKeyboardFocus(true) && searchEditor_->getText().isNotEmpty())
        {
            searchEditor_->clear();
            return true;
        }

        if (key.getModifiers().isCommandDown() && key == juce::KeyPress(juce::KeyPress::tabKey))
        {
            cycleTab(false);
            return true;
        }

        if (key.getModifiers().isCommandDown() && key == juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier | juce::ModifierKeys::commandModifier, 0))
        {
            cycleTab(true);
            return true;
        }

        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey)
        {
            cycleTab(true);
            return true;
        }

        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey)
        {
            cycleTab(false);
            return true;
        }

        if (key.getTextCharacter() >= '1' && key.getTextCharacter() <= '5')
        {
            setSelectedTabIndex((int) (key.getTextCharacter() - '1'));
            return true;
        }

        return false;
    }

    bool getAllTracksFullDepth() const
    {
        return allTracksFullDepthToggle_ && allTracksFullDepthToggle_->getToggleState();
    }
    void setAllTracksFullDepth(bool v)
    {
        if (allTracksFullDepthToggle_)
            allTracksFullDepthToggle_->setToggleState(v);
    }

    /** Callback fired when user clicks "Audio Device" button. */
    std::function<void()> onOpenAudioDevice;
    /** Callback fired when user clicks "Control Room / Monitor" button. */
    std::function<void()> onOpenControlRoom;

    /** Callback fired when user clicks the [?] help button beside profile. */
    std::function<void()> onOpenShortcutHelp;

    /** Callback fired after Apply button applies a profile (grab focus). */
    std::function<void()> onProfileApplied;

    void mouseMove(const juce::MouseEvent&) override { repaint(); }

private:
    enum class Tab { General, Bubblegum, Workflow, Shortcuts, About };
    enum class SectionId
    {
        GeneralHardware = 0,
        GeneralInteraction,
        GeneralScrolling,
        GeneralMixerVisual,
        MixerBubbles,
        BubblegumRouting,
        BubblegumAppearance,
        WorkflowFolder,
        ShortcutsProfile,
        ShortcutsInterface,
        AboutInfo,
        Count
    };

    void createLabel(std::unique_ptr<juce::Label>& label,
                     const juce::String& text,
                     bool isHeader = false,
                     std::function<void(juce::Component*)> addFn = {})
    {
        label = std::make_unique<juce::Label>();
        label->setText(text, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, isHeader ? Theme::getInstance().colors.text : Theme::getInstance().colors.textSecondary);
        label->setFont(isHeader ? Theme::getInstance().fonts.bold : Theme::getInstance().fonts.small);
        if (addFn)
            addFn(label.get());
    }

    void createCollapseButton(std::unique_ptr<juce::TextButton>& button,
                              SectionId id,
                              std::function<void(juce::Component*)> addFn)
    {
        button = std::make_unique<juce::TextButton>(juce::CharPointer_UTF8("\xe2\x96\xbe"));
        button->onClick = [this, id]() { toggleSection(id); };
        addFn(button.get());
    }

    void setCurrentTab(Tab tab)
    {
        currentTab_ = tab;
        generalTabBtn_->setSelected(tab == Tab::General);
        bubblegumTabBtn_->setSelected(tab == Tab::Bubblegum);
        workflowTabBtn_->setSelected(tab == Tab::Workflow);
        shortcutsTabBtn_->setSelected(tab == Tab::Shortcuts);
        aboutTabBtn_->setSelected(tab == Tab::About);
        layoutCurrentTab();
        if (!suppressTabChangeCallback_ && onSelectedTabChanged)
            onSelectedTabChanged((int) currentTab_);
        repaint();
    }

    juce::String getTabTitle(Tab tab) const
    {
        switch (tab)
        {
            case Tab::General:   return "General";
            case Tab::Bubblegum: return "Bubblegum";
            case Tab::Workflow:  return "Workflow";
            case Tab::Shortcuts: return "Shortcuts";
            case Tab::About:     return "About";
        }

        return "Settings";
    }

    juce::String getTabDescription(Tab tab) const
    {
        switch (tab)
        {
            case Tab::General:   return "Hardware, interaction, performance, and mixer visuals.";
            case Tab::Bubblegum: return "Routing behavior and Bubblegum appearance tuning.";
            case Tab::Workflow:  return "Drag/drop and clip editing defaults for faster arrangement work.";
            case Tab::Shortcuts: return "Profiles and interface conventions for your workflow.";
            case Tab::About:     return "Project information and architecture summary.";
        }

        return {};
    }

    void cycleTab(bool backwards)
    {
        int next = getSelectedTabIndex() + (backwards ? -1 : 1);
        if (next < 0) next = 4;
        if (next > 4) next = 0;
        setSelectedTabIndex(next);
    }

    void hideAllContentComponents()
    {
        for (auto* c : contentComponents_)
            c->setVisible(false);
    }

    bool matchesSearch(std::initializer_list<juce::String> phrases) const
    {
        if (searchQuery_.isEmpty())
            return true;

        const auto query = searchQuery_.toLowerCase();
        for (const auto& phrase : phrases)
            if (phrase.toLowerCase().contains(query))
                return true;

        return false;
    }

    int layoutSectionTitle(juce::Label* header, juce::Label* desc, juce::TextButton* collapseBtn, SectionId id, bool highlighted, int x, int y, int w)
    {
        header->setColour(juce::Label::textColourId,
                          highlighted ? Theme::getInstance().colors.accent.brighter(0.25f)
                                      : Theme::getInstance().colors.text);
        header->setVisible(true);
        header->setBounds(x + 14, y, w - 60, 20);
        if (collapseBtn != nullptr)
        {
            collapseBtn->setVisible(true);
            collapseBtn->setButtonText(isSectionCollapsed(id) ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb8"))
                                                              : juce::String(juce::CharPointer_UTF8("\xe2\x96\xbe")));
            collapseBtn->setBounds(x + w - 34, y - 1, 22, 22);
        }
        y += 22;
        if (isSectionCollapsed(id))
            return y;
        if (desc != nullptr)
        {
            desc->setColour(juce::Label::textColourId,
                            highlighted ? Theme::getInstance().colors.text.withAlpha(0.95f)
                                        : Theme::getInstance().colors.textSecondary);
            desc->setVisible(true);
            desc->setBounds(x + 14, y, w - 28, 30);
            y += 34;
        }
        return y;
    }

    int beginCard(int x, int y, int w, int contentHeight, bool highlighted)
    {
        SettingsContentComponent::CardVisual card;
        card.bounds = juce::Rectangle<int>(x, y, w, contentHeight);
        card.highlighted = highlighted;
        cardBounds_.push_back(card);
        return y + 14;
    }

    int endCard(int y)
    {
        if (!cardBounds_.empty())
        {
            auto& r = cardBounds_.back();
            r.bounds.setHeight(juce::jmax(52, y - r.bounds.getY() + 14));
            y = r.bounds.getBottom() + 14;
        }
        return y;
    }

    bool isSectionCollapsed(SectionId id) const noexcept
    {
        return collapsedSections_[(size_t) id];
    }

    void toggleSection(SectionId id)
    {
        collapsedSections_[(size_t) id] = !collapsedSections_[(size_t) id];
        layoutCurrentTab();
    }

    void layoutCurrentTab()
    {
        hideAllContentComponents();
        cardBounds_.clear();

        const int width = juce::jmax(160, contentViewport_.getWidth() - contentViewport_.getScrollBarThickness() - 4);
        const int x = 4;
        const int w = width - 24;
        const int insetX = x + 14;
        const int insetW = w - 28;
        int y = 12;
        bool anythingShown = false;
        const bool searching = searchQuery_.isNotEmpty();

        noResultsLabel_->setVisible(false);

        if ((searching || currentTab_ == Tab::General)
            && matchesSearch({ "hardware", "audio device", "control room", "monitor", "device" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(generalHeaderLabel_.get(), generalDescriptionLabel_.get(), generalCollapseBtn_.get(), SectionId::GeneralHardware, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::GeneralHardware))
            {
            audioDeviceBtn_->setVisible(true);
            audioDeviceBtn_->setBounds(insetX, y, insetW, 30);
            y += 36;
            controlRoomBtn_->setVisible(true);
            controlRoomBtn_->setBounds(insetX, y, insetW, 30);
            y += 12;
            }
            y = endCard(y);

        }

        if ((searching || currentTab_ == Tab::General)
            && matchesSearch({ "scroll", "waveform", "performance", "viewport" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(scrollingHeaderLabel_.get(), scrollingDescriptionLabel_.get(), scrollingCollapseBtn_.get(), SectionId::GeneralScrolling, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::GeneralScrolling))
            {
            waveformScrollToggle_->setVisible(true);
            waveformScrollToggle_->setBounds(insetX, y, insetW, 30);
            y += 12;
            }
            y = endCard(y);

        }

        if ((searching || currentTab_ == Tab::General)
            && matchesSearch({ "mixer", "visual", "3d", "depth", "spheres" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(mixerVisualHeaderLabel_.get(), mixerVisualDescriptionLabel_.get(), mixerVisualCollapseBtn_.get(), SectionId::GeneralMixerVisual, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::GeneralMixerVisual))
            {
            allTracksFullDepthToggle_->setVisible(true);
            allTracksFullDepthToggle_->setBounds(insetX, y, insetW, 30);
            y += 12;
            }
            y = endCard(y);
        }

        if ((searching || currentTab_ == Tab::General)
            && matchesSearch({ "bubbles", "mixer bubbles", "sphere colour", "background colour", "floating" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(mixerBubblesHeaderLabel_.get(), mixerBubblesDescLabel_.get(),
                                   mixerBubblesCollapseBtn_.get(), SectionId::MixerBubbles, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::MixerBubbles))
            {
                const int bw = (insetW - 4) / 3;
                const int bh = 26;
                for (int i = 0; i < kBubblePresetCount; ++i)
                {
                    const int col = i % 3;
                    const int row = i / 3;
                    bubblePresetBtns_[i]->setVisible(true);
                    bubblePresetBtns_[i]->setBounds(insetX + col * (bw + 2),
                                                     y + row * (bh + 4), bw, bh);
                }
                y += 2 * (bh + 4) + 4;
            }
            y = endCard(y);
        }

        if ((searching || currentTab_ == Tab::Bubblegum)
            && matchesSearch({ "bubblegum", "routing", "auto-scroll", "offscreen", "cables" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(bubblegumRoutingHeaderLabel_.get(), bubblegumRoutingDescriptionLabel_.get(), bubblegumRoutingCollapseBtn_.get(), SectionId::BubblegumRouting, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::BubblegumRouting))
            {
            bubblegumAutoScrollToggle_->setVisible(true);
            bubblegumAutoScrollToggle_->setBounds(insetX, y, insetW, 30);
            y += 36;
            bubblegumAutoCloseToggle_->setVisible(true);
            bubblegumAutoCloseToggle_->setBounds(insetX, y, insetW, 30);
            y += 36;
            bubblegumKeepCablesVisibleToggle_->setVisible(true);
            bubblegumKeepCablesVisibleToggle_->setBounds(insetX, y, insetW, 30);
            y += 36;
            bubblegumKeepOffscreenVisibleToggle_->setVisible(true);
            bubblegumKeepOffscreenVisibleToggle_->setBounds(insetX, y, insetW, 30);
            y += 12;
            }
            y = endCard(y);

        }

        if ((searching || currentTab_ == Tab::Bubblegum)
            && matchesSearch({ "bubblegum appearance", "selected track", "cable colour", "neon", "palette", "color" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(bubblegumAppearanceHeaderLabel_.get(), bubblegumAppearanceDescriptionLabel_.get(), bubblegumAppearanceCollapseBtn_.get(), SectionId::BubblegumAppearance, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::BubblegumAppearance))
            {
            bubblegumAppearancePanel_->setVisible(true);
            bubblegumAppearancePanel_->setBounds(insetX, y, insetW, bubblegumAppearancePanel_->getHeight());
            y += bubblegumAppearancePanel_->getHeight() + 8;
            }
            y = endCard(y);
        }

        if ((searching || currentTab_ == Tab::Workflow)
            && matchesSearch({ "workflow", "folder", "drag", "drop", "bus", "fade", "split", "clip cut", "auto fade" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(folderHeaderLabel_.get(), folderDescriptionLabel_.get(), workflowCollapseBtn_.get(), SectionId::WorkflowFolder, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::WorkflowFolder))
            {
            // Folder-bus drag & drop DISABLED (product decision): the
            // "Drop Behavior" control is hidden. The combo and its label are
            // retained (not deleted) so the feature can be re-enabled later.
            folderDropComboLabel_->setVisible(false);
            folderDropComboLabel_->setBounds({});
            folderDropModeCombo_->setVisible(false);
            folderDropModeCombo_->setBounds({});
            autoFadeOnSplitToggle_->setVisible(true);
            autoFadeOnSplitToggle_->setBounds(insetX, y, insetW, 30);
            y += 12;
            }
            y = endCard(y);
        }

        if ((searching || currentTab_ == Tab::Shortcuts)
            && matchesSearch({ "shortcuts", "profile", "key binding", "help" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(shortcutsHeaderLabel_.get(), shortcutsDescriptionLabel_.get(), shortcutsCollapseBtn_.get(), SectionId::ShortcutsProfile, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::ShortcutsProfile))
            {
            const bool compactActions = insetW < 250;
            profileCombo_->setVisible(true);
            applyProfileBtn_->setVisible(true);
            shortcutHelpBtn_->setVisible(true);
            if (compactActions)
            {
                profileCombo_->setBounds(insetX, y, insetW, 26);
                y += 32;
                applyProfileBtn_->setBounds(insetX, y, insetW - 34, 26);
                shortcutHelpBtn_->setBounds(insetX + insetW - 28, y, 28, 26);
                y += 36;
            }
            else
            {
                profileCombo_->setBounds(insetX, y, insetW - 104, 26);
                applyProfileBtn_->setBounds(insetX + insetW - 100, y, 66, 26);
                shortcutHelpBtn_->setBounds(insetX + insetW - 30, y, 28, 26);
                y += 36;
            }
            if (!profileAppliedName_.isEmpty() && profileAppliedTimer_ > 0)
            {
                profileAppliedLabel_->setVisible(true);
                profileAppliedLabel_->setBounds(insetX, y, insetW, 18);
                y += 24;
            }
            }
            y = endCard(y);

        }

        if ((searching || currentTab_ == Tab::Shortcuts)
            && matchesSearch({ "interface", "shortcut platform", "gui visual style", "windows", "apple" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(interfaceHeaderLabel_.get(), nullptr, interfaceCollapseBtn_.get(), SectionId::ShortcutsInterface, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::ShortcutsInterface))
            {
            shortcutStyleLabel_->setVisible(true);
            shortcutStyleLabel_->setBounds(insetX, y, insetW, 18);
            y += 20;
            shortcutStyleCombo_->setVisible(true);
            shortcutStyleCombo_->setBounds(insetX, y, insetW, 26);
            y += 32;
            guiStyleLabel_->setVisible(true);
            guiStyleLabel_->setBounds(insetX, y, insetW, 18);
            y += 20;
            guiStyleCombo_->setVisible(true);
            guiStyleCombo_->setBounds(insetX, y, insetW, 26);
            y += 12;
            }
            y = endCard(y);
        }

        if ((searching || currentTab_ == Tab::About)
            && matchesSearch({ "about", "daw core", "modular architecture" }))
        {
            anythingShown = true;
            beginCard(x, y, w, 40, searching);
            y = layoutSectionTitle(aboutHeaderLabel_.get(), nullptr, aboutCollapseBtn_.get(), SectionId::AboutInfo, searching, x, y, w);
            if (!isSectionCollapsed(SectionId::AboutInfo))
            {
            aboutTextLabel_->setVisible(true);
            aboutTextLabel_->setBounds(insetX, y, insetW, 40);
            y += 12;
            }
            y = endCard(y);
        }

        if (!anythingShown)
        {
            noResultsLabel_->setVisible(true);
            noResultsLabel_->setBounds(x, y + 8, w, 24);
            y += 40;
        }

        content_.setSize(width, juce::jmax(contentViewport_.getHeight(), y + 8));
        content_.setCards(cardBounds_);
        content_.repaint();
    }

    void updateProfileAppliedLabel()
    {
        if (profileAppliedLabel_)
            profileAppliedLabel_->setText(juce::String(juce::CharPointer_UTF8("\xe2\x9c\x93")) + " Applied: " + profileAppliedName_, juce::dontSendNotification);
        layoutCurrentTab();
    }

    // Interaction mode
    std::unique_ptr<ModeButton> touchBtn_, hybridBtn_, desktopBtn_;
    InteractionModeManager* interactionManager_ = nullptr;

    SettingsContentComponent content_;
    juce::Viewport contentViewport_;
    std::vector<juce::Component*> contentComponents_;
    std::vector<SettingsContentComponent::CardVisual> cardBounds_;
    std::unique_ptr<juce::TextEditor> searchEditor_;
    juce::String searchQuery_;
    Tab currentTab_ = Tab::General;
    std::unique_ptr<SettingsTabButton> generalTabBtn_, bubblegumTabBtn_, workflowTabBtn_, shortcutsTabBtn_, aboutTabBtn_;
    std::array<bool, (size_t) SectionId::Count> collapsedSections_ {};

    // Gamepad
    std::unique_ptr<ToggleButton> gamepadToggle_;
    std::unique_ptr<ToggleButton> waveformScrollToggle_;
    std::unique_ptr<ToggleButton> bubblegumAutoScrollToggle_;
    std::unique_ptr<ToggleButton> bubblegumAutoCloseToggle_;
    std::unique_ptr<ToggleButton> bubblegumKeepCablesVisibleToggle_;
    std::unique_ptr<ToggleButton> bubblegumKeepOffscreenVisibleToggle_;
    std::unique_ptr<BubblegumAppearancePanel> bubblegumAppearancePanel_;
    std::unique_ptr<ToggleButton> allTracksFullDepthToggle_;
    std::unique_ptr<juce::ComboBox> folderDropModeCombo_;
    std::unique_ptr<ToggleButton> autoFadeOnSplitToggle_;
    std::unique_ptr<juce::ComboBox> profileCombo_;
    std::unique_ptr<juce::TextButton> applyProfileBtn_;
    std::unique_ptr<juce::TextButton> audioDeviceBtn_;
    std::unique_ptr<juce::TextButton> controlRoomBtn_;
    std::unique_ptr<juce::TextButton> shortcutHelpBtn_;
    std::unique_ptr<juce::TextButton> generalCollapseBtn_, interactionCollapseBtn_, scrollingCollapseBtn_, mixerVisualCollapseBtn_;
    std::unique_ptr<juce::TextButton> mixerBubblesCollapseBtn_;
    std::unique_ptr<juce::Label> mixerBubblesHeaderLabel_, mixerBubblesDescLabel_;
    // Bubble colour preset buttons
    struct BubblePreset { juce::Colour deep; juce::Colour near; juce::String name; };
    static constexpr int kBubblePresetCount = 6;
    static const std::array<BubblePreset, kBubblePresetCount>& getBubblePresets()
    {
        static const std::array<BubblePreset, kBubblePresetCount> p {{
            { juce::Colour(0xFF3A4455), juce::Colour(0xFF6A7A90), "Ice Blue"    },
            { juce::Colour(0xFF1A2A1A), juce::Colour(0xFF3A6A3A), "Deep Green"  },
            { juce::Colour(0xFF2A1A2A), juce::Colour(0xFF6A3A6A), "Violet"      },
            { juce::Colour(0xFF2A1A10), juce::Colour(0xFF8A5A30), "Amber"       },
            { juce::Colour(0xFF0A0A0A), juce::Colour(0xFF303030), "Obsidian"    },
            { juce::Colour(0xFF1A2230), juce::Colour(0xFFB0C4DE), "Silver"      },
        }};
        return p;
    }
    int selectedBubblePreset_ = 0;
    std::array<std::unique_ptr<juce::TextButton>, kBubblePresetCount> bubblePresetBtns_;
    std::function<void(juce::Colour, juce::Colour)> onMixerBubbleColourChanged;
    std::unique_ptr<juce::TextButton> bubblegumRoutingCollapseBtn_, bubblegumAppearanceCollapseBtn_;
    std::unique_ptr<juce::TextButton> workflowCollapseBtn_, shortcutsCollapseBtn_, interfaceCollapseBtn_, aboutCollapseBtn_;
    juce::String profileAppliedName_;
    int profileAppliedTimer_ = 0;
    std::unique_ptr<juce::ComboBox> shortcutStyleCombo_;
    std::unique_ptr<juce::ComboBox> guiStyleCombo_;
    GamepadManager* gamepadManager_ = nullptr;
    bool suppressBubblegumRoutingCallback_ = false;
    bool suppressFolderDropModeCallback_ = false;
    bool suppressBubblegumAppearanceCallback_ = false;
    bool suppressTabChangeCallback_ = false;
    float contentAlpha_ = 1.0f;
    float contentTargetAlpha_ = 1.0f;
    BubblegumAppearanceSettings::PaletteFamily selectedTrackFamily_ = BubblegumAppearanceSettings::getDefaultSelectedTrackChoice().family;
    int selectedTrackPresetIndex_ = BubblegumAppearanceSettings::getDefaultSelectedTrackChoice().index;
    juce::Colour selectedTrackColour_ = BubblegumAppearanceSettings::getDefaultSelectedTrackAccent();
    BubblegumAppearanceSettings::PaletteFamily cableFamily_ = BubblegumAppearanceSettings::getDefaultCableChoice().family;
    int cablePresetIndex_ = BubblegumAppearanceSettings::getDefaultCableChoice().index;
    juce::Colour cableColour_ = BubblegumAppearanceSettings::getDefaultCableAccent();
    juce::Colour sidechainCableColour_ = juce::Colour(0xFF3A7BD5);
    juce::Colour routingTargetColour_ = BubblegumAppearanceSettings::getDefaultSelectedTrackAccent();
    juce::Colour masterTrackColour_ = juce::Colour(0xFFCC9900);
    float cableThickness_ = 5.0f;
    std::unique_ptr<juce::Label> generalHeaderLabel_, generalDescriptionLabel_;
    std::unique_ptr<juce::Label> scrollingHeaderLabel_, scrollingDescriptionLabel_;
    std::unique_ptr<juce::Label> mixerVisualHeaderLabel_, mixerVisualDescriptionLabel_;
    std::unique_ptr<juce::Label> bubblegumRoutingHeaderLabel_, bubblegumRoutingDescriptionLabel_;
    std::unique_ptr<juce::Label> bubblegumAppearanceHeaderLabel_, bubblegumAppearanceDescriptionLabel_;
    std::unique_ptr<juce::Label> folderHeaderLabel_, folderDescriptionLabel_, folderDropComboLabel_;
    std::unique_ptr<juce::Label> shortcutsHeaderLabel_, shortcutsDescriptionLabel_, profileAppliedLabel_;
    std::unique_ptr<juce::Label> interfaceHeaderLabel_, shortcutStyleLabel_, guiStyleLabel_;
    std::unique_ptr<juce::Label> aboutHeaderLabel_, aboutTextLabel_;
    std::unique_ptr<juce::Label> noResultsLabel_;

    void setMode(InteractionMode m)
    {
        if (interactionManager_)
            interactionManager_->setMode(m);
        updateModeButtons(m);
        repaint();
    }

    void updateModeButtons(InteractionMode m)
    {
        touchBtn_->setSelected  (m == InteractionMode::Touchscreen);
        hybridBtn_->setSelected (m == InteractionMode::Hybrid);
        desktopBtn_->setSelected(m == InteractionMode::Mouse);
    }

    void timerCallback() override
    {
        if (profileAppliedTimer_ > 0)
        {
            --profileAppliedTimer_;
            if (profileAppliedTimer_ == 0)
                updateProfileAppliedLabel();
        }

        if (std::abs(contentTargetAlpha_ - contentAlpha_) > 0.001f)
        {
            contentAlpha_ += (contentTargetAlpha_ - contentAlpha_) * 0.24f;
            if (std::abs(contentTargetAlpha_ - contentAlpha_) < 0.01f)
                contentAlpha_ = contentTargetAlpha_;
            contentViewport_.setAlpha(contentAlpha_);
        }
    }

    void applyBubblegumRoutingSettings()
    {
        if (suppressBubblegumRoutingCallback_)
            return;

        const bool autoScroll    = bubblegumAutoScrollToggle_  && bubblegumAutoScrollToggle_->getToggleState();
        const bool autoClose     = bubblegumAutoCloseToggle_   && bubblegumAutoCloseToggle_->getToggleState();
        const bool keepCables    = bubblegumKeepCablesVisibleToggle_ && bubblegumKeepCablesVisibleToggle_->getToggleState();
        const bool keepOffscreen = bubblegumKeepOffscreenVisibleToggle_ && bubblegumKeepOffscreenVisibleToggle_->getToggleState();
        if (onBubblegumRoutingSettingsChanged)
            onBubblegumRoutingSettingsChanged(autoScroll, autoClose, keepCables, keepOffscreen);
    }

    void rebuildSelectedTrackPresetItems() {}
    void rebuildCablePresetItems() {}
    void refreshColourButtons()
    {
        if (bubblegumAppearancePanel_)
        {
            bubblegumAppearancePanel_->setSelectedTrackColour(selectedTrackColour_);
            bubblegumAppearancePanel_->setRoutingTargetColour(routingTargetColour_);
            bubblegumAppearancePanel_->setCableAccentColour(cableColour_);
            bubblegumAppearancePanel_->setSidechainCableColour(sidechainCableColour_);
            bubblegumAppearancePanel_->setCableThickness(cableThickness_);
        }
    }

    void applySelectedTrackPaletteFamily() {}
    void applySelectedTrackPreset() {}
    void applyCablePaletteFamily() {}
    void applyCablePreset() {}

    void applyFolderDropModeSetting()
    {
        if (suppressFolderDropModeCallback_ || !folderDropModeCombo_)
            return;

        int mode = 0;
        switch (folderDropModeCombo_->getSelectedId())
        {
            case 1: mode = 2; break;
            case 2: mode = 1; break;
            case 3: mode = 0; break;
            default: mode = 1; break;
        }

        if (onFolderDropModeChanged)
            onFolderDropModeChanged(mode);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};

} // namespace DAW
