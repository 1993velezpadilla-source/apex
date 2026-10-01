#include <JuceHeader.h>
#include "../../Source/StepSequencerCore/StepSequencerPlaybackCore.h"
#include "../../Source/StepSequencerCore/StepSequencerTypes.h"

static constexpr double kTempo = 120.0;
static constexpr double kSampleRate = 44100.0;
static constexpr int kStepsPerBeat = 4;

static int64_t patternSamplesForSteps (int totalSteps)
{
    const double samplesPerBeat = kSampleRate / (kTempo / 60.0);
    const double samplesPerStep = samplesPerBeat / (double)kStepsPerBeat;
    return (int64_t)((double)totalSteps * samplesPerStep);
}

class PolymeterSwingTests final : public juce::UnitTest
{
public:
    PolymeterSwingTests() : juce::UnitTest ("StepSequencer.PolymeterSwing", "APEX.StepSequencer") {}

    static DAW::StepSequencerModel::Snapshot makeSnapshot (
        int totalSteps, int beatsPerBar,
        float globalSwing,
        const juce::Array<DAW::StepSequencerModel::Snapshot::LaneSnapshot>& lanes)
    {
        DAW::StepSequencerModel::Snapshot snap;
        snap.totalSteps = totalSteps;
        snap.stepsPerBeat = kStepsPerBeat;
        snap.beatsPerBar = beatsPerBar;
        snap.barsPerPattern = 1;
        snap.globalSwing = globalSwing;
        snap.lanes = lanes;
        return snap;
    }

    static DAW::StepSequencerModel::Snapshot::LaneSnapshot makeLane (
        int laneLength, float swingAmount,
        const juce::Array<DAW::StepEvent>& steps,
        int midiNote = 60, int midiChannel = 0)
    {
        DAW::StepSequencerModel::Snapshot::LaneSnapshot lane;
        lane.laneLength = laneLength;
        lane.swingAmount = swingAmount;
        lane.steps = steps;
        lane.midiNote = midiNote;
        lane.midiChannel = midiChannel;
        lane.volume = 1.0f;
        lane.pan = 0.0f;
        lane.muted = false;
        lane.soloed = false;
        return lane;
    }

    static juce::Array<DAW::StepEvent> makeActiveSteps (int count)
    {
        juce::Array<DAW::StepEvent> steps;
        for (int i = 0; i < count; ++i)
        {
            DAW::StepEvent e;
            e.active = true;
            e.velocity = 1.0f;
            steps.add(e);
        }
        return steps;
    }

