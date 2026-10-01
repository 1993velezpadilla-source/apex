#include <JuceHeader.h>
#include "../../Source/StepSequencerCore/StepSequencerModel.h"
#include "../../Source/StepSequencerCore/StepPianoRollComponent.h"

class PianoRollTests final : public juce::UnitTest
{
public:
    PianoRollTests() : juce::UnitTest ("StepPianoRollComponent", "APEX.StepSequencer") {}

    void runTest() override
    {
        beginTest ("Component creation");
        {
            DAW::StepSequencerModel model;
            model.addChannel("Test");
            DAW::StepPianoRollComponent comp(model, 0);
            expect (comp.getCurrentLane() == 0);
        }

        beginTest ("Set lane");
        {
            DAW::StepSequencerModel model;
            model.addChannel("Ch1");
            model.addChannel("Ch2");
            DAW::StepPianoRollComponent comp(model, 0);
            expect (comp.getCurrentLane() == 0);
            comp.setCurrentLane(1);
            expect (comp.getCurrentLane() == 1);
        }

        beginTest ("Note range");
        {
            DAW::StepSequencerModel model;
            model.addChannel("Test");
            model.getLane(0).midiNote = 60;
            DAW::StepPianoRollComponent comp(model, 0);

            expectEquals (comp.getLowestNote(), 48);
            expectEquals (comp.getHighestNote(), 72);
            expectEquals (comp.getNumVisibleRows(), 25);
        }

        beginTest ("Step-to-pitch mapping");
        {
            DAW::StepSequencerModel model;
            model.addChannel("Test");
            model.getLane(0).midiNote = 60;
            DAW::StepPianoRollComponent comp(model, 0);

            int topRow = 0;
            expectEquals (comp.getNoteFromRow(topRow), 72);
            expectEquals ((int)comp.getPitchOffsetForRow(topRow), 12);

            int bottomRow = comp.getNumVisibleRows() - 1;
            expectEquals (comp.getNoteFromRow(bottomRow), 48);
            expectEquals ((int)comp.getPitchOffsetForRow(bottomRow), -12);

            int midRow = comp.getNumVisibleRows() / 2;
            expectEquals (comp.getNoteFromRow(midRow), 60);
            expectEquals ((int)comp.getPitchOffsetForRow(midRow), 0);
        }

        beginTest ("addStepAtPitch");
        {
            DAW::StepSequencerModel model;
            model.addChannel("Test");
            model.getLane(0).midiNote = 60;

            model.addStepAtPitch(0, 0, 5);
            const auto& lane = model.getLane(0);
            expect (lane.steps[0].active);
            expectEquals ((int)lane.steps[0].pitchOffset, 5);
        }
    }
};

static PianoRollTests pianoRollTests;
