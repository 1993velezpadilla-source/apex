#pragma once

#include <JuceHeader.h>
#include <thread>
#include "../DeviceCore/DeviceSessionCore.h"
#include "../ThemeCore/Theme.h"
#include "../VocalTuneUI/ApexTuneColors.h"

namespace DAW {

class AudioDevicePanelUI : public juce::Component
{
public:
    AudioDevicePanelUI(DeviceSessionCore& session,
                       DevicePanelModelCore& model,
                       juce::AudioDeviceManager& deviceManager)
        : session_(session),
          model_(model),
          deviceManager_(deviceManager)
    {
        setOpaque(true);

        configureLabel(titleLabel_, juce::String::fromUTF8 (u8"AUDIO DEVICE 😈"));
        titleLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
        addAndMakeVisible(titleLabel_);

        configureLabel(statusLabel_, {});
        statusLabel_.setJustificationType(juce::Justification::centredLeft);
        statusLabel_.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(statusLabel_);

        configureLabel(backendLabel_, "Backend");
        configureLabel(outputLabel_, "Output");
        configureLabel(outputChannelsLabel_, "Output Pair");
        configureLabel(inputLabel_, "Input");
        configureLabel(sampleRateLabel_, "Sample Rate");
        configureLabel(bufferLabel_, "Buffer");

        addAndMakeVisible(backendLabel_);
        addAndMakeVisible(outputLabel_);
        addAndMakeVisible(outputChannelsLabel_);
        addAndMakeVisible(inputLabel_);
        addAndMakeVisible(sampleRateLabel_);
        addAndMakeVisible(bufferLabel_);

        configureCombo(backendBox_);
        configureCombo(outputBox_);
        configureCombo(outputChannelsBox_);
        configureCombo(inputBox_);
        configureCombo(sampleRateBox_);
        configureCombo(bufferBox_);

        addAndMakeVisible(backendBox_);
        addAndMakeVisible(outputBox_);
        addAndMakeVisible(outputChannelsBox_);
        addAndMakeVisible(inputBox_);
        addAndMakeVisible(sampleRateBox_);
        addAndMakeVisible(bufferBox_);

        configureButton(applyButton_, "Apply");
        configureButton(advancedButton_, juce::String::fromUTF8 (u8"Advanced…"));
        configureButton(configureAsioButton_, juce::String::fromUTF8 (u8"Configure…"));
        configureButton(closeButton_, juce::String::fromUTF8 (u8"×"));

        applyButton_.onClick = [this] { applyPendingRequest(); };
        advancedButton_.onClick = [this]
        {
            if (onOpenAdvancedPanel)
                onOpenAdvancedPanel();
        };
        configureAsioButton_.onClick = [this] { showAsioControlPanel(); };
        closeButton_.onClick = [this] { closePanel(); };

        addAndMakeVisible(applyButton_);
        addAndMakeVisible(advancedButton_);
        addAndMakeVisible(configureAsioButton_);
        addAndMakeVisible(closeButton_);

        backendBox_.onChange = [this] { backendChanged(); };
        outputBox_.onChange = [this] { controlsChanged(); };
        outputChannelsBox_.onChange = [this] { outputChannelsChanged(); };
        inputBox_.onChange = [this] { controlsChanged(); };
        sampleRateBox_.onChange = [this] { controlsChanged(); };
        bufferBox_.onChange = [this] { controlsChanged(); };

        // No refreshFromSnapshot() here: visibilityChanged() performs the
        // one-scan-per-open refresh, avoiding a double device scan on first show.
    }

