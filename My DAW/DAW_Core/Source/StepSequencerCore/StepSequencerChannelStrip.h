#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"
#include "StepSequencerStepGrid.h"

namespace DAW {

class StepSequencerChannelStrip : public juce::Component {
public:
    StepSequencerChannelStrip(StepSequencerModel& model, int channelIndex);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void setChannelIndex(int index);
    void setCurrentStep(int step) { grid_.setCurrentStep(step); }

    std::function<void(int channelIndex, const juce::String& filePath)> onSampleLoaded;

private:
    StepSequencerModel& model_;
    int channelIndex_ = 0;
    StepSequencerStepGrid grid_;

    juce::Label nameLabel_;
    juce::Slider volumeSlider_;
    juce::Slider panSlider_;
    juce::ToggleButton muteButton_;
    juce::ToggleButton soloButton_;
    juce::TextButton loadSampleBtn_{"Load"};

    juce::FileChooser fileChooser_{"Load Sample", juce::File{}, "*.wav;*.mp3;*.flac;*.ogg;*.aiff"};

    void updateFromModel();
    void loadSampleClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerChannelStrip)
};

} // namespace DAW