    static int countNoteOns (const juce::MidiBuffer& midi)
    {
        int n = 0;
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn() && metadata.getMessage().getVelocity() > 0)
                ++n;
        return n;
    }

    static int countNoteOnsAtMidiNote (const juce::MidiBuffer& midi, int note)
    {
        int n = 0;
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn() && metadata.getMessage().getVelocity() > 0
                && metadata.getMessage().getNoteNumber() == note)
                ++n;
        return n;
    }

    static juce::Array<int> getNoteOnPositions (const juce::MidiBuffer& midi, int note)
    {
        juce::Array<int> positions;
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn() && metadata.getMessage().getVelocity() > 0
                && metadata.getMessage().getNoteNumber() == note)
                positions.add (metadata.samplePosition);
        return positions;
    }

    void runTest() override
    {
        beginTest ("Per-lane swing override");
        {
            auto steps = makeActiveSteps(16);
            juce::Array<DAW::StepSequencerModel::Snapshot::LaneSnapshot> lanes;
            lanes.add(makeLane(0, 0.5f, steps, 60));
            auto snap = makeSnapshot(16, 4, 0.0f, lanes);
            const int blockSamples = (int)patternSamplesForSteps(16);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay(kSampleRate, blockSamples);
            juce::MidiBuffer midi;
            playback.processBlock(midi, blockSamples, 0, kTempo, snap);

            expectEquals(countNoteOns(midi), 16, "Lane with swing=0.5 should play all 16 steps");
        }

        beginTest ("Per-lane swing fallback to global");
        {
            auto steps = makeActiveSteps(16);
            juce::Array<DAW::StepSequencerModel::Snapshot::LaneSnapshot> lanes;
            lanes.add(makeLane(0, 0.0f, steps, 60));
            auto snap = makeSnapshot(16, 4, 0.3f, lanes);
            const int blockSamples = (int)patternSamplesForSteps(16);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay(kSampleRate, blockSamples);
            juce::MidiBuffer midi;
            playback.processBlock(midi, blockSamples, 0, kTempo, snap);

            expectEquals(countNoteOns(midi), 16, "Lane with swingAmount=0 should use globalSwing=0.3");
        }

        beginTest ("Polymeter basic (laneLength=12 produces 12 notes)");
        {
            auto steps = makeActiveSteps(16);
            juce::Array<DAW::StepSequencerModel::Snapshot::LaneSnapshot> lanes;
            lanes.add(makeLane(12, 0.0f, steps, 60));
            auto snap = makeSnapshot(16, 4, 0.0f, lanes);
            const int blockSamples = (int)patternSamplesForSteps(12);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay(kSampleRate, blockSamples);
            juce::MidiBuffer midi;
            playback.processBlock(midi, blockSamples, 0, kTempo, snap);

            expectEquals(countNoteOns(midi), 12, "laneLength=12 should produce exactly 12 notes per cycle");
        }

        beginTest ("Two-lane polymeter (16 vs 12 steps)");
        {
            auto steps16 = makeActiveSteps(16);
            auto steps12 = makeActiveSteps(16);

            juce::Array<DAW::StepSequencerModel::Snapshot::LaneSnapshot> lanes;
            lanes.add(makeLane(16, 0.0f, steps16, 60));
            lanes.add(makeLane(12, 0.0f, steps12, 62));
            auto snap = makeSnapshot(16, 4, 0.0f, lanes);

            const int64_t lcm16_12 = 48;
            const int blockSamples = (int)patternSamplesForSteps((int)lcm16_12);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay(kSampleRate, blockSamples);
            juce::MidiBuffer midi;
            playback.processBlock(midi, blockSamples, 0, kTempo, snap);

            expectEquals(countNoteOnsAtMidiNote(midi, 60), 16 * 3,
                         "Lane 0 (16 steps) should produce 16 notes x3 repeats in 48 steps");
            expectEquals(countNoteOnsAtMidiNote(midi, 62), 12 * 4,
                         "Lane 1 (12 steps) should produce 12 notes x4 repeats in 48 steps");
        }

        beginTest ("Polymeter + swing combined");
        {
            auto steps = makeActiveSteps(12);
            juce::Array<DAW::StepSequencerModel::Snapshot::LaneSnapshot> lanes;
            lanes.add(makeLane(12, 0.4f, steps, 60));
            auto snap = makeSnapshot(16, 4, 0.0f, lanes);
            const int blockSamples = (int)patternSamplesForSteps(12);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay(kSampleRate, blockSamples);
            juce::MidiBuffer midi;
            playback.processBlock(midi, blockSamples, 0, kTempo, snap);

            expectEquals(countNoteOns(midi), 12,
                         "laneLength=12 with swing should produce 12 notes");

            auto positions = getNoteOnPositions(midi, 60);
            expect(positions.size() == 12, "Should have 12 NoteOn positions");

            if (positions.size() >= 6)
            {
                const double samplesPerBeat = kSampleRate / (kTempo / 60.0);
                const double samplesPerStep = samplesPerBeat / (double)kStepsPerBeat;
                const int64_t ticksPerStep = 960 / kStepsPerBeat;
                const float swingTicks = (float)ticksPerStep * 0.4f * 0.5f;
                const int64_t swingSample = (int64_t)(swingTicks / 960.0 * samplesPerBeat);

                int diffUnswung = positions[1] - positions[0];
                expectWithinAbsoluteError((double)diffUnswung, samplesPerStep, 2.0,
                    "Step 0→1 (unswung) should be at samplesPerStep spacing");

                int diffSwung = positions[5] - positions[4];
                int64_t expectedSwungDiff = (int64_t)samplesPerStep + swingSample;
                expectWithinAbsoluteError((double)diffSwung, (double)expectedSwungDiff, 2.0,
                    "Step 4→5 (offbeat) should include swing offset");
            }
        }

        beginTest ("Polymeter note timing verification");
        {
            auto steps = makeActiveSteps(12);
            juce::Array<DAW::StepSequencerModel::Snapshot::LaneSnapshot> lanes;
            lanes.add(makeLane(12, 0.0f, steps, 60));
            auto snap = makeSnapshot(16, 4, 0.0f, lanes);
            const int blockSamples = (int)patternSamplesForSteps(12);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay(kSampleRate, blockSamples);
            juce::MidiBuffer midi;
            playback.processBlock(midi, blockSamples, 0, kTempo, snap);

            auto positions = getNoteOnPositions(midi, 60);
            expectEquals(positions.size(), 12, "Should have exactly 12 note positions");

            const double samplesPerBeat = kSampleRate / (kTempo / 60.0);
            const double samplesPerStep = samplesPerBeat / (double)kStepsPerBeat;

            for (int i = 0; i < positions.size(); ++i)
            {
                double expectedSample = (double)i * samplesPerStep;
                expectWithinAbsoluteError((double)positions[i], expectedSample, 2.0,
                    "Each step should be evenly spaced at samplesPerStep intervals");
            }
        }
    }
};

static PolymeterSwingTests polymeterSwingTests;