    void visibilityChanged() override
    {
        if (isVisible())
        {
            // One fresh scan per panel-open: picks up hot-plugged/removed
            // devices without rescanning on every control interaction.
            model_.invalidateDeviceCache();
            session_.invalidateScanCache();

            // PAINT-FIRST OPEN: the device scan (ASIO4ALL especially) can block
            // for hundreds of ms. Show the panel frame immediately with a
            // loading status, then populate one tick later so the user sees
            // the panel open instantly instead of a frozen click.
            statusLabel_.setText("Loading devices...", juce::dontSendNotification);
            repaint();
            if (auto* peer = getPeer())
                peer->performAnyPendingRepaintsNow();

            juce::Component::SafePointer<AudioDevicePanelUI> safe(this);
            juce::MessageManager::callAsync([safe]
            {
                if (auto* self = safe.getComponent())
                {
                    if (!self->isVisible())
                        return;
                    self->refreshFromSnapshot();
                }
            });

            // Ensure panel-level key handling works even when a child has focus.
            setWantsKeyboardFocus(true);
            grabKeyboardFocus();
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        // Ensure common dialog keys work even when a child control has focus.
        if (key == juce::KeyPress::escapeKey)
        {
            // Esc always closes immediately, discarding un-applied edits.
            if (!applyInFlight_)
            {
                pendingRequest_ = session_.snapshotCurrent();
                setVisible(false);
                if (onPanelClosed)
                    onPanelClosed();
            }
            return true;
        }

        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::F10Key)
        {
            if (!applyInFlight_)
                applyPendingRequest();
            return true;
        }

        return false;
    }

    std::function<void()> onSettingsConfirmed;
    std::function<void()> onPanelClosed;
    std::function<void()> onOpenAdvancedPanel;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Col::VocalTune::background());

        auto bounds = getLocalBounds().toFloat();
        g.setColour(Col::VocalTune::panelDivider().withAlpha(0.9f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

        auto header = bounds.removeFromTop(38.0f);
        g.setColour(Col::VocalTune::toolbarBg().withAlpha(0.95f));
        g.fillRoundedRectangle(header, 8.0f);
        g.setColour(Col::VocalTune::panelDivider().withAlpha(0.85f));
        g.drawLine(header.getX(), header.getBottom(), header.getRight(), header.getBottom(), 1.0f);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (!applyInFlight_ || applyProgress_ <= 0.0f)
            return;

        auto area = applyButton_.getBounds().toFloat();
        if (area.isEmpty())
            return;

        const float pad = 2.0f;
        auto bar = area.reduced(pad);
        const float r = 6.0f;

        g.setColour(Col::VocalTune::toolbarBg().withAlpha(0.65f));
        g.fillRoundedRectangle(bar, r);

        auto fill = bar;
        fill.setWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, applyProgress_));
        g.setColour(Col::VocalTune::panelDivider().withAlpha(0.9f));
        g.fillRoundedRectangle(fill, r);

