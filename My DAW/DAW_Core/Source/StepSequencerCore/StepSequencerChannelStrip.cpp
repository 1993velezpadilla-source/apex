#include "StepSequencerChannelStrip.h"

namespace DAW {

StepSequencerChannelStrip::StepSequencerChannelStrip(
    StepSequencerModel& model, int channelIndex)
    : model_(model), channelIndex_(channelIndex), grid_(model, channelIndex)
{
    addAndMakeVisible(nameLabel_);
    addAndMakeVisible(volumeSlider_);
    addAndMakeVisible(panSlider_);
    addAndMakeVisible(muteButton_);
    addAndMakeVisible(soloButton_);
    addAndMakeVisible(loadSampleBtn_);
    addAndMakeVisible(grid_);

    nameLabel_.setEditable(true);
    nameLabel_.setText(model_.getChannel(channelIndex_).name, juce::dontSendNotification);
    nameLabel_.onTextChange = [this]() {
        model_.getChannel(channelIndex_).name = nameLabel_.getText();
    };

    volumeSlider_.setRange(0.0, 2.0, 0.01);
    volumeSlider_.setValue(model_.getChannel(channelIndex_).volume, juce::dontSendNotification);
    volumeSlider_.onValueChange = [this]() {
        model_.getChannel(channelIndex_).volume = (float)volumeSlider_.getValue();
    };

    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(model_.getChannel(channelIndex_).pan, juce::dontSendNotification);
    panSlider_.onValueChange = [this]() {
        model_.getChannel(channelIndex_).pan = (float)panSlider_.getValue();
    };

    muteButton_.setButtonText("M");
    muteButton_.onClick = [this]() {
        model_.getChannel(channelIndex_).muted = muteButton_.getToggleState();
        repaint();
    };

    soloButton_.setButtonText("S");
    soloButton_.onClick = [this]() {
        model_.getChannel(channelIndex_).soloed = soloButton_.getToggleState();
    };

    loadSampleBtn_.onClick = [this]() { loadSampleClicked(); };
}

void StepSequencerChannelStrip::setChannelIndex(int index) {
    channelIndex_ = index;
    grid_.setChannelIndex(index);
    updateFromModel();
}

void StepSequencerChannelStrip::updateFromModel() {
    const auto& ch = model_.getChannel(channelIndex_);
    nameLabel_.setText(ch.name, juce::dontSendNotification);
    volumeSlider_.setValue(ch.volume, juce::dontSendNotification);
    panSlider_.setValue(ch.pan, juce::dontSendNotification);
    muteButton_.setToggleState(ch.muted, juce::dontSendNotification);
    soloButton_.setToggleState(ch.soloed, juce::dontSendNotification);
}

void StepSequencerChannelStrip::loadSampleClicked() {
    fileChooser_.launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile() && onSampleLoaded)
            {
                onSampleLoaded(channelIndex_, file.getFullPathName());
                model_.getChannel(channelIndex_).sampleFilePath = file.getFullPathName();
                model_.getChannel(channelIndex_).name = file.getFileNameWithoutExtension();
                updateFromModel();
            }
        });
}

void StepSequencerChannelStrip::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff222244));
}

void StepSequencerChannelStrip::resized() {
    auto area = getLocalBounds();
    const int controlsWidth = 160;
    auto controls = area.removeFromLeft(controlsWidth);

    nameLabel_.setBounds(controls.removeFromTop(20));
    auto knobs = controls.removeFromTop(24);
    volumeSlider_.setBounds(knobs.removeFromLeft(40));
    panSlider_.setBounds(knobs.removeFromLeft(40));
    muteButton_.setBounds(knobs.removeFromLeft(22));
    soloButton_.setBounds(knobs.removeFromLeft(22));
    loadSampleBtn_.setBounds(knobs);

    grid_.setBounds(area);
}

} // namespace DAW
