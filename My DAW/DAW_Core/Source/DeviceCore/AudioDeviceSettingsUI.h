#pragma once
#include <JuceHeader.h>
#include <thread>
#include "../ThemeCore/Theme.h"
#include "SafeAsioDeviceTypeCore.h"
#include "AudioDeviceSettingsModel.h"

namespace DAW {

/**
 * AudioDeviceSettingsUI — Settings panel for Audio Device configuration.
 *
 * Displays: driver type, device, sample rate, buffer size, latency.
 * Uses JUCE's AudioDeviceSelectorComponent internally for device selection.
 *
 * This panel is accessed from Settings → Audio Device.
 * It is separate from the Control Room / Monitor Section settings.
 */
class AudioDeviceSettingsUI : public juce::Component,
                              private juce::ChangeListener
{
public:
    AudioDeviceSettingsUI(juce::AudioDeviceManager& deviceManager,
                          std::function<void()> beforeDeviceMutation)
        : deviceManager_(deviceManager),
          beforeDeviceMutation_(std::move(beforeDeviceMutation))
    {
        deviceManager_.getAudioDeviceSetup(lastKnownGoodSetup_);
        if (auto* currentType = deviceManager_.getCurrentDeviceTypeObject())
            lastKnownGoodType_ = currentType->getTypeName();

        refreshSelectorComponent();

        deviceManager_.addChangeListener(this);
    }

    std::function<void()> onPanelClosed;
    std::function<void()> onSettingsConfirmed;
    std::function<void()> onOpenAdvancedPanel;

    ~AudioDeviceSettingsUI() override
    {
        deviceManager_.removeChangeListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        g.fillAll(t.colors.backgroundDark);

        // Header
        auto header = getLocalBounds().removeFromTop(32).toFloat();
        g.setColour(juce::Colour(0xFF4488CC).withAlpha(0.12f));
        g.fillRect(header);
        g.setColour(juce::Colour(0xFF4488CC).withAlpha(0.85f));
        g.fillRect(header.removeFromBottom(1.5f));

        g.setFont(juce::Font(10.f, juce::Font::bold));
        g.setColour(juce::Colour(0xFF4488CC));
        g.drawText("AUDIO DEVICE", header.withLeft(12.f), juce::Justification::centredLeft);

        closeBtnBounds_ = juce::Rectangle<float>((float)getWidth() - 24.f, 7.f, 16.f, 16.f);
        g.setColour(t.colors.surface.withAlpha(0.7f));
        g.fillRoundedRectangle(closeBtnBounds_, 3.f);
        g.setColour(t.colors.border.withAlpha(0.6f));
        g.drawRoundedRectangle(closeBtnBounds_, 3.f, 1.f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawLine(closeBtnBounds_.getX() + 4.f, closeBtnBounds_.getY() + 4.f,
                   closeBtnBounds_.getRight() - 4.f, closeBtnBounds_.getBottom() - 4.f, 1.4f);
        g.drawLine(closeBtnBounds_.getRight() - 4.f, closeBtnBounds_.getY() + 4.f,
                   closeBtnBounds_.getX() + 4.f, closeBtnBounds_.getBottom() - 4.f, 1.4f);

        g.setColour(t.colors.surface.withAlpha(0.75f));
        g.fillRoundedRectangle(advancedBtnBounds_, 4.f);
        g.setColour(t.colors.border.withAlpha(0.65f));
        g.drawRoundedRectangle(advancedBtnBounds_, 4.f, 1.1f);
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.setColour(t.colors.text);
        g.drawText("Advanced…", advancedBtnBounds_, juce::Justification::centred);

        g.setColour(t.colors.surface.withAlpha(0.75f));
        g.fillRoundedRectangle(confirmBtnBounds_, 4.f);
        g.setColour(juce::Colour(0xFF44CC88).withAlpha(0.85f));
        g.drawRoundedRectangle(confirmBtnBounds_, 4.f, 1.1f);
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.setColour(juce::Colours::white);
        g.drawText("Confirm", confirmBtnBounds_, juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop(34); // header
        auto footer = b.removeFromBottom(52);
        auto buttonRow = footer.removeFromBottom(30).translated(0, 6);
        auto confirmArea = buttonRow.removeFromRight(108);
        auto advancedArea = buttonRow.removeFromRight(108);
        confirmBtnBounds_ = confirmArea.toFloat().withSizeKeepingCentre(88.f, 24.f);
        advancedBtnBounds_ = advancedArea.toFloat().withSizeKeepingCentre(88.f, 24.f);

        if (selector_)
            selector_->setBounds(b.reduced(8));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (closeBtnBounds_.contains(e.position))
        {
            setVisible(false);
            if (onPanelClosed)
                onPanelClosed();
            return;
        }

        if (confirmBtnBounds_.contains(e.position))
        {
            ensureSafeConfirmedSelection();
            refreshModel();
            setVisible(false);
            if (onSettingsConfirmed)
                onSettingsConfirmed();
        }

        if (advancedBtnBounds_.contains(e.position))
        {
            if (onOpenAdvancedPanel)
                onOpenAdvancedPanel();
        }
    }

    /** Refreshes the displayed settings model from the live device. */
    void refreshModel()
    {
        model_.readFromDevice(deviceManager_);
    }

    const AudioDeviceSettingsModel& getModel() const { return model_; }

private:
    void refreshSelectorComponent()
    {
        selector_.reset();
        selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager_,
            0, 64,
            0, 64,
            true,
            false,
            true,
            true);
        addAndMakeVisible(selector_.get());
        selector_->toBack();
    }

    void ensureSafeConfirmedSelection()
    {
        auto* currentType = deviceManager_.getCurrentDeviceTypeObject();
        if (currentType == nullptr || !currentType->getTypeName().containsIgnoreCase("asio"))
            return;

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);

        const auto outputName = setup.outputDeviceName.trim();
        const auto outputs = currentType->getDeviceNames(false);
        const bool hasValidExplicitOutput = outputName.isNotEmpty()
            && !outputName.containsIgnoreCase("none")
            && outputs.contains(outputName);

        if (hasValidExplicitOutput)
            return;

        DBG("[AudioDeviceSettingsUI] Rejecting invalid ASIO confirmation without an explicit valid device; restoring last known good device.");
        restoreLastKnownGoodDevice();
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        if (auto* device = deviceManager_.getCurrentAudioDevice())
            if (device->getName().isNotEmpty())
            {
                deviceManager_.getAudioDeviceSetup(lastKnownGoodSetup_);
                if (auto* currentType = deviceManager_.getCurrentDeviceTypeObject())
                    lastKnownGoodType_ = currentType->getTypeName();
            }
    }

    void restoreLastKnownGoodDevice()
    {
        if (lastKnownGoodType_.isEmpty()
            || lastKnownGoodSetup_.outputDeviceName.isEmpty()
            || lastKnownGoodSetup_.outputDeviceName.containsIgnoreCase("none"))
        {
            return;
        }

        const juce::ScopedValueSetter<bool> guard(repairingDevice_, true);
        if (beforeDeviceMutation_)
            beforeDeviceMutation_();
        deviceManager_.setCurrentAudioDeviceType(lastKnownGoodType_, true);
        auto error = deviceManager_.setAudioDeviceSetup(lastKnownGoodSetup_, true);
        if (error.isNotEmpty())
            DBG("[AudioDeviceSettingsUI] restore last known device failed: " + error);
    }

    void logAvailableAudioDevices()
    {
        auto& types = deviceManager_.getAvailableDeviceTypes();

        DBG("[AudioDeviceSettingsUI] AudioDeviceManager device types: " + juce::String(types.size()));
        for (auto* type : types)
        {
            if (type == nullptr)
                continue;

            const auto inputs = type->getDeviceNames(true);
            const auto outputs = type->getDeviceNames(false);

            DBG("[AudioDeviceSettingsUI] Type=" + type->getTypeName()
                + " inputs=" + inputs.joinIntoString(", ")
                + " outputs=" + outputs.joinIntoString(", "));
        }
    }

    juce::AudioDeviceManager& deviceManager_;
    std::function<void()> beforeDeviceMutation_;
    AudioDeviceSettingsModel model_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector_;
    juce::AudioDeviceManager::AudioDeviceSetup lastKnownGoodSetup_;
    juce::String lastKnownGoodType_;
    juce::Rectangle<float> closeBtnBounds_;
    juce::Rectangle<float> confirmBtnBounds_;
    juce::Rectangle<float> advancedBtnBounds_;
    bool repairingDevice_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDeviceSettingsUI)
};

} // namespace DAW
