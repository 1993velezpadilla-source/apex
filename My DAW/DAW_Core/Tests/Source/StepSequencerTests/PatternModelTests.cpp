#include <JuceHeader.h>
#include "../../Source/StepSequencerCore/StepSequencerModel.h"

class PatternModelTests final : public juce::UnitTest
{
public:
    PatternModelTests() : juce::UnitTest ("StepSequencerModel.PatternModel", "APEX.StepSequencer") {}

    void runTest() override
    {
        beginTest ("Lane add and remove");
        {
            DAW::StepSequencerModel model;
            expectEquals (model.getLaneCount(), 0);

            int idx0 = model.addLane("Kick");
            expectEquals (model.getLaneCount(), 1);
            expect (model.getLane(idx0).name == "Kick");

            model.addLane("Snare");
            expectEquals (model.getLaneCount(), 2);

            model.removeLane(idx0);
            expectEquals (model.getLaneCount(), 1);
            expect (model.getLane(0).name == "Snare");
        }

        beginTest ("Lane move");
        {
            DAW::StepSequencerModel model;
            model.addLane("A");
            model.addLane("B");
            expectEquals (model.getLaneCount(), 2);

            model.moveLane(1, 0);
            expect (model.getLane(0).name == "B");
            expect (model.getLane(1).name == "A");
        }

        beginTest ("Toggle step");
        {
            DAW::StepSequencerModel model;
            model.addLane("Kick");
            auto& lane = model.getLane(0);
            expect (lane.steps.size() == 16);
            expect (!lane.steps[0].active);

            model.toggleStep(0, 0);
            expect (model.getLane(0).steps[0].active);

            model.toggleStep(0, 0);
            expect (!model.getLane(0).steps[0].active);

            model.toggleStep(0, 15);
            expect (model.getLane(0).steps[15].active);
        }

        beginTest ("Set step velocity");
        {
            DAW::StepSequencerModel model;
            model.addLane("Kick");
            model.toggleStep(0, 0);
            model.setStepVelocity(0, 0, 0.5f);
            expectWithinAbsoluteError (model.getLane(0).steps[0].velocity, 0.5f, 0.001f);

            model.setStepVelocity(0, 0, 1.0f);
            expectWithinAbsoluteError (model.getLane(0).steps[0].velocity, 1.0f, 0.001f);
        }

        beginTest ("Set step probability");
        {
            DAW::StepSequencerModel model;
            model.addLane("Kick");
            model.toggleStep(0, 0);
            model.setStepProbability(0, 0, 0.75f);
            expectWithinAbsoluteError (model.getLane(0).steps[0].probability, 0.75f, 0.001f);
        }

        beginTest ("Set step pitch offset");
        {
            DAW::StepSequencerModel model;
            model.addLane("Kick");
            model.toggleStep(0, 0);
            model.setStepPitchOffset(0, 0, 12);
            expectEquals ((int)model.getLane(0).steps[0].pitchOffset, 12);

            model.setStepPitchOffset(0, 0, -5);
            expectEquals ((int)model.getLane(0).steps[0].pitchOffset, -5);
        }

        beginTest ("Snapshot building with lane fields");
        {
            DAW::StepSequencerModel model;
            int idx = model.addLane("TestLane");
            model.toggleStep(idx, 0);
            model.setStepVelocity(idx, 0, 0.8f);
            model.setStepProbability(idx, 0, 0.9f);
            model.setStepPitchOffset(idx, 0, 3);

            auto snap = model.getSnapshot();
            expectEquals (snap.lanes.size(), 1);
            expectEquals (snap.totalSteps, 16);
            expectWithinAbsoluteError (snap.globalSwing, 0.0f, 0.001f);

            expect (snap.lanes[0].name == "TestLane");
            expect (snap.lanes[0].steps[0].active);
            expectWithinAbsoluteError (snap.lanes[0].steps[0].velocity, 0.8f, 0.001f);
            expectWithinAbsoluteError (snap.lanes[0].steps[0].probability, 0.9f, 0.001f);
            expectEquals ((int)snap.lanes[0].steps[0].pitchOffset, 3);

            expect (!snap.patternId.isNull());
            expect (snap.name == "Pattern 1");
            expectWithinAbsoluteError (snap.lanes[0].swingAmount, 0.0f, 0.001f);
            expectEquals (snap.lanes[0].laneLength, 0);
        }

        beginTest ("Lane polymeter (laneLength != 0)");
        {
            DAW::StepSequencerModel model;
            int idx = model.addLane("Poly");
            model.getLane(idx).laneLength = 12;
            expectEquals (model.getLane(idx).getTotalSteps(), 12);

            auto snap = model.getSnapshot();
            expectEquals (snap.lanes[0].laneLength, 12);
        }

        beginTest ("Lane defaults");
        {
            DAW::LaneData lane;
            expect (!lane.id.isNull());
            expect (lane.name == "Lane");
            expectEquals (lane.stepsPerBeat, 4);
            expectEquals (lane.beatsPerBar, 4);
            expectEquals (lane.barsPerLane, 1);
            expectEquals (lane.laneLength, 0);
            expectEquals (lane.mixerTrackIndex, -1);
            expectEquals (lane.vstInstanceId, -1);
            expect (lane.sampleFilePath.isEmpty());
            expect (lane.sourceType == DAW::LaneData::SourceType::None);
            expectEquals (lane.getTotalSteps(), 16);
        }

        beginTest ("getChannel backward compat returns same as getLane");
        {
            DAW::StepSequencerModel model;
            int idx = model.addLane("X");
            model.toggleStep(idx, 0);
            model.setStepVelocity(idx, 0, 0.5f);

            auto& ch = model.getChannel(idx);
            auto& ln = model.getLane(idx);
            expect (&ch == &ln);
            expect (ch.name == "X");
            expect (ch.steps[0].active);
            expectWithinAbsoluteError (ch.steps[0].velocity, 0.5f, 0.001f);
        }

        beginTest ("getChannels backward compat");
        {
            DAW::StepSequencerModel model;
            model.addLane("A");
            auto& channels = model.getChannels();
            expectEquals (channels.size(), 1);
            expect (channels[0].name == "A");
        }

        beginTest ("RT snapshot publication is atomic, latest-wins, allocation-free");
        {
            DAW::StepSequencerModel model;
            model.addLane ("Kick");
            model.toggleStep (0, 0);
            model.publishSnapshot();

            auto first = model.getSnapshotRT();
            expect (first != nullptr);
            expectEquals (first->lanes.size(), 1);
            expect (first->lanes[0].steps[0].active);

            // Re-publish replaces the published snapshot; a previously
            // acquired shared_ptr keeps the retired snapshot alive and valid
            // (audio-thread lifetime contract).
            model.toggleStep (0, 1);
            model.publishSnapshot();
            auto second = model.getSnapshotRT();
            expect (second != nullptr);
            expect (second.get() != first.get());
            expect (first->lanes[0].steps[0].active);
            expect (! first->lanes[0].steps[1].active);
            expect (second->lanes[0].steps[1].active);

            // getSnapshotRT must be allocation-free (audio-thread contract;
            // previously it acquired a std::mutex on every call).
            bool nonNull = true;
            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int i = 0; i < 1000; ++i)
                    nonNull = nonNull && (model.getSnapshotRT() != nullptr);
            }
            expect (nonNull);
        }
    }
};

static PatternModelTests patternModelTests;
