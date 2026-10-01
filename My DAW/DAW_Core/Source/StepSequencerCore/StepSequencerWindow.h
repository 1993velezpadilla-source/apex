#pragma once
#include <JuceHeader.h>
#include "StepSequencerComponent.h"

namespace DAW {

class TransportController;

class StepSequencerWindow : public juce::DocumentWindow,
                             private juce::Timer {
public:
    StepSequencerWindow();
    ~StepSequencerWindow() override;

    StepSequencerComponent& getStepSequencer() { return stepSequencer_; }

    void setTransportReference(TransportController* transport, double sampleRate);

    void closeButtonPressed() override;

private:
    StepSequencerComponent stepSequencer_;
    TransportController* transport_ = nullptr;
    double sampleRate_ = 44100.0;
    int currentStep_ = -1;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerWindow)
};

} // namespace DAW
