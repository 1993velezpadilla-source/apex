#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../Bubblegum/BubblegumAppearanceSettings.h"

namespace DAW {

class BubblegumAppearancePanel : public juce::Component
{
public:
    enum class SendPillMode { ShowSND = 0, ShowTrackName, Hide };

    std::function<void(juce::Colour)> onCableBodyTopChanged;
    std::function<void(juce::Colour)> onCableBodyBottomChanged;
    std::function<void(juce::Colour)> onCableCoreTopChanged;
    std::function<void(juce::Colour)> onCableCoreBottomChanged;
    std::function<void(juce::Colour)> onCableMistTopChanged;
    std::function<void(juce::Colour)> onCableMistBottomChanged;
    std::function<void(juce::Colour)> onCableDropletChanged;
    std::function<void(juce::Colour)> onCableShadowChanged;
    std::function<void(float)>        onCableThicknessChanged;
    std::function<void(juce::Colour)> onSidechainCableColourChanged;

    std::function<void(juce::Colour)> onSelectedGlassColourChanged;
    std::function<void(juce::Colour)> onSelectedSphereColourChanged;
    std::function<void(juce::Colour)> onSelectedParticleColourChanged;

    std::function<void(juce::Colour)> onBubblegumTargetColourChanged;
    std::function<void(SendPillMode)> onSendPillModeChanged;

    std::function<void(juce::Colour)> onMasterTrackColourChanged;
    std::function<void()> onApplyCableColourToRoutingTargets;
    std::function<void()> onApplyRoutingTargetColourToCables;

    std::function<void(bool)> onShowSendBadgesChanged;
    std::function<void(bool)> onShowSidechainBadgesChanged;
    std::function<void(float)> onSidechainCableThicknessChanged;

