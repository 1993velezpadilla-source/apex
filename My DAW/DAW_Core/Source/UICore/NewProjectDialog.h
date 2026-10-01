#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../StateCore/ApplicationState.h"
#include "../DeviceCore/DeviceSessionCore.h"

namespace DAW {

class NewProjectDialog : public juce::Component
{
public:
    std::function<void(const juce::String& name, const juce::String& key,
                        double bpm, int timeSigNum, int timeSigDen,
                        const juce::String& deviceType, const juce::String& deviceName)> onCreate;
    std::function<void()> onCancel;

    NewProjectDialog()
    {
        setOpaque(false);
        setAlwaysOnTop(true);

        nameEditor_.setText("My Project");
        nameEditor_.setSelectAllWhenFocused(true);
        addAndMakeVisible(nameEditor_);

        keyCombo_.addItemList({
            "C Maj", "C# Maj", "D Maj", "D# Maj", "E Maj", "F Maj",
            "F# Maj", "G Maj", "G# Maj", "A Maj", "A# Maj", "B Maj",
            "C Min", "C# Min", "D Min", "D# Min", "E Min", "F Min",
            "F# Min", "G Min", "G# Min", "A Min", "A# Min", "B Min"
        }, 1);
        keyCombo_.setSelectedItemIndex(0);
        addAndMakeVisible(keyCombo_);

        bpmSlider_.setRange(20.0, 300.0, 0.1);
        bpmSlider_.setValue(120.0);
        bpmSlider_.setSliderStyle(juce::Slider::IncDecButtons);
        bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        addAndMakeVisible(bpmSlider_);

        timeSigNum_.addItemList({"2", "3", "4", "5", "6", "7", "8"}, 1);
        timeSigNum_.setSelectedItemIndex(2);
        addAndMakeVisible(timeSigNum_);

        timeSigDen_.addItemList({"2", "4", "8", "16"}, 1);
        timeSigDen_.setSelectedItemIndex(1);
        addAndMakeVisible(timeSigDen_);

        deviceTypeCombo_.addItemList({"ASIO", "WASAPI", "DirectSound", "Windows Audio"}, 1);
        deviceTypeCombo_.setSelectedItemIndex(0);
        addAndMakeVisible(deviceTypeCombo_);
        deviceTypeCombo_.onChange = [this] { updateDeviceNames(); };
        updateDeviceNames();

        createBtn_.setButtonText("Create Project");
        createBtn_.onClick = [this]
        {
            if (onCreate)
            {
                auto keyItems = keyCombo_.getItemText(keyCombo_.getSelectedItemIndex());
                auto deviceItems = deviceNameCombo_.getItemText(deviceNameCombo_.getSelectedItemIndex());
                onCreate(
                    nameEditor_.getText(),
                    keyItems,
                    bpmSlider_.getValue(),
                    std::stoi(timeSigNum_.getText().toStdString()),
                    std::stoi(timeSigDen_.getText().toStdString()),
                    deviceTypeCombo_.getText(),
                    deviceNameCombo_.getText()
                );
            }
        };
        addAndMakeVisible(createBtn_);

        cancelBtn_.setButtonText("Cancel");
        cancelBtn_.onClick = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible(cancelBtn_);
    }

