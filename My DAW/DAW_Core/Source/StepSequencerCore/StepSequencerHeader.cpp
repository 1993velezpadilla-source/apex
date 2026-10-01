#include "StepSequencerHeader.h"

namespace DAW {

StepSequencerHeader::StepSequencerHeader(StepSequencerModel& model)
    : model_(model)
{
    addAndMakeVisible(patternSelector_);
    addAndMakeVisible(prevPatternBtn_);
    addAndMakeVisible(nextPatternBtn_);
    addAndMakeVisible(addPatternBtn_);
    addAndMakeVisible(clonePatternBtn_);
    addAndMakeVisible(addChannelBtn_);
    addAndMakeVisible(removeChannelBtn_);
    addAndMakeVisible(swingSlider_);
    addAndMakeVisible(stepsPerBeatCombo_);
    addAndMakeVisible(barsCombo_);

    refreshPatternSelector();

    prevPatternBtn_.onClick = [this]() {
        int idx = model_.getCurrentPatternIndex();
        if (idx > 0) { model_.setCurrentPattern(idx - 1); patternChanged(); }
    };
    nextPatternBtn_.onClick = [this]() {
        int idx = model_.getCurrentPatternIndex();
        if (idx < model_.getPatternCount() - 1) { model_.setCurrentPattern(idx + 1); patternChanged(); }
    };
    addPatternBtn_.onClick = [this]() {
        model_.addPattern(); patternChanged();
    };
    clonePatternBtn_.onClick = [this]() {
        model_.clonePattern(model_.getCurrentPatternIndex()); patternChanged();
    };
    addChannelBtn_.onClick = [this]() {
        if (onAddChannel) onAddChannel();
    };
    removeChannelBtn_.onClick = [this]() {
        if (onRemoveChannel) onRemoveChannel();
    };

    swingSlider_.setRange(0.0, 1.0, 0.01);
    swingSlider_.setSliderStyle(juce::Slider::LinearHorizontal);

    stepsPerBeatCombo_.addItemList({"4", "6", "8", "12", "16", "24", "32"}, 1);
    barsCombo_.addItemList({"1", "2", "4", "8", "16"}, 1);
}

void StepSequencerHeader::refreshPatternSelector() {
    patternSelector_.clear();
    for (int i = 0; i < model_.getPatternCount(); ++i)
        patternSelector_.addItem(model_.getPattern(i).name, i + 1);
    patternSelector_.setSelectedId(model_.getCurrentPatternIndex() + 1, juce::dontSendNotification);
}

void StepSequencerHeader::patternChanged() {
    refreshPatternSelector();
    repaint();
}

void StepSequencerHeader::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff111133));

    if (gridArea_.getWidth() <= 0) return;

    const auto& snap = model_.getSnapshot();
    const int totalSteps = snap.totalSteps;
    const int stepsPerBeat = snap.stepsPerBeat;

    StepSequencerLookAndFeel lnf;

    const float stepWidth = (float)gridArea_.getWidth() / totalSteps;

    for (int i = 0; i < totalSteps; ++i) {
        if ((i % stepsPerBeat) == 0) {
            float x = gridArea_.getX() + i * stepWidth;
            juce::String label = juce::String(i / stepsPerBeat + 1);
            g.setColour(lnf.stepNumberColour);
            g.setFont(10.0f);
            g.drawText(label, juce::Rectangle<float>(x, (float)gridArea_.getY(),
                stepWidth * stepsPerBeat, (float)gridArea_.getHeight()),
                juce::Justification::centredTop, false);
        }
    }
}

void StepSequencerHeader::resized() {
    auto area = getLocalBounds().reduced(4);
    auto topRow = area.removeFromTop(28);
    prevPatternBtn_.setBounds(topRow.removeFromLeft(28));
    patternSelector_.setBounds(topRow.removeFromLeft(150));
    nextPatternBtn_.setBounds(topRow.removeFromLeft(28));
    addPatternBtn_.setBounds(topRow.removeFromLeft(28));
    clonePatternBtn_.setBounds(topRow.removeFromLeft(50));
    swingSlider_.setBounds(topRow.removeFromLeft(120));

    auto bottomRow = area.removeFromTop(24);
    stepsPerBeatCombo_.setBounds(bottomRow.removeFromLeft(60));
    barsCombo_.setBounds(bottomRow.removeFromLeft(60));
    addChannelBtn_.setBounds(bottomRow.removeFromLeft(28));
    removeChannelBtn_.setBounds(bottomRow.removeFromLeft(28));
}

} // namespace DAW
