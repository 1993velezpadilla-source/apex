#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"
#include "StepSequencerHeader.h"
#include "StepSequencerChannelStrip.h"
#include "../DrumSamplerCore/DrumSamplerEngine.h"

namespace DAW {

class StepSequencerComponent : public juce::Component,
                                private juce::ChangeListener {
public:
    StepSequencerComponent();
    ~StepSequencerComponent() override;

    StepSequencerModel& getModel() { return model_; }
    void setDrumSampler(DrumSamplerEngine* engine) { drumSampler_ = engine; }
    void setCurrentStep(int step);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    StepSequencerModel model_;
    StepSequencerHeader header_;
    juce::OwnedArray<StepSequencerChannelStrip> channelStrips_;
    DrumSamplerEngine* drumSampler_ = nullptr;

    void rebuildChannelStrips();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerComponent)
};

} // namespace DAW