        g.setColour(Col::VocalTune::panelDivider().withAlpha(0.9f));
        g.drawRoundedRectangle(bar, r, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        auto header = area.removeFromTop(28);
        closeButton_.setBounds(getWidth() - 38, 8, 26, 26);
        titleLabel_.setBounds(header.withRight(closeButton_.getX() - 8));

        area.removeFromTop(8);

        layoutRow(area, backendLabel_, backendBox_);
        layoutRow(area, outputLabel_, outputBox_);
        layoutRow(area, outputChannelsLabel_, outputChannelsBox_);
        layoutRow(area, inputLabel_, inputBox_);
        layoutRow(area, sampleRateLabel_, sampleRateBox_);
        layoutRow(area, bufferLabel_, bufferBox_);

        area.removeFromTop(8);
        // Full-width status row — the old 88px sliver truncated "Applying…"
        // and every error message, making Apply feedback look broken.
        statusLabel_.setBounds(16, getHeight() - 66, getWidth() - 32, 22);
        const int y = getHeight() - 34;
        configureAsioButton_.setBounds(getWidth() - 132, y, 120, 26);
        applyButton_.setBounds(getWidth() - 228, y, 92, 26);
        advancedButton_.setBounds(getWidth() - 328, y, 96, 26);
    }

private:
    void configureLabel(juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, Col::VocalTune::text());
        label.setFont(juce::Font(11.0f));
    }

    void configureCombo(juce::ComboBox& box)
    {
        box.setColour(juce::ComboBox::backgroundColourId, Col::VocalTune::toolbarBg());
        box.setColour(juce::ComboBox::textColourId, Col::VocalTune::text());
        box.setColour(juce::ComboBox::outlineColourId, Col::VocalTune::panelDivider());
        box.setColour(juce::ComboBox::arrowColourId, Col::VocalTune::text());
        box.setTextWhenNothingSelected({});
    }

    void configureButton(juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText(text);
        button.setColour(juce::TextButton::buttonColourId, Col::VocalTune::panelDivider());
        button.setColour(juce::TextButton::textColourOffId, Col::VocalTune::text());
        button.setColour(juce::TextButton::textColourOnId, Col::VocalTune::text());
    }

    void layoutRow(juce::Rectangle<int>& area, juce::Label& label, juce::ComboBox& combo)
    {
        auto row = area.removeFromTop(36);
        label.setBounds(row.removeFromLeft(96));
        combo.setBounds(row.reduced(0, 4));
    }

    void refreshFromSnapshot()
    {
        pendingRequest_ = session_.snapshotCurrent();

        setComboItems(backendBox_, model_.getTypeNames());
        selectComboText(backendBox_, pendingRequest_.typeName, false);

        repopulateDeviceLists();
        setComboItems(sampleRateBox_, model_.getCommonSampleRates());
        setComboItems(bufferBox_, model_.getCommonBufferSizes());
        selectComboText(sampleRateBox_, juce::String(juce::roundToInt(pendingRequest_.sampleRate)), true);
        selectComboText(bufferBox_, juce::String(pendingRequest_.bufferSize), true);

        updateStatusFromValidation();
    }

    void setComboItems(juce::ComboBox& box, const juce::StringArray& items)
    {
        const juce::ScopedValueSetter<bool> guard(ignoreControlChanges_, true);
        box.clear(juce::dontSendNotification);
        int id = 1;
        for (const auto& item : items)
            box.addItem(item, id++);
    }

    void selectComboText(juce::ComboBox& box, const juce::String& text, bool allowCustomText)
    {
        const juce::ScopedValueSetter<bool> guard(ignoreControlChanges_, true);
        if (text.isEmpty())
        {
            box.setSelectedId(0, juce::dontSendNotification);
            if (allowCustomText)
                box.setText({}, juce::dontSendNotification);
            return;
        }

        const auto index = findComboItemIndex(box, text);
        if (index >= 0)
            box.setSelectedItemIndex(index, juce::dontSendNotification);
        else if (allowCustomText)
            box.setText(text, juce::dontSendNotification);
        else
            box.setSelectedId(0, juce::dontSendNotification);
    }

    int findComboItemIndex(const juce::ComboBox& box, const juce::String& text) const
    {
        for (int i = 0; i < box.getNumItems(); ++i)
            if (box.getItemText(i) == text)
                return i;

        return -1;
    }

    juce::AudioIODeviceType* findDeviceType(const juce::String& typeName) const
    {
        auto& types = deviceManager_.getAvailableDeviceTypes();

        for (auto* type : types)
            if (type != nullptr && type->getTypeName() == typeName)
                return type;

        return nullptr;
    }

    bool hasPendingExplicitDeviceSelection() const
    {
        const auto outputName = outputBox_.getText().trim();

        return outputName.isNotEmpty()
            && ! outputName.containsIgnoreCase("none")
            && outputName != DevicePanelModelCore::noDeviceSentinel();
    }

    bool hasPendingChanges() const
    {
        auto current = session_.snapshotCurrent();

        return pendingRequest_.typeName.trim() != current.typeName.trim()
            || pendingRequest_.outputDeviceName.trim() != current.outputDeviceName.trim()
            || pendingRequest_.inputDeviceName.trim() != current.inputDeviceName.trim()
            || pendingRequest_.useDefaultOutputChannels != current.useDefaultOutputChannels
            || pendingRequest_.outputChannels != current.outputChannels
            || juce::roundToInt(pendingRequest_.sampleRate) != juce::roundToInt(current.sampleRate)
            || pendingRequest_.bufferSize != current.bufferSize;
    }

    void repopulateDeviceLists()
    {
        const auto typeName = backendBox_.getText();
        setComboItems(outputBox_, model_.getOutputDevices(typeName));
        setComboItems(inputBox_, model_.getInputDevices(typeName));

        if (pendingRequest_.typeName != typeName)
            pendingRequest_.typeName = typeName;

        if (typeName.containsIgnoreCase("asio")
            && (pendingRequest_.outputDeviceName.trim().isEmpty()
                || pendingRequest_.outputDeviceName.containsIgnoreCase("none")))
        {
            pendingRequest_.outputDeviceName = DevicePanelModelCore::noDeviceSentinel();
        }

        selectComboText(outputBox_, pendingRequest_.outputDeviceName, false);
        repopulateOutputChannelChoices();
        selectComboText(inputBox_, pendingRequest_.inputDeviceName, false);
    }

    void repopulateOutputChannelChoices()
    {
        outputChannelChoices_ = model_.getOutputChannelChoices(pendingRequest_.typeName,
                                                               pendingRequest_.outputDeviceName);

        juce::StringArray labels;
        for (const auto& choice : outputChannelChoices_)
            labels.add(choice.label);

        setComboItems(outputChannelsBox_, labels);

        if (!pendingRequest_.typeName.containsIgnoreCase("asio"))
        {
            pendingRequest_.useDefaultOutputChannels = true;
            pendingRequest_.outputChannels.clear();
            outputChannelsLabel_.setVisible(false);
            outputChannelsBox_.setVisible(false);
            return;
        }

        outputChannelsLabel_.setVisible(true);
        outputChannelsBox_.setVisible(true);

        const auto selectedText = getSelectedOutputChannelLabel();
        selectComboText(outputChannelsBox_, selectedText, false);
    }

    juce::String getSelectedOutputChannelLabel() const
    {
        if (pendingRequest_.useDefaultOutputChannels)
            return DevicePanelModelCore::defaultOutputChannelsSentinel();

        for (const auto& choice : outputChannelChoices_)
            if (!choice.useDefault && choice.channels == pendingRequest_.outputChannels)
                return choice.label;

        return {};
    }

    void backendChanged()
    {
        if (ignoreControlChanges_)
            return;

        pendingRequest_.typeName = backendBox_.getText();
        pendingRequest_.outputDeviceName = {};
        pendingRequest_.inputDeviceName = {};
        pendingRequest_.outputChannels.clear();
        pendingRequest_.useDefaultOutputChannels = true;

        if (pendingRequest_.typeName.containsIgnoreCase("asio"))
            pendingRequest_.outputDeviceName = DevicePanelModelCore::noDeviceSentinel();

        repopulateDeviceLists();
        updateStatusFromValidation();
    }

    void controlsChanged()
    {
        if (ignoreControlChanges_)
            return;

        pendingRequest_.typeName = backendBox_.getText();
        pendingRequest_.outputDeviceName = outputBox_.getText();
        pendingRequest_.inputDeviceName = inputBox_.getText();
        pendingRequest_.sampleRate = sampleRateBox_.getText().getDoubleValue();
        pendingRequest_.bufferSize = bufferBox_.getText().isNotEmpty() ? bufferBox_.getText().getIntValue() : 0;
        repopulateOutputChannelChoices();
        syncOutputChannelSelectionFromUI();
        updateStatusFromValidation();
    }

    void outputChannelsChanged()
    {
        if (ignoreControlChanges_)
            return;

        syncOutputChannelSelectionFromUI();
        updateStatusFromValidation();
    }

    void syncOutputChannelSelectionFromUI()
    {
        if (!pendingRequest_.typeName.containsIgnoreCase("asio"))
        {
            pendingRequest_.useDefaultOutputChannels = true;
            pendingRequest_.outputChannels.clear();
            return;
        }

        const auto selected = outputChannelsBox_.getText().trim();
        for (const auto& choice : outputChannelChoices_)
        {
            if (choice.label == selected)
            {
                pendingRequest_.useDefaultOutputChannels = choice.useDefault;
                pendingRequest_.outputChannels = choice.channels;
                return;
            }
        }

        pendingRequest_.useDefaultOutputChannels = true;
        pendingRequest_.outputChannels.clear();
    }

    void updateStatusFromValidation()
    {
        const auto validation = session_.validate(pendingRequest_);
        if (validation.ok)
            statusLabel_.setText("Ready to apply.", juce::dontSendNotification);
        else
            statusLabel_.setText(validation.reason, juce::dontSendNotification);

        // Apply stays clickable — clicking with an invalid config surfaces the
        // reason loudly instead of a mysteriously dead button.
        applyButton_.setEnabled(!applyInFlight_);
        updateConfigureButtonVisibility();
    }

    void updateConfigureButtonVisibility()
    {
        const bool isAsio = backendBox_.getText().containsIgnoreCase("asio");
        configureAsioButton_.setVisible(true);
        configureAsioButton_.setEnabled(isAsio && hasPendingExplicitDeviceSelection());
        if (isAsio)
        {
            outputChannelsLabel_.setVisible(true);
            outputChannelsBox_.setVisible(true);
            outputChannelsBox_.setEnabled(!outputChannelChoices_.isEmpty());
        }
        else
        {
            outputChannelsLabel_.setVisible(false);
            outputChannelsBox_.setVisible(false);
        }
    }

    void showAsioControlPanel()
    {
        const auto typeName = backendBox_.getText().trim();
        const auto outputName = outputBox_.getText().trim();
        auto inputName = inputBox_.getText().trim();

        if (! typeName.containsIgnoreCase("asio"))
        {
            statusLabel_.setText("Driver control panel is available only for ASIO devices.",
                                 juce::dontSendNotification);
            return;
        }

        if (! hasPendingExplicitDeviceSelection())
        {
            statusLabel_.setText("Select an ASIO device first.",
                                 juce::dontSendNotification);
            return;
        }

        if (inputName == DevicePanelModelCore::noDeviceSentinel())
            inputName.clear();

        DBG("[AudioDevicePanelUI] Configure requested. type=" + typeName
            + " out=" + outputName
            + " in=" + inputName);

        // Opening a driver control panel (ASIO4ALL especially) can block the
        // message thread for a while — show feedback NOW so the click never
        // feels dead.
        statusLabel_.setText("Opening driver control panel...", juce::dontSendNotification);
        statusLabel_.repaint();
        if (auto* peer = getPeer())
            peer->performAnyPendingRepaintsNow();

        if (hasPendingChanges())
        {
            DBG("[AudioDevicePanelUI] Configure committing pending ASIO selection before opening control panel.");

            const auto validation = session_.validate(pendingRequest_);
            if (!validation.ok)
            {
                DBG("[AudioDevicePanelUI] Configure blocked by validation error: " + validation.reason);
                statusLabel_.setText(validation.reason, juce::dontSendNotification);
                return;
            }

            const auto error = session_.commit(pendingRequest_);
            if (error.isNotEmpty())
            {
                DBG("[AudioDevicePanelUI] Configure commit failed: " + error);
                statusLabel_.setText(error, juce::dontSendNotification);
                return;
            }

            DBG("[AudioDevicePanelUI] Configure commit succeeded.");
            refreshFromSnapshot();

            if (onSettingsConfirmed)
                onSettingsConfirmed();
        }

        if (auto* currentDevice = deviceManager_.getCurrentAudioDevice())
        {
            const auto currentType = deviceManager_.getCurrentAudioDeviceType();

            if (currentType == typeName
                && currentDevice->getName() == outputName
                && currentDevice->hasControlPanel())
            {
                currentDevice->showControlPanel();
                refreshFromSnapshot();
                statusLabel_.setText("Driver control panel opened.", juce::dontSendNotification);
                return;
            }
        }

        auto* type = findDeviceType(typeName);
        if (type == nullptr)
        {
            statusLabel_.setText("ASIO device type not available.", juce::dontSendNotification);
            return;
        }

        // Reuse the session scan cache — a redundant scanForDevices() here cost
        // seconds with ASIO4ALL every time Configure was clicked.
        model_.ensureTypeScanned(typeName);
        std::unique_ptr<juce::AudioIODevice> tempDevice (type->createDevice(outputName, inputName));

        if (tempDevice == nullptr)
        {
            statusLabel_.setText("Could not open the selected ASIO driver control panel.",
                                 juce::dontSendNotification);
            return;
        }

        if (! tempDevice->hasControlPanel())
        {
            statusLabel_.setText("No ASIO control panel available for this device.",
                                 juce::dontSendNotification);
            return;
        }

        tempDevice->showControlPanel();

        // After the driver panel closes, rescan in case buffer size or
        // sample rate changed inside the driver's own settings.
        juce::Timer::callAfterDelay(400, [this]
        {
            if (auto* d = deviceManager_.getCurrentAudioDevice())
            {
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                deviceManager_.getAudioDeviceSetup(setup);
                deviceManager_.setAudioDeviceSetup(setup, true);
            }
            refreshFromSnapshot();
        });
    }

    void applyPendingRequest()
    {
        DBG("[AudioDevicePanelUI] applyPendingRequest() ENTER");
        DBG("[AudioDevicePanelUI] Apply clicked. pending type=" + pendingRequest_.typeName
            + " out=" + pendingRequest_.outputDeviceName
            + " in=" + pendingRequest_.inputDeviceName
            + " sr=" + juce::String(juce::roundToInt(pendingRequest_.sampleRate))
            + " buf=" + juce::String(pendingRequest_.bufferSize));

        const auto validation = session_.validate(pendingRequest_);
        if (!validation.ok)
        {
            statusLabel_.setText(juce::String::fromUTF8 (u8"⚠ ") + validation.reason,
                                 juce::dontSendNotification);
            return;
        }

        beginApplyAsync();
    }

    void beginApplyAsync()
    {
        DBG("[AudioDevicePanelUI] beginApplyAsync() ENTER");
        if (applyInFlight_)
            return;

        applyInFlight_ = true;
        applyProgress_ = 0.0f;

        const auto request = pendingRequest_;
        setControlsEnabled(false);
        statusLabel_.setText(juce::String::fromUTF8 (u8"Applying…"), juce::dontSendNotification);
        applyButton_.setButtonText(juce::String::fromUTF8 (u8"Applying…"));
        setMouseCursor(juce::MouseCursor::WaitCursor);

        // Indeterminate progress animation: keep it cheap and deterministic.
        applyProgressTimer_.startTimerHz(60);

        // CRITICAL: force the "Applying…" frame onto the screen NOW.
        // repaint() alone only queues an invalidation — if the commit ran first
        // and blocked the message thread (ASIO4ALL init can take seconds), the
        // user never saw any feedback. This flush makes feedback deterministic.
        repaint();
        if (auto* peer = getPeer())
            peer->performAnyPendingRepaintsNow();

        const auto startedMs = juce::Time::getMillisecondCounterHiRes();

        // Commit must run on the JUCE message thread (AudioDeviceManager is not
        // thread-safe). Defer one tick so button-up/paint events fully settle.
        juce::Component::SafePointer<AudioDevicePanelUI> safe(this);
        juce::Timer::callAfterDelay(30, [safe, request, startedMs]()
        {
            if (auto* self = safe.getComponent())
                self->finishApply(request, startedMs);
        });
    }

    void finishApply(const DeviceRequest& request, double startedMs)
    {
        const auto error = session_.commit(request);
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedMs;

        applyInFlight_ = false;
        applyProgressTimer_.stopTimer();
        applyProgress_ = 0.0f;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint(applyButton_.getBounds());

        if (error.isNotEmpty())
        {
            DBG("[AudioDevicePanelUI] Apply failed: " + error);
            closeAfterApply_ = false;
            statusLabel_.setText(juce::String::fromUTF8 (u8"⚠ ") + error, juce::dontSendNotification);
            applyButton_.setButtonText("Apply");
            setControlsEnabled(true);
            return;
        }

        auto* device = deviceManager_.getCurrentAudioDevice();
        const auto activeType = deviceManager_.getCurrentAudioDeviceType();
        const auto activeDevice = device != nullptr ? device->getName() : request.outputDeviceName;
        const auto activeRate = device != nullptr ? device->getCurrentSampleRate() : request.sampleRate;
        const auto activeBuffer = device != nullptr ? device->getCurrentBufferSizeSamples() : request.bufferSize;

        const auto msText = elapsedMs >= 1.0 ? (" (" + juce::String((int) juce::roundToInt(elapsedMs)) + " ms)") : juce::String();
        statusLabel_.setText(juce::String::fromUTF8 (u8"✓ Applied: ") + activeType + " / " + activeDevice + " @ "
                                + juce::String(juce::roundToInt(activeRate)) + " Hz / "
                                + juce::String(activeBuffer) + " smp" + msText,
                             juce::dontSendNotification);

        DBG("[AudioDevicePanelUI] Apply succeeded. active type=" + activeType
            + " device=" + activeDevice
            + " sr=" + juce::String(juce::roundToInt(activeRate))
            + " buf=" + juce::String(activeBuffer)
            + " elapsedMs=" + juce::String(elapsedMs));

        refreshFromSnapshot();
        applyButton_.setButtonText("Apply");
        setControlsEnabled(true);

        if (onSettingsConfirmed)
            onSettingsConfirmed();

        if (closeAfterApply_)
        {
            closeAfterApply_ = false;
            setVisible(false);
            if (onPanelClosed)
                onPanelClosed();
        }
    }

    void closePanel()
    {
        if (applyInFlight_)
        {
            statusLabel_.setText(juce::String::fromUTF8 (u8"Applying…"), juce::dontSendNotification);
            return;
        }

        if (hasPendingChanges())
        {
            DBG("[AudioDevicePanelUI] Close requested with pending changes.");

            const auto validation = session_.validate(pendingRequest_);
            if (!validation.ok)
            {
                DBG("[AudioDevicePanelUI] Close blocked by validation error: " + validation.reason);
                statusLabel_.setText(validation.reason + "  (fix or press Esc to discard)",
                                     juce::dontSendNotification);
                return;
            }

            closeAfterApply_ = true;
            applyPendingRequest();
            return;
        }

        setVisible(false);
        if (onPanelClosed)
            onPanelClosed();
    }

    void setControlsEnabled(bool enabled)
    {
        backendBox_.setEnabled(enabled);
        outputBox_.setEnabled(enabled);
        outputChannelsBox_.setEnabled(enabled && outputChannelsBox_.isVisible() && !outputChannelChoices_.isEmpty());
        inputBox_.setEnabled(enabled);
        sampleRateBox_.setEnabled(enabled);
        bufferBox_.setEnabled(enabled);
        applyButton_.setEnabled(enabled);
        if (enabled) updateConfigureButtonVisibility();
        closeButton_.setEnabled(enabled);
    }

    DeviceSessionCore& session_;
    DevicePanelModelCore& model_;
    juce::AudioDeviceManager& deviceManager_;
    DeviceRequest pendingRequest_;
    juce::Array<DevicePanelModelCore::OutputChannelChoice> outputChannelChoices_;
    bool ignoreControlChanges_ = false;

    juce::Label titleLabel_;
    juce::Label statusLabel_;
    juce::Label backendLabel_;
    juce::Label outputLabel_;
    juce::Label outputChannelsLabel_;
    juce::Label inputLabel_;
    juce::Label sampleRateLabel_;
    juce::Label bufferLabel_;

    juce::ComboBox backendBox_;
    juce::ComboBox outputBox_;
    juce::ComboBox outputChannelsBox_;
    juce::ComboBox inputBox_;
    juce::ComboBox sampleRateBox_;
    juce::ComboBox bufferBox_;

    juce::TextButton applyButton_;
    juce::TextButton advancedButton_;
    juce::TextButton configureAsioButton_;
    juce::TextButton closeButton_;

    bool applyInFlight_ = false;
    bool closeAfterApply_ = false;

    struct ApplyProgressTimer : public juce::Timer
    {
        explicit ApplyProgressTimer(AudioDevicePanelUI& owner) : owner_(owner) {}

        void timerCallback() override
        {
            if (!owner_.applyInFlight_)
            {
                stopTimer();
                owner_.applyProgress_ = 0.0f;
                owner_.repaint(owner_.applyButton_.getBounds());
                return;
            }

            // Ping-pong style indeterminate bar.
            // Keeps motion smooth without relying on paint cadence.
            const float speed = 0.02f;
            owner_.applyProgress_ += speed * owner_.applyProgressDir_;
            if (owner_.applyProgress_ >= 1.0f) { owner_.applyProgress_ = 1.0f; owner_.applyProgressDir_ = -1.0f; }
            if (owner_.applyProgress_ <= 0.0f) { owner_.applyProgress_ = 0.0f; owner_.applyProgressDir_ =  1.0f; }
            owner_.repaint(owner_.applyButton_.getBounds());
        }

        AudioDevicePanelUI& owner_;
    };

    ApplyProgressTimer applyProgressTimer_ { *this };
    float applyProgress_ = 0.0f;
    float applyProgressDir_ = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDevicePanelUI)
};

} // namespace DAW
