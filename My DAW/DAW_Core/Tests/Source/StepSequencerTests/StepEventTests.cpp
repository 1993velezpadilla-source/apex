#include <JuceHeader.h>
#include "../../Source/StepSequencerCore/StepSequencerTypes.h"

class StepEventTests final : public juce::UnitTest
{
public:
    StepEventTests() : juce::UnitTest ("StepEvent.Schema", "APEX.StepSequencer") {}

    void runTest() override
    {
        beginTest ("StepEvent defaults");
        {
            DAW::StepEvent e;
            expect (!e.active);
            expectWithinAbsoluteError (e.velocity, 1.0f, 0.001f);
            expectEquals ((int)e.pitchOffset, 0);
            expectWithinAbsoluteError (e.pan, 0.0f, 0.001f);
            expectEquals ((int)e.timingShift, 0);
            expectWithinAbsoluteError (e.probability, 1.0f, 0.001f);
            expectEquals (e.duration, (int32_t)0);
            expectEquals (e.ratchetCount, 1);
            expectEquals (e.ratchetSpacing, 0);
            expect (!e.flamEnabled);
            expectEquals (e.flamOffsetTicks, 0);
            expect (!e.tieToNext);
            expect (e.condition == DAW::StepEvent::Condition::Every);
            expectEquals (e.microtimingTicks, 0);
            expect (!e.swingBypass);
        }

        beginTest ("StepEvent IDs are unique");
        {
            DAW::StepEvent a, b;
            expect (!a.id.isNull());
            expect (a.id != b.id);
        }

        beginTest ("LaneData defaults");
        {
            DAW::LaneData lane;
            expect (!lane.id.isNull());
            expect (lane.name == "Lane");
            expectEquals (lane.stepsPerBeat, 4);
            expectEquals (lane.beatsPerBar, 4);
            expectEquals (lane.barsPerLane, 1);
            expectWithinAbsoluteError (lane.swingAmount, 0.0f, 0.001f);
            expectEquals (lane.midiNote, 60);
            expectEquals (lane.midiChannel, 0);
            expectWithinAbsoluteError (lane.volume, 1.0f, 0.001f);
            expectWithinAbsoluteError (lane.pan, 0.0f, 0.001f);
            expect (!lane.muted);
            expect (!lane.soloed);
        }

        beginTest ("LaneData ID uniqueness");
        {
            DAW::LaneData a, b;
            expect (a.id != b.id);
        }

        beginTest ("GrooveTemplate defaults");
        {
            DAW::GrooveTemplate g;
            expect (!g.id.isNull());
            expect (g.name == "Default");
            expectWithinAbsoluteError (g.swingAmount, 0.0f, 0.001f);
            expectWithinAbsoluteError (g.shuffleAmount, 0.0f, 0.001f);
            expectEquals (g.microTimingPreset, 0);
        }
    }
};

static StepEventTests stepEventTests;
