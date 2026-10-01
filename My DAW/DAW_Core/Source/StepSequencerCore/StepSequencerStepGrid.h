#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"
#include "StepSequencerLookAndFeel.h"

namespace DAW {

class StepSequencerStepGrid : public juce::Component {
public:
    StepSequencerStepGrid(StepSequencerModel& model, int channelIndex);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void setChannelIndex(int index) { channelIndex_ = index; }
    int getChannelIndex() const noexcept { return channelIndex_; }
    void setCurrentStep(int step) { currentStep_ = step; repaint(); }

private:
    StepSequencerModel& model_;
    int channelIndex_ = 0;
    int currentStep_ = -1;
    bool dragToggleState_ = false;
    bool isDragging_ = false;

    int getStepFromX(float x) const;
    juce::Rectangle<float> getStepBounds(int stepIndex) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerStepGrid)
};

} // namespace DAW