    void resized() override
    {
        auto box = getLocalBounds().withSizeKeepingCentre(380, 360);
        auto inner = box.reduced(24);

        // Title
        auto titleArea = inner.removeFromTop(28);

        inner.removeFromTop(12);

        const int rowH = 22;
        const int gap = 8;
        const int labelW = 110;
        const int editorIndent = 14;

        // Project Name
        auto row = inner.removeFromTop(rowH + 4);
        auto labelR = row.removeFromLeft(labelW);
        nameEditor_.setBounds(row.reduced(editorIndent, 0));

        inner.removeFromTop(gap);

        // Master Key
        row = inner.removeFromTop(rowH + 4);
        labelR = row.removeFromLeft(labelW);
        keyCombo_.setBounds(row.reduced(editorIndent, 0).removeFromLeft(120));

        inner.removeFromTop(gap);

        // BPM
        row = inner.removeFromTop(rowH + 4);
        labelR = row.removeFromLeft(labelW);
        bpmSlider_.setBounds(row.reduced(editorIndent, 0).removeFromLeft(140));

        inner.removeFromTop(gap);

        // Time Signature
        row = inner.removeFromTop(rowH + 4);
        labelR = row.removeFromLeft(labelW);
        auto tsRow = row.reduced(editorIndent, 0).removeFromLeft(140);
        timeSigNum_.setBounds(tsRow.removeFromLeft(50));
        tsRow.removeFromLeft(6);
        timeSigDen_.setBounds(tsRow.removeFromLeft(50));

        inner.removeFromTop(gap);

        // Audio Device Type
        row = inner.removeFromTop(rowH + 4);
        labelR = row.removeFromLeft(labelW);
        deviceTypeCombo_.setBounds(row.reduced(editorIndent, 0));

        inner.removeFromTop(gap);

        // Audio Device Name
        row = inner.removeFromTop(rowH + 4);
        labelR = row.removeFromLeft(labelW);
        deviceNameCombo_.setBounds(row.reduced(editorIndent, 0));

        inner.removeFromTop(14);

        // Buttons
        auto btnArea = inner.removeFromBottom(28);
        auto btnW = 120;
        cancelBtn_.setBounds(btnArea.getCentreX() - btnW - 6, btnArea.getY(), btnW, 24);
        createBtn_.setBounds(btnArea.getCentreX() + 6, btnArea.getY(), btnW, 24);
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        g.fillAll(juce::Colours::black.withAlpha(0.75f));

        auto box = getLocalBounds().withSizeKeepingCentre(380, 360).toFloat();
        g.setColour(t.colors.surface);
        g.fillRoundedRectangle(box, 10.f);
        g.setColour(t.colors.border.withAlpha(0.5f));
        g.drawRoundedRectangle(box, 10.f, 1.5f);

        auto inner = box.reduced(24.f);

        // Title
        g.setFont(juce::Font(18.f, juce::Font::bold));
        g.setColour(t.colors.accent);
        g.drawText("New Project", inner.removeFromTop(28.f), juce::Justification::centred);

        inner.removeFromTop(12.f);

        // Labels
        const float labelW = 110.f;
        const float rowH = 22.f + 4.f;
        const float gap = 8.f;
        const float editorIndent = 14.f;

        g.setFont(juce::Font(11.f));
        g.setColour(t.colors.textSecondary.withAlpha(0.7f));

        auto drawLabel = [&](const juce::String& text)
        {
            auto r = inner.removeFromTop(rowH);
            g.drawText(text, r.removeFromLeft(labelW).reduced(4, 0), juce::Justification::centredLeft);
            inner.removeFromTop(gap);
        };

        drawLabel("Project Name");
        drawLabel("Master Key");
        drawLabel("BPM");
        drawLabel("Time Signature");
        drawLabel("Audio Device Type");
        drawLabel("Audio Device");
    }

    bool hitTest(int, int) override { return true; }

private:
    juce::TextEditor nameEditor_;
    juce::ComboBox keyCombo_;
    juce::Slider bpmSlider_;
    juce::ComboBox timeSigNum_;
    juce::ComboBox timeSigDen_;
    juce::ComboBox deviceTypeCombo_;
    juce::ComboBox deviceNameCombo_;
    juce::TextButton createBtn_;
    juce::TextButton cancelBtn_;

    void updateDeviceNames()
    {
        deviceNameCombo_.clear();
        deviceNameCombo_.addItem("Default Device", 1);
        deviceNameCombo_.setSelectedItemIndex(0);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewProjectDialog)
};

} // namespace DAW
