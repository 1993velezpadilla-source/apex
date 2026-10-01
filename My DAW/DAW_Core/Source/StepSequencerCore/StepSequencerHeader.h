#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"
#include "StepSequencerLookAndFeel.h"

namespace DAW {

class StepSequencerHeader : public juce::Component {
public:
    StepSequencerHeader(StepSequencerModel& model);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setGridArea(juce::Rectangle<int> gridArea) { gridArea_ = gridArea; }

    std::function<void()> onAddChannel;
    std::function<void()> onRemoveChannel;

private:
    StepSequencerModel& model_;
    juce::Rectangle<int> gridArea_;
    juce::ComboBox patternSelector_;
    juce::TextButton prevPatternBtn_{"<"};
    juce::TextButton nextPatternBtn_{">"};
    juce::TextButton addPatternBtn_{"+"};
    juce::TextButton clonePatternBtn_{"Clone"};
    juce::TextButton addChannelBtn_{"+"};
    juce::TextButton removeChannelBtn_{"-"};
    juce::Slider swingSlider_;
    juce::ComboBox stepsPerBeatCombo_;
    juce::ComboBox barsCombo_;

    void refreshPatternSelector();
    void patternChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerHeader)
};

} // namespace DAW
