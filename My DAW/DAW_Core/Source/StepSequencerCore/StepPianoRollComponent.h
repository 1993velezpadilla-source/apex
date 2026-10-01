#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"
#include "StepSequencerLookAndFeel.h"

namespace DAW {

class StepPianoRollComponent : public juce::Component,
                                private juce::ChangeListener {
public:
    StepPianoRollComponent(StepSequencerModel& model, int laneIndex = 0);
    ~StepPianoRollComponent() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void setCurrentLane(int laneIndex);
    int getCurrentLane() const noexcept { return laneIndex_; }

    int getLowestNote() const;
    int getHighestNote() const;
    int getNumVisibleRows() const;

    int getNoteFromRow(int row) const;
    int getRowFromNote(int midiNote) const;
    int getPitchOffsetForRow(int row) const;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    StepSequencerModel& model_;
    int laneIndex_ = 0;

    static constexpr int pianoKeyWidth_ = 60;
    static constexpr int noteRange_ = 24;

    bool dragPaintActive_ = false;
    bool dragPaintState_ = false;

    juce::Rectangle<float> getCellBounds(int stepIndex, int row) const;
    int getStepFromX(float x) const;
    int getRowFromY(float y) const;

    static juce::String getNoteName(int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepPianoRollComponent)
};

} // namespace DAW
