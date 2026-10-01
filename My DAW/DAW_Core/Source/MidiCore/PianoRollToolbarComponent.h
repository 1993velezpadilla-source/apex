#pragma once

#include <JuceHeader.h>
#include "PianoRollComponent.h"

namespace DAW {

class PianoRollToolbarComponent : public juce::Component
{
public:
    PianoRollToolbarComponent();

    void bind(PianoRollComponent* pianoRoll);
    void resized() override;

private:
    void refreshFromBoundComponent();

    PianoRollComponent* pianoRoll_ = nullptr;
    juce::Label gridLabel_;
    juce::ComboBox gridCombo_;
    juce::ToggleButton snapToggle_;
    juce::ToggleButton scaleSnapToggle_;
    juce::Label colorLabel_;
    juce::ComboBox colorCombo_;
    juce::Label scaleRootLabel_;
    juce::ComboBox scaleRootCombo_;
    juce::Label scaleNameLabel_;
    juce::ComboBox scaleNameCombo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollToolbarComponent)
};

} // namespace DAW