    BubblegumAppearancePanel()
    {
        auto& t = Theme::getInstance();

        auto styleActionButton = [&](juce::TextButton& button, bool accent = false)
        {
            button.setColour(juce::TextButton::buttonColourId,
                             accent ? t.colors.champagne.withAlpha(0.14f)
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

        auto styleSlider = [&](juce::Slider& slider)
        {
            slider.setColour(juce::Slider::backgroundColourId, t.colors.graphite.withAlpha(0.95f));
            slider.setColour(juce::Slider::trackColourId, t.colors.champagne.withAlpha(0.92f));
            slider.setColour(juce::Slider::thumbColourId, t.colors.champagne.brighter(0.10f));
            slider.setColour(juce::Slider::textBoxBackgroundColourId, t.colors.graphite.withAlpha(0.98f));
            slider.setColour(juce::Slider::textBoxOutlineColourId, t.colors.pewter.withAlpha(0.84f));
            slider.setColour(juce::Slider::textBoxTextColourId, t.colors.text);
        };

        auto styleToggle = [&](juce::ToggleButton& toggle)
        {
            toggle.setColour(juce::ToggleButton::textColourId, t.colors.text);
            toggle.setColour(juce::ToggleButton::tickColourId, t.colors.champagne.withAlpha(0.95f));
            toggle.setColour(juce::ToggleButton::tickDisabledColourId, t.colors.textDisabled);
        };

        setupSectionLabel(selectedLabel_, "Selected Track Effects");
        setupSectionLabel(routingLabel_, "Routing Target Effects");
        setupSectionLabel(cableLabel_, "Cable Layers");
        setupSectionLabel(masterLabel_, "Master Track");

        setupColourControl(selectedGlassTitle_, selectedGlassButton_, "Glass / Rim", BubblegumAppearanceSettings::getDefaultSelectedTrackAccent());
        setupColourControl(selectedSphereTitle_, selectedSphereButton_, "Floating Spheres", juce::Colour(0xFF1A1A1A));
        setupColourControl(selectedParticleTitle_, selectedParticleButton_, "Rising Particles", BubblegumAppearanceSettings::getDefaultSelectedTrackAccent().brighter(0.2f));
        selectedApplyAllButton_.setButtonText("Apply glass color to all selected-track effects");
        styleActionButton(selectedApplyAllButton_, true);
        selectedApplyAllButton_.onClick = [this]
        {
            auto colour = selectedGlassButton_.getColour();
            selectedSphereButton_.setColour(colour);
            selectedParticleButton_.setColour(colour);
            if (onSelectedSphereColourChanged) onSelectedSphereColourChanged(colour);
            if (onSelectedParticleColourChanged) onSelectedParticleColourChanged(colour);
        };
        addAndMakeVisible(selectedApplyAllButton_);
        selectedResetButton_.setButtonText("Reset selected-track effects");
        styleActionButton(selectedResetButton_);
        selectedResetButton_.onClick = [this]
        {
            setSelectedTrackColour(BubblegumAppearanceSettings::getDefaultSelectedTrackAccent());
            selectedSphereButton_.setColour(juce::Colour(0xFF1A1A1A));
            selectedParticleButton_.setColour(BubblegumAppearanceSettings::getDefaultSelectedTrackAccent().brighter(0.2f));
            if (onSelectedGlassColourChanged) onSelectedGlassColourChanged(selectedGlassButton_.getColour());
            if (onSelectedSphereColourChanged) onSelectedSphereColourChanged(selectedSphereButton_.getColour());
            if (onSelectedParticleColourChanged) onSelectedParticleColourChanged(selectedParticleButton_.getColour());
        };
        addAndMakeVisible(selectedResetButton_);

        setupColourControl(routingTargetTitle_, routingTargetButton_, "Outline / Send Tracks", BubblegumAppearanceSettings::getDefaultSelectedTrackAccent());

        sendPillTitle_.setText("Send pill", juce::dontSendNotification);
        sendPillTitle_.setColour(juce::Label::textColourId, Theme::getInstance().colors.text);
        sendPillTitle_.setFont(Theme::getInstance().fonts.small);
        addAndMakeVisible(sendPillTitle_);
        sendPillMode_.addItem("Show \"SND\" badge", 1);
        sendPillMode_.addItem("Show target track name", 2);
        sendPillMode_.addItem("Hide pill completely", 3);
        sendPillMode_.setSelectedId(1, juce::dontSendNotification);
        styleComboBox(sendPillMode_);
        sendPillMode_.onChange = [this]
        {
            if (!onSendPillModeChanged)
                return;

            switch (sendPillMode_.getSelectedId())
            {
                case 2: onSendPillModeChanged(SendPillMode::ShowTrackName); break;
                case 3: onSendPillModeChanged(SendPillMode::Hide); break;
                default: onSendPillModeChanged(SendPillMode::ShowSND); break;
            }
        };
        addAndMakeVisible(sendPillMode_);
        routingResetButton_.setButtonText("Reset routing target color");
        styleActionButton(routingResetButton_);
        routingResetButton_.onClick = [this]
        {
            setRoutingTargetColour(BubblegumAppearanceSettings::getDefaultSelectedTrackAccent());
            if (onBubblegumTargetColourChanged) onBubblegumTargetColourChanged(routingTargetButton_.getColour());
        };
        addAndMakeVisible(routingResetButton_);

        applyCableToTargetsButton_.setButtonText("Use cable color for routing targets");
        styleActionButton(applyCableToTargetsButton_, true);
        applyCableToTargetsButton_.onClick = [this]
        {
            routingTargetButton_.setColour(cableBodyBottomButton_.getColour());
            if (onBubblegumTargetColourChanged)
                onBubblegumTargetColourChanged(cableBodyBottomButton_.getColour());
            if (onApplyCableColourToRoutingTargets)
                onApplyCableColourToRoutingTargets();
        };
        addAndMakeVisible(applyCableToTargetsButton_);

        setupColourControl(cableBodyTopTitle_, cableBodyTopButton_, "Body gradient", juce::Colour::fromFloatRGBA(1.0f, 0.86f, 0.93f, 0.88f));
        setupColourControl(cableBodyBottomTitle_, cableBodyBottomButton_, "Body base", BubblegumAppearanceSettings::getDefaultCableAccent());
        setupColourControl(cableDropletTitle_, cableDropletButton_, "Droplets", juce::Colour::fromFloatRGBA(1.0f, 0.93f, 0.97f, 0.42f));
        setupColourControl(cableShadowTitle_, cableShadowButton_, "Shadow", juce::Colour::fromFloatRGBA(0.38f, 0.03f, 0.18f, 0.16f));
        setupColourControl(sidechainCableTitle_, sidechainCableButton_, "Sidechain chain colour", juce::Colour(0xFF3A7BD5));
        sidechainPresetTitle_.setText("Sidechain presets", juce::dontSendNotification);
        sidechainPresetTitle_.setColour(juce::Label::textColourId, Theme::getInstance().colors.textSecondary);
        sidechainPresetTitle_.setFont(Theme::getInstance().fonts.small);
        addAndMakeVisible(sidechainPresetTitle_);
        sidechainPresetMode_.addItem("Blue", 1);
        sidechainPresetMode_.addItem("Cyan", 2);
        sidechainPresetMode_.addItem("Purple", 3);
        sidechainPresetMode_.addItem("Green", 4);
        sidechainPresetMode_.addItem("Amber", 5);
        sidechainPresetMode_.addItem("Custom", 6);
        sidechainPresetMode_.setSelectedId(1, juce::dontSendNotification);
        styleComboBox(sidechainPresetMode_);
        sidechainPresetMode_.onChange = [this]
        {
            juce::Colour colour;
            switch (sidechainPresetMode_.getSelectedId())
            {
                case 2: colour = juce::Colour(0xFF22D3EE); break;
                case 3: colour = juce::Colour(0xFF8B5CF6); break;
                case 4: colour = juce::Colour(0xFF22C55E); break;
                case 5: colour = juce::Colour(0xFFF59E0B); break;
                case 6: return;
                default: colour = juce::Colour(0xFF3A7BD5); break;
            }
            sidechainCableButton_.setColour(colour);
            if (onSidechainCableColourChanged)
                onSidechainCableColourChanged(colour);
        };
        addAndMakeVisible(sidechainPresetMode_);

        // Badge visibility toggles
        setupSectionLabel(badgeLabel_, "Folder Badge Indicators");

        showSendBadgesToggle_.setButtonText("Show send badges on collapsed folders");
        showSendBadgesToggle_.setToggleState(true, juce::dontSendNotification);
        styleToggle(showSendBadgesToggle_);
        showSendBadgesToggle_.onClick = [this]
        {
            if (onShowSendBadgesChanged)
                onShowSendBadgesChanged(showSendBadgesToggle_.getToggleState());
        };
        addAndMakeVisible(showSendBadgesToggle_);

        showSidechainBadgesToggle_.setButtonText("Show sidechain badges on collapsed folders");
        showSidechainBadgesToggle_.setToggleState(true, juce::dontSendNotification);
        styleToggle(showSidechainBadgesToggle_);
        showSidechainBadgesToggle_.onClick = [this]
        {
            if (onShowSidechainBadgesChanged)
                onShowSidechainBadgesChanged(showSidechainBadgesToggle_.getToggleState());
        };
        addAndMakeVisible(showSidechainBadgesToggle_);

        // Sidechain cable thickness
        sidechainThicknessTitle_.setText("Sidechain cable thickness (chain)", juce::dontSendNotification);
        sidechainThicknessTitle_.setColour(juce::Label::textColourId, Theme::getInstance().colors.text);
        sidechainThicknessTitle_.setFont(Theme::getInstance().fonts.small);
        addAndMakeVisible(sidechainThicknessTitle_);
        sidechainThicknessSlider_.setRange(0.3, 4.0, 0.1);
        sidechainThicknessSlider_.setValue(1.5, juce::dontSendNotification);
        sidechainThicknessSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        sidechainThicknessSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        sidechainThicknessSlider_.setTextValueSuffix(" px");
        styleSlider(sidechainThicknessSlider_);
        sidechainThicknessSlider_.onValueChange = [this]
        {
            if (onSidechainCableThicknessChanged)
                onSidechainCableThicknessChanged((float) sidechainThicknessSlider_.getValue());
        };
        addAndMakeVisible(sidechainThicknessSlider_);

        cableApplyAllButton_.setButtonText("Apply body color to all cable layers");
        styleActionButton(cableApplyAllButton_, true);
        cableApplyAllButton_.onClick = [this]
        {
            setCableAccentColour(cableBodyBottomButton_.getColour());
            auto colour = cableBodyBottomButton_.getColour();
            if (onCableBodyTopChanged) onCableBodyTopChanged(cableBodyTopButton_.getColour());
            if (onCableBodyBottomChanged) onCableBodyBottomChanged(colour);
            if (onCableDropletChanged) onCableDropletChanged(cableDropletButton_.getColour());
            if (onCableShadowChanged) onCableShadowChanged(cableShadowButton_.getColour());
        };
        addAndMakeVisible(cableApplyAllButton_);
        cableResetButton_.setButtonText("Reset cable layers");
        styleActionButton(cableResetButton_);
        cableResetButton_.onClick = [this]
        {
            setCableAccentColour(BubblegumAppearanceSettings::getDefaultCableAccent());
            if (onCableBodyTopChanged) onCableBodyTopChanged(cableBodyTopButton_.getColour());
            if (onCableBodyBottomChanged) onCableBodyBottomChanged(cableBodyBottomButton_.getColour());
            if (onCableDropletChanged) onCableDropletChanged(cableDropletButton_.getColour());
            if (onCableShadowChanged) onCableShadowChanged(cableShadowButton_.getColour());
        };
        addAndMakeVisible(cableResetButton_);

        thicknessTitle_.setText("Send cable thickness (water)", juce::dontSendNotification);
        thicknessTitle_.setColour(juce::Label::textColourId, Theme::getInstance().colors.text);
        thicknessTitle_.setFont(Theme::getInstance().fonts.small);
        addAndMakeVisible(thicknessTitle_);
        thicknessSlider_.setRange(0.5, 15.0, 0.25);
        thicknessSlider_.setValue(5.0, juce::dontSendNotification);
        thicknessSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        thicknessSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        thicknessSlider_.setTextValueSuffix(" px");
        styleSlider(thicknessSlider_);
        thicknessSlider_.onValueChange = [this]
        {
            if (onCableThicknessChanged)
                onCableThicknessChanged((float) thicknessSlider_.getValue());
        };
        addAndMakeVisible(thicknessSlider_);

        applyRoutingTargetsToCableButton_.setButtonText("Use routing target color for cables");
        styleActionButton(applyRoutingTargetsToCableButton_, true);
        applyRoutingTargetsToCableButton_.onClick = [this]
        {
            setCableAccentColour(routingTargetButton_.getColour());
            if (onCableBodyBottomChanged)
                onCableBodyBottomChanged(routingTargetButton_.getColour());
            if (onApplyRoutingTargetColourToCables)
                onApplyRoutingTargetColourToCables();
        };
        addAndMakeVisible(applyRoutingTargetsToCableButton_);

        setupColourControl(masterAccentTitle_, masterAccentButton_, "Master accent", juce::Colour(0xFFCC9900));
        masterResetButton_.setButtonText("Reset master accent");
        styleActionButton(masterResetButton_);
        masterResetButton_.onClick = [this]
        {
            masterAccentButton_.setColour(juce::Colour(0xFFCC9900));
            if (onMasterTrackColourChanged) onMasterTrackColourChanged(masterAccentButton_.getColour());
        };
        addAndMakeVisible(masterResetButton_);

        wirePicker(selectedGlassButton_, onSelectedGlassColourChanged);
        wirePicker(selectedSphereButton_, onSelectedSphereColourChanged);
        wirePicker(selectedParticleButton_, onSelectedParticleColourChanged);
        wirePicker(routingTargetButton_, onBubblegumTargetColourChanged);
        wirePicker(cableBodyTopButton_, onCableBodyTopChanged);
        wirePicker(cableBodyBottomButton_, onCableBodyBottomChanged);
        wirePicker(cableDropletButton_, onCableDropletChanged);
        wirePicker(cableShadowButton_, onCableShadowChanged);
        wirePicker(sidechainCableButton_, onSidechainCableColourChanged);
        wirePicker(masterAccentButton_, onMasterTrackColourChanged);

        setSize(380, 1420);
    }

    void setSelectedTrackColour(juce::Colour colour)
    {
        selectedGlassButton_.setColour(colour);
    }

    void setRoutingTargetColour(juce::Colour colour)
    {
        routingTargetButton_.setColour(colour);
    }

    void setCableAccentColour(juce::Colour colour)
    {
        cableBodyBottomButton_.setColour(colour);
        cableBodyTopButton_.setColour(colour.interpolatedWith(juce::Colours::white, 0.70f));
        cableDropletButton_.setColour(colour.interpolatedWith(juce::Colours::white, 0.92f).withAlpha(0.42f));
        cableShadowButton_.setColour(colour.darker(1.8f).withAlpha(0.16f));
    }

    void setCableThickness(float thickness)
    {
        thicknessSlider_.setValue(thickness, juce::dontSendNotification);
    }

    void setSidechainCableColour(juce::Colour colour)
    {
        sidechainCableButton_.setColour(colour);
        const auto setPreset = [&](int id){ sidechainPresetMode_.setSelectedId(id, juce::dontSendNotification); };
        if      (colour == juce::Colour(0xFF3A7BD5)) setPreset(1);
        else if (colour == juce::Colour(0xFF22D3EE)) setPreset(2);
        else if (colour == juce::Colour(0xFF8B5CF6)) setPreset(3);
        else if (colour == juce::Colour(0xFF22C55E)) setPreset(4);
        else if (colour == juce::Colour(0xFFF59E0B)) setPreset(5);
        else                                          setPreset(6);
    }

    void setShowSendBadges(bool show)
    {
        showSendBadgesToggle_.setToggleState(show, juce::dontSendNotification);
    }

    void setShowSidechainBadges(bool show)
    {
        showSidechainBadgesToggle_.setToggleState(show, juce::dontSendNotification);
    }

    void setSidechainCableThickness(float thickness)
    {
        sidechainThicknessSlider_.setValue(thickness, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient bg(t.colors.obsidian.withAlpha(0.62f), bounds.getTopLeft(),
                                t.colors.graphite.withAlpha(0.22f), bounds.getBottomRight(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(bounds, 12.0f);

        auto drawSection = [&](juce::Rectangle<int> area, const juce::String& caption, juce::Colour accent, bool glow = false)
        {
            auto r = area.toFloat();
            g.setColour(t.colors.shadow.withAlpha(glow ? 0.20f : 0.12f));
            g.fillRoundedRectangle(r.translated(0.0f, 4.0f), 12.0f);

            juce::ColourGradient fill(t.colors.graphite.withAlpha(0.97f), r.getTopLeft(),
                                      t.colors.charcoal.withAlpha(0.93f), r.getBottomRight(), false);
            g.setGradientFill(fill);
            g.fillRoundedRectangle(r, 12.0f);

            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.fillRoundedRectangle(r.removeFromTop(14.0f), 12.0f);

            g.setColour(accent.withAlpha(glow ? 0.55f : 0.28f));
            g.drawRoundedRectangle(area.toFloat(), 12.0f, glow ? 1.2f : 1.0f);

            auto badge = area.removeFromTop(18).removeFromLeft(132).reduced(4, 0).toFloat();
            g.setColour(accent.withAlpha(0.14f));
            g.fillRoundedRectangle(badge, badge.getHeight() * 0.5f);
            g.setColour(accent.withAlpha(0.60f));
            g.drawRoundedRectangle(badge, badge.getHeight() * 0.5f, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.setFont(t.fonts.small);
            g.drawText(caption, badge.toNearestInt(), juce::Justification::centred, false);
        };

        auto header = headerBounds_.toFloat();
        juce::ColourGradient hero(t.colors.champagne.withAlpha(0.12f), header.getTopLeft(),
                                  t.colors.graphite.withAlpha(0.96f), header.getBottomRight(), false);
        g.setGradientFill(hero);
        g.fillRoundedRectangle(header, 14.0f);
        g.setColour(t.colors.pewter.withAlpha(0.72f));
        g.drawRoundedRectangle(header, 14.0f, 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.setFont(t.fonts.bold.withHeight(16.0f));
        g.drawText("Bubblegum Visual Design", headerBounds_.reduced(14, 10).removeFromTop(22), juce::Justification::centredLeft, false);
        g.setColour(t.colors.textSecondary.withAlpha(0.95f));
        g.setFont(t.fonts.small);
        g.drawText("Cleaner sections, stronger hierarchy, and better focus for cable styling.",
                   headerBounds_.reduced(14, 10).withTrimmedTop(22), juce::Justification::centredLeft, true);

        drawSection(selectedSectionBounds_, "Selected", t.colors.accent, true);
        drawSection(routingSectionBounds_, "Routing", t.colors.accent);
        drawSection(cableSectionBounds_, "Cables", t.colors.accent, true);
        drawSection(utilitySectionBounds_, "Thickness + Badges", t.colors.steel.brighter(0.2f));
        drawSection(masterSectionBounds_, "Master", juce::Colour(0xFFCC9900));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        const int rowH = 30;
        const int labelH = 18;
        const int rowGap = 6;
        const int sectionGap = 12;
        const int sectionInset = 14;

        headerBounds_ = area.removeFromTop(64);
        area.removeFromTop(sectionGap);

        auto layoutColour = [&](juce::Rectangle<int>& sectionArea, juce::Label& title, ColourButton& button)
        {
            title.setBounds(sectionArea.removeFromTop(labelH));
            button.setBounds(sectionArea.removeFromTop(rowH));
            sectionArea.removeFromTop(rowGap);
        };

        selectedSectionBounds_ = area.removeFromTop(250);
        auto selectedArea = selectedSectionBounds_.reduced(sectionInset, 14);
        selectedLabel_.setBounds(selectedArea.removeFromTop(22));
        selectedArea.removeFromTop(rowGap);
        layoutColour(selectedArea, selectedGlassTitle_, selectedGlassButton_);
        layoutColour(selectedArea, selectedSphereTitle_, selectedSphereButton_);
        layoutColour(selectedArea, selectedParticleTitle_, selectedParticleButton_);
        selectedApplyAllButton_.setBounds(selectedArea.removeFromTop(rowH));
        selectedArea.removeFromTop(rowGap);
        selectedResetButton_.setBounds(selectedArea.removeFromTop(rowH));
        area.removeFromTop(sectionGap);

        routingSectionBounds_ = area.removeFromTop(220);
        auto routingArea = routingSectionBounds_.reduced(sectionInset, 14);
        routingLabel_.setBounds(routingArea.removeFromTop(22));
        routingArea.removeFromTop(rowGap);
        layoutColour(routingArea, routingTargetTitle_, routingTargetButton_);
        sendPillTitle_.setBounds(routingArea.removeFromTop(labelH));
        sendPillMode_.setBounds(routingArea.removeFromTop(rowH));
        routingArea.removeFromTop(rowGap);
        routingResetButton_.setBounds(routingArea.removeFromTop(rowH));
        routingArea.removeFromTop(rowGap);
        applyCableToTargetsButton_.setBounds(routingArea.removeFromTop(rowH));
        area.removeFromTop(sectionGap);

        cableSectionBounds_ = area.removeFromTop(446);
        auto cableArea = cableSectionBounds_.reduced(sectionInset, 14);
        cableLabel_.setBounds(cableArea.removeFromTop(22));
        cableArea.removeFromTop(rowGap);
        layoutColour(cableArea, cableBodyTopTitle_, cableBodyTopButton_);
        layoutColour(cableArea, cableBodyBottomTitle_, cableBodyBottomButton_);
        layoutColour(cableArea, cableDropletTitle_, cableDropletButton_);
        layoutColour(cableArea, cableShadowTitle_, cableShadowButton_);
        layoutColour(cableArea, sidechainCableTitle_, sidechainCableButton_);
        sidechainPresetTitle_.setBounds(cableArea.removeFromTop(labelH));
        sidechainPresetMode_.setBounds(cableArea.removeFromTop(rowH));
        cableArea.removeFromTop(rowGap);
        cableApplyAllButton_.setBounds(cableArea.removeFromTop(rowH));
        cableArea.removeFromTop(rowGap);
        cableResetButton_.setBounds(cableArea.removeFromTop(rowH));
        cableArea.removeFromTop(rowGap);
        applyRoutingTargetsToCableButton_.setBounds(cableArea.removeFromTop(rowH));
        area.removeFromTop(sectionGap);

        utilitySectionBounds_ = area.removeFromTop(220);
        auto utilityArea = utilitySectionBounds_.reduced(sectionInset, 14);
        thicknessTitle_.setBounds(utilityArea.removeFromTop(labelH));
        thicknessSlider_.setBounds(utilityArea.removeFromTop(rowH));
        utilityArea.removeFromTop(rowGap + 4);
        sidechainThicknessTitle_.setBounds(utilityArea.removeFromTop(labelH));
        sidechainThicknessSlider_.setBounds(utilityArea.removeFromTop(rowH));
        utilityArea.removeFromTop(12);
        badgeLabel_.setBounds(utilityArea.removeFromTop(22));
        utilityArea.removeFromTop(rowGap);
        showSendBadgesToggle_.setBounds(utilityArea.removeFromTop(rowH));
        utilityArea.removeFromTop(rowGap);
        showSidechainBadgesToggle_.setBounds(utilityArea.removeFromTop(rowH));
        area.removeFromTop(sectionGap);

        masterSectionBounds_ = area.removeFromTop(140);
        auto masterArea = masterSectionBounds_.reduced(sectionInset, 14);
        masterLabel_.setBounds(masterArea.removeFromTop(22));
        masterArea.removeFromTop(rowGap);
        layoutColour(masterArea, masterAccentTitle_, masterAccentButton_);
        masterResetButton_.setBounds(masterArea.removeFromTop(rowH));
    }

private:
    class LiveColourSelector : public juce::ColourSelector,
                               private juce::ChangeListener
    {
    public:
        LiveColourSelector()
            : juce::ColourSelector(juce::ColourSelector::showAlphaChannel
                                 | juce::ColourSelector::showColourAtTop
                                 | juce::ColourSelector::showSliders
                                 | juce::ColourSelector::showColourspace)
        {
            addChangeListener(this);
        }

        ~LiveColourSelector() override
        {
            removeChangeListener(this);
        }

        std::function<void(juce::Colour)> onColourChanged;

    private:
        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            if (onColourChanged)
                onColourChanged(getCurrentColour());
        }
    };

    class ColourButton : public juce::Component
    {
    public:
        void setColour(juce::Colour c) { colour_ = c; repaint(); }
        juce::Colour getColour() const noexcept { return colour_; }
        std::function<void()> onClick;

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto r = getLocalBounds().toFloat();
            const bool hov = getLocalBounds().contains(getMouseXYRelative());
            g.setColour(hov ? t.colors.surfaceHover : t.colors.surface);
            g.fillRoundedRectangle(r, 5.f);
            g.setColour(t.colors.border.withAlpha(0.65f));
            g.drawRoundedRectangle(r, 5.f, 1.f);

            auto swatch = r.reduced(6.f).removeFromLeft(28.f);
            g.setColour(colour_);
            g.fillRoundedRectangle(swatch, 4.f);
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.drawRoundedRectangle(swatch, 4.f, 1.f);

            g.setColour(t.colors.text);
            g.setFont(t.fonts.small);
            g.drawText("Custom colour...", getLocalBounds().withTrimmedLeft(40), juce::Justification::centredLeft, true);
        }

        void mouseDown(const juce::MouseEvent&) override
        {
            if (onClick)
                onClick();
        }

    private:
        juce::Colour colour_ = juce::Colours::white;
    };

    void setupSectionLabel(juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, Theme::getInstance().colors.text);
        label.setFont(Theme::getInstance().fonts.bold);
        addAndMakeVisible(label);
    }

    void setupColourControl(juce::Label& title, ColourButton& button, const juce::String& text, juce::Colour colour)
    {
        title.setText(text, juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, Theme::getInstance().colors.textSecondary);
        title.setFont(Theme::getInstance().fonts.small);
        addAndMakeVisible(title);
        button.setColour(colour);
        addAndMakeVisible(button);
    }

    void wirePicker(ColourButton& button, std::function<void(juce::Colour)>& callback)
    {
        button.onClick = [this, &button, &callback]()
        {
            auto selector = std::make_unique<LiveColourSelector>();
            selector->setCurrentColour(button.getColour());
            selector->setSize(320, 380);
            selector->onColourChanged = [&button, &callback](juce::Colour colour)
            {
                button.setColour(colour);
                if (callback)
                    callback(colour);
            };

            juce::CallOutBox::launchAsynchronously(std::move(selector),
                                                   button.getScreenBounds(),
                                                   nullptr);
        };
    }

    juce::Label selectedLabel_, selectedGlassTitle_, selectedSphereTitle_, selectedParticleTitle_;
    juce::Label routingLabel_, routingTargetTitle_, sendPillTitle_;
    juce::Label cableLabel_, cableBodyTopTitle_, cableBodyBottomTitle_, cableDropletTitle_, cableShadowTitle_, sidechainCableTitle_, sidechainPresetTitle_, thicknessTitle_;
    juce::Label badgeLabel_, sidechainThicknessTitle_;
    juce::Label masterLabel_, masterAccentTitle_;

    ColourButton selectedGlassButton_, selectedSphereButton_, selectedParticleButton_;
    juce::TextButton selectedApplyAllButton_, selectedResetButton_;
    ColourButton routingTargetButton_;
    juce::TextButton routingResetButton_;
    ColourButton cableBodyTopButton_, cableBodyBottomButton_, cableDropletButton_, cableShadowButton_;
    ColourButton sidechainCableButton_;
    juce::TextButton cableApplyAllButton_, cableResetButton_;
    ColourButton masterAccentButton_;
    juce::TextButton masterResetButton_;

    juce::ComboBox sendPillMode_;
    juce::ComboBox sidechainPresetMode_;
    juce::Slider thicknessSlider_;
    juce::Slider sidechainThicknessSlider_;
    juce::ToggleButton showSendBadgesToggle_;
    juce::ToggleButton showSidechainBadgesToggle_;
    juce::TextButton applyCableToTargetsButton_;
    juce::TextButton applyRoutingTargetsToCableButton_;
    juce::Rectangle<int> headerBounds_;
    juce::Rectangle<int> selectedSectionBounds_;
    juce::Rectangle<int> routingSectionBounds_;
    juce::Rectangle<int> cableSectionBounds_;
    juce::Rectangle<int> utilitySectionBounds_;
    juce::Rectangle<int> masterSectionBounds_;
};

} // namespace DAW

