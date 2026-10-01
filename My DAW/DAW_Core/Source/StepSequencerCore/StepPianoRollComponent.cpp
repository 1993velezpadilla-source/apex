#include "StepPianoRollComponent.h"

namespace DAW {

StepPianoRollComponent::StepPianoRollComponent(StepSequencerModel& model, int laneIndex)
    : model_(model), laneIndex_(laneIndex)
{
    model_.addChangeListener(this);
}

StepPianoRollComponent::~StepPianoRollComponent()
{
    model_.removeChangeListener(this);
}

void StepPianoRollComponent::setCurrentLane(int laneIndex)
{
    laneIndex_ = laneIndex;
    repaint();
}

int StepPianoRollComponent::getLowestNote() const
{
    const auto& lane = model_.getLane(laneIndex_);
    return juce::jmax(0, lane.midiNote - noteRange_ / 2);
}

int StepPianoRollComponent::getHighestNote() const
{
    const auto& lane = model_.getLane(laneIndex_);
    return juce::jmin(127, lane.midiNote + noteRange_ / 2);
}

int StepPianoRollComponent::getNumVisibleRows() const
{
    return getHighestNote() - getLowestNote() + 1;
}

int StepPianoRollComponent::getNoteFromRow(int row) const
{
    return getHighestNote() - row;
}

int StepPianoRollComponent::getRowFromNote(int midiNote) const
{
    return getHighestNote() - midiNote;
}

int StepPianoRollComponent::getPitchOffsetForRow(int row) const
{
    const auto& lane = model_.getLane(laneIndex_);
    return (int8_t)(getNoteFromRow(row) - lane.midiNote);
}

juce::Rectangle<float> StepPianoRollComponent::getCellBounds(int stepIndex, int row) const
{
    const auto& snap = model_.getSnapshot();
    const int totalSteps = snap.totalSteps;
    const int numRows = getNumVisibleRows();

    const float gridWidth = (float)(getWidth() - pianoKeyWidth_);
    const float cellWidth = gridWidth / totalSteps;
    const float cellHeight = (float)getHeight() / numRows;

    const float x = (float)pianoKeyWidth_ + stepIndex * cellWidth;
    const float y = row * cellHeight;

    return { x, y, cellWidth, cellHeight };
}

int StepPianoRollComponent::getStepFromX(float x) const
{
    const auto& snap = model_.getSnapshot();
    const int totalSteps = snap.totalSteps;
    const float gridWidth = (float)(getWidth() - pianoKeyWidth_);
    const float cellWidth = gridWidth / totalSteps;
    return juce::jlimit(0, totalSteps - 1, (int)((x - pianoKeyWidth_) / cellWidth));
}

int StepPianoRollComponent::getRowFromY(float y) const
{
    const int numRows = getNumVisibleRows();
    const float cellHeight = (float)getHeight() / numRows;
    return juce::jlimit(0, numRows - 1, (int)(y / cellHeight));
}

juce::String StepPianoRollComponent::getNoteName(int midiNote)
{
    static const juce::StringArray noteNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int octave = (midiNote / 12) - 1;
    int noteIndex = midiNote % 12;
    return noteNames[noteIndex] + juce::String(octave);
}

void StepPianoRollComponent::paint(juce::Graphics& g)
{
    const auto& snap = model_.getSnapshot();
    if (laneIndex_ >= snap.lanes.size()) return;
    const auto& lane = snap.lanes[laneIndex_];
    const int totalSteps = snap.totalSteps;
    const int numRows = getNumVisibleRows();
    const int stepsPerBeat = snap.stepsPerBeat;
    const int beatsPerBar = snap.beatsPerBar;

    StepSequencerLookAndFeel lnf;
    g.fillAll(lnf.backgroundColour);

    const float gridWidth = (float)(getWidth() - pianoKeyWidth_);
    const float cellWidth = gridWidth / totalSteps;
    const float cellHeight = (float)getHeight() / numRows;

    for (int row = 0; row < numRows; ++row) {
        int midiNote = getNoteFromRow(row);
        bool isBlackKey = (midiNote % 12 == 1 || midiNote % 12 == 3 ||
                           midiNote % 12 == 6 || midiNote % 12 == 8 ||
                           midiNote % 12 == 10);

        float y = row * cellHeight;
        juce::Rectangle<float> keyBounds(0.0f, y, (float)pianoKeyWidth_, cellHeight);

        if (isBlackKey)
            g.setColour(lnf.backgroundColour.brighter(0.15f));
        else
            g.setColour(lnf.backgroundColour.brighter(0.3f));
        g.fillRect(keyBounds);

        g.setColour(lnf.beatLineColour);
        g.drawLine(0.0f, y, (float)getWidth(), y, 0.5f);

        g.setColour(lnf.stepNumberColour);
        g.setFont(cellHeight * 0.6f);
        g.drawText(getNoteName(midiNote), keyBounds.reduced(2.0f),
                   juce::Justification::centredRight, false);
    }

    for (int i = 0; i < totalSteps; ++i) {
        bool isDownbeat = (i % (stepsPerBeat * beatsPerBar)) == 0;
        bool isBeat1 = (i % stepsPerBeat) == 0;

        if (isDownbeat && i > 0) {
            float x = (float)pianoKeyWidth_ + i * cellWidth;
            g.setColour(lnf.barLineColour);
            g.drawLine(x, 0.0f, x, (float)getHeight(), 1.5f);
        } else if (isBeat1 && i > 0) {
            float x = (float)pianoKeyWidth_ + i * cellWidth;
            g.setColour(lnf.beatLineColour);
            g.drawLine(x, 0.0f, x, (float)getHeight(), 0.5f);
        }
    }

    for (int row = 0; row < numRows; ++row) {
        for (int step = 0; step < totalSteps; ++step) {
            int8_t pitchOffset = (int8_t)getPitchOffsetForRow(row);

            bool isActive = false;
            float velocity = 1.0f;
            for (const auto& ev : lane.steps) {
                if (step < lane.steps.size() && &ev == &lane.steps.getReference(step)) {
                    isActive = ev.active && ev.pitchOffset == pitchOffset;
                    velocity = ev.velocity;
                    break;
                }
            }

            auto bounds = getCellBounds(step, row).reduced(0.5f);

            bool isBeat1 = (step % stepsPerBeat) == 0;
            bool isDownbeat = (step % (stepsPerBeat * beatsPerBar)) == 0;

            lnf.drawStepButton(g, bounds, isActive, isBeat1, lane.muted, isDownbeat, velocity);
        }
    }

    g.setColour(lnf.barLineColour);
    g.drawLine((float)pianoKeyWidth_, 0.0f, (float)pianoKeyWidth_, (float)getHeight(), 1.0f);
}

void StepPianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.getPosition().x < pianoKeyWidth_) return;

    const int step = getStepFromX((float)e.getPosition().x);
    const int row = getRowFromY((float)e.getPosition().y);
    const int8_t pitchOffset = (int8_t)getPitchOffsetForRow(row);

    const auto& lane = model_.getLane(laneIndex_);
    bool alreadyActive = false;
    if (juce::isPositiveAndBelow(step, lane.steps.size()))
        alreadyActive = lane.steps[step].active && lane.steps[step].pitchOffset == pitchOffset;

    if (alreadyActive) {
        model_.setStepPitchOffset(laneIndex_, step, 0);
        model_.toggleStep(laneIndex_, step);
    } else {
        model_.addStepAtPitch(laneIndex_, step, pitchOffset);
    }

    dragPaintActive_ = true;
    dragPaintState_ = !alreadyActive;
    repaint();
}

void StepPianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragPaintActive_) return;
    if (e.getPosition().x < pianoKeyWidth_) return;

    const int step = getStepFromX((float)e.getPosition().x);
    const int row = getRowFromY((float)e.getPosition().y);
    const int8_t pitchOffset = (int8_t)getPitchOffsetForRow(row);

    const auto& lane = model_.getLane(laneIndex_);
    bool alreadyActive = false;
    if (juce::isPositiveAndBelow(step, lane.steps.size()))
        alreadyActive = lane.steps[step].active && lane.steps[step].pitchOffset == pitchOffset;

    if (dragPaintState_ && !alreadyActive) {
        model_.addStepAtPitch(laneIndex_, step, pitchOffset);
        repaint();
    } else if (!dragPaintState_ && alreadyActive) {
        model_.setStepPitchOffset(laneIndex_, step, 0);
        model_.toggleStep(laneIndex_, step);
        repaint();
    }
}

void StepPianoRollComponent::mouseUp(const juce::MouseEvent&)
{
    dragPaintActive_ = false;
}

void StepPianoRollComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint();
}

} // namespace DAW
