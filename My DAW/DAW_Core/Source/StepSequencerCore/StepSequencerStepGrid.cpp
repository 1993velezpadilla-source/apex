#include "StepSequencerStepGrid.h"

namespace DAW {

StepSequencerStepGrid::StepSequencerStepGrid(StepSequencerModel& model, int channelIndex)
    : model_(model), channelIndex_(channelIndex) {}

int StepSequencerStepGrid::getStepFromX(float x) const {
    const auto& snap = model_.getSnapshot();
    const int totalSteps = snap.totalSteps;
    const float stepWidth = getWidth() / (float)totalSteps;
    return juce::jlimit(0, totalSteps - 1, (int)(x / stepWidth));
}

juce::Rectangle<float> StepSequencerStepGrid::getStepBounds(int stepIndex) const {
    const auto& snap = model_.getSnapshot();
    const int totalSteps = snap.totalSteps;
    const float stepWidth = (float)getWidth() / totalSteps;
    return { stepIndex * stepWidth, 0.0f, stepWidth, (float)getHeight() };
}

void StepSequencerStepGrid::paint(juce::Graphics& g) {
    const auto& snap = model_.getSnapshot();
    if (channelIndex_ >= snap.lanes.size()) return;
    const auto& lane = snap.lanes[channelIndex_];
    const int totalSteps = snap.totalSteps;
    const int stepsPerBeat = snap.stepsPerBeat;
    const int beatsPerBar = snap.beatsPerBar;

    StepSequencerLookAndFeel lnf;
    g.fillAll(lnf.backgroundColour);

    const float stepWidth = (float)getWidth() / totalSteps;

    for (int i = 0; i < totalSteps; ++i) {
        float x = i * stepWidth;

        bool isDownbeat = (i % (stepsPerBeat * beatsPerBar)) == 0;
        bool isBeat1 = (i % stepsPerBeat) == 0;

        if (isDownbeat && i > 0) {
            g.setColour(lnf.barLineColour);
            g.drawLine(x, 0.0f, x, (float)getHeight(), 1.5f);
        } else if (isBeat1 && i > 0) {
            g.setColour(lnf.beatLineColour);
            g.drawLine(x, 0.0f, x, (float)getHeight(), 0.5f);
        }

        const bool isOn = (i < lane.steps.size()) ? lane.steps[i].active : false;
        const bool isMuted = lane.muted;
        const float velocity = (i < lane.steps.size()) ? lane.steps[i].velocity : 1.0f;

        auto bounds = getStepBounds(i).reduced(1.0f);
        lnf.drawStepButton(g, bounds, isOn, isBeat1, isMuted, isDownbeat, velocity);

        if (i == currentStep_) {
            g.setColour(juce::Colour(0xff4488ff).withAlpha(0.25f));
            g.fillRect(bounds);
        }
    }
}

void StepSequencerStepGrid::mouseDown(const juce::MouseEvent& e) {
    const int step = getStepFromX((float)e.getPosition().x);
    model_.toggleStep(channelIndex_, step);
    const auto& steps = model_.getChannel(channelIndex_).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        dragToggleState_ = steps[step].active;
    isDragging_ = true;
    repaint();
}

void StepSequencerStepGrid::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_) return;
    const int step = getStepFromX((float)e.getPosition().x);
    auto& steps = model_.getChannel(channelIndex_).steps;
    if (juce::isPositiveAndBelow(step, steps.size())) {
        if (steps[step].active != dragToggleState_) {
            model_.toggleStep(channelIndex_, step);
            repaint();
        }
    }
}

void StepSequencerStepGrid::mouseUp(const juce::MouseEvent&) {
    isDragging_ = false;
}

void StepSequencerStepGrid::mouseWheelMove(
    const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const int step = getStepFromX((float)e.getPosition().x);
    auto& steps = model_.getChannel(channelIndex_).steps;
    if (juce::isPositiveAndBelow(step, steps.size())) {
        float newProb = juce::jlimit(0.0f, 1.0f,
            steps[step].probability + (float)wheel.deltaY * 0.1f);
        model_.setStepProbability(channelIndex_, step, newProb);
        repaint();
    }
}

} // namespace DAW
