#include "StepSequencerWindow.h"
#include "../TransportCore/TransportController.h"

namespace DAW {

StepSequencerWindow::StepSequencerWindow()
    : DocumentWindow("Step Sequencer",
                     juce::Colour(0xff1a1a2e),
                     DocumentWindow::allButtons)
{
    setResizable(true, true);
    setResizeLimits(600, 200, 1600, 900);
    setContentNonOwned(&stepSequencer_, true);
    centreWithSize(800, 400);
    setVisible(false);
}

StepSequencerWindow::~StepSequencerWindow() {
    stopTimer();
}

void StepSequencerWindow::setTransportReference(TransportController* transport, double sampleRate) {
    transport_ = transport;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    startTimerHz(60);
}

void StepSequencerWindow::timerCallback() {
    if (transport_ == nullptr || !transport_->isPlaying()) {
        if (currentStep_ != -1) {
            currentStep_ = -1;
            stepSequencer_.setCurrentStep(-1);
        }
        return;
    }

    const auto& snap = stepSequencer_.getModel().getSnapshot();
    if (snap.totalSteps <= 0 || snap.lanes.isEmpty())
        return;

    const double tempo = transport_->getTempo();
    if (tempo <= 0.0) return;

    const double beatsPerBar = static_cast<double>(snap.beatsPerBar);
    const double stepsPerBeat = static_cast<double>(snap.stepsPerBeat);
    const double totalSteps = static_cast<double>(snap.totalSteps);

    const double samplesPerBeat = sampleRate_ * 60.0 / tempo;
    const double samplesPerPattern = samplesPerBeat * beatsPerBar
                                     * static_cast<double>(snap.barsPerPattern);
    if (samplesPerPattern <= 0.0) return;

    const auto pos = transport_->getPosition();
    const double patternPos = static_cast<double>(pos % static_cast<SamplePosition>(samplesPerPattern));
    const int step = juce::jlimit(0, snap.totalSteps - 1,
                                  static_cast<int>((patternPos / samplesPerPattern) * totalSteps));

    if (step != currentStep_) {
        currentStep_ = step;
        stepSequencer_.setCurrentStep(step);
    }
}

void StepSequencerWindow::closeButtonPressed() {
    setVisible(false);
}

} // namespace DAW
