#include "PianoRollToolbarComponent.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

namespace
{
    constexpr int kGridWhole = 3840;
    constexpr int kGridHalf = 1920;
    constexpr int kGridQuarter = 960;
    constexpr int kGridEighth = 480;
    constexpr int kGridSixteenth = 240;
    constexpr int kGridThirtySecond = 120;
    constexpr int kGridSixtyFourth = 60;
}

PianoRollToolbarComponent::PianoRollToolbarComponent()
{
    auto& theme = Theme::getInstance();

    auto styleLabel = [this, &theme](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(theme.fonts.small.boldened());
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, theme.colors.text);
        this->addAndMakeVisible(label);
    };

    styleLabel(gridLabel_, "Grid");
    styleLabel(colorLabel_, "Color");
    styleLabel(scaleRootLabel_, "Root");
    styleLabel(scaleNameLabel_, "Scale");

    gridCombo_.addItem("1/1", kGridWhole);
    gridCombo_.addItem("1/2", kGridHalf);
    gridCombo_.addItem("1/4", kGridQuarter);
    gridCombo_.addItem("1/8", kGridEighth);
    gridCombo_.addItem("1/16", kGridSixteenth);
    gridCombo_.addItem("1/32", kGridThirtySecond);
    gridCombo_.addItem("1/64", kGridSixtyFourth);
    gridCombo_.onChange = [this]
    {
        if (pianoRoll_ != nullptr)
            pianoRoll_->setTicksPerGridDivision(gridCombo_.getSelectedId());
    };
    addAndMakeVisible(gridCombo_);

    snapToggle_.setButtonText("Snap");
    snapToggle_.onClick = [this]
    {
        if (pianoRoll_ != nullptr)
            pianoRoll_->setSnapEnabled(snapToggle_.getToggleState());
    };
    addAndMakeVisible(snapToggle_);

    scaleSnapToggle_.setButtonText("Scale Lock");
    scaleSnapToggle_.onClick = [this]
    {
        if (pianoRoll_ != nullptr)
            pianoRoll_->setScaleSnapEnabled(scaleSnapToggle_.getToggleState());
    };
    addAndMakeVisible(scaleSnapToggle_);

    colorCombo_.addItem("Velocity", 1);
    colorCombo_.addItem("Pitch", 2);
    colorCombo_.addItem("Clip", 3);
    colorCombo_.addItem("Channel", 4);
    colorCombo_.onChange = [this]
    {
        if (pianoRoll_ == nullptr)
            return;

        switch (colorCombo_.getSelectedId())
        {
            case 2: pianoRoll_->setNoteColorMode(PianoRollComponent::NoteColorMode::Pitch); break;
            case 3: pianoRoll_->setNoteColorMode(PianoRollComponent::NoteColorMode::Clip); break;
            case 4: pianoRoll_->setNoteColorMode(PianoRollComponent::NoteColorMode::Channel); break;
            default: pianoRoll_->setNoteColorMode(PianoRollComponent::NoteColorMode::Velocity); break;
        }
    };
    addAndMakeVisible(colorCombo_);

    for (auto root : { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" })
        scaleRootCombo_.addItem(root, scaleRootCombo_.getNumItems() + 1);
    scaleRootCombo_.onChange = [this]
    {
        if (pianoRoll_ != nullptr)
            pianoRoll_->setScaleRoot(scaleRootCombo_.getText());
    };
    addAndMakeVisible(scaleRootCombo_);

    for (auto scale : { "Major", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian", "Blues", "Pentatonic" })
        scaleNameCombo_.addItem(scale, scaleNameCombo_.getNumItems() + 1);
    scaleNameCombo_.onChange = [this]
    {
        if (pianoRoll_ != nullptr)
            pianoRoll_->setScaleName(scaleNameCombo_.getText());
    };
    addAndMakeVisible(scaleNameCombo_);
}

void PianoRollToolbarComponent::bind(PianoRollComponent* pianoRoll)
{
    pianoRoll_ = pianoRoll;
    refreshFromBoundComponent();
}

void PianoRollToolbarComponent::resized()
{
    auto area = getLocalBounds().reduced(8, 4);
    const int labelW = 38;
    const int comboW = 88;
    const int toggleW = 70;
    const int scaleToggleW = 96;
    const int gap = 8;

    auto placeLabelCombo = [&](juce::Label& label, juce::ComboBox& combo)
    {
        label.setBounds(area.removeFromLeft(labelW));
        combo.setBounds(area.removeFromLeft(comboW));
        area.removeFromLeft(gap);
    };

    placeLabelCombo(gridLabel_, gridCombo_);
    snapToggle_.setBounds(area.removeFromLeft(toggleW));
    area.removeFromLeft(gap);
    scaleSnapToggle_.setBounds(area.removeFromLeft(scaleToggleW));
    area.removeFromLeft(gap);
    placeLabelCombo(colorLabel_, colorCombo_);
    placeLabelCombo(scaleRootLabel_, scaleRootCombo_);
    placeLabelCombo(scaleNameLabel_, scaleNameCombo_);
}

void PianoRollToolbarComponent::refreshFromBoundComponent()
{
    if (pianoRoll_ == nullptr)
        return;

    gridCombo_.setSelectedId((int) pianoRoll_->getTicksPerGridDivision(), juce::dontSendNotification);
    snapToggle_.setToggleState(pianoRoll_->isSnapEnabled(), juce::dontSendNotification);
    scaleSnapToggle_.setToggleState(pianoRoll_->isScaleSnapEnabled(), juce::dontSendNotification);

    switch (pianoRoll_->getNoteColorMode())
    {
        case PianoRollComponent::NoteColorMode::Pitch: colorCombo_.setSelectedId(2, juce::dontSendNotification); break;
        case PianoRollComponent::NoteColorMode::Clip: colorCombo_.setSelectedId(3, juce::dontSendNotification); break;
        case PianoRollComponent::NoteColorMode::Channel: colorCombo_.setSelectedId(4, juce::dontSendNotification); break;
        default: colorCombo_.setSelectedId(1, juce::dontSendNotification); break;
    }

    scaleRootCombo_.setText(pianoRoll_->getScaleRoot(), juce::dontSendNotification);
    scaleNameCombo_.setText(pianoRoll_->getScaleName(), juce::dontSendNotification);
}

} // namespace DAW
