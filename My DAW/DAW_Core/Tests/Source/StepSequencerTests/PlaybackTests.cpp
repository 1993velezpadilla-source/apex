#include <JuceHeader.h>
#include "../../Source/StepSequencerCore/StepSequencerPlaybackCore.h"
#include "../../Source/StepSequencerCore/StepSequencerTypes.h"

// At 120 BPM, stepsPerBeat=4: samplesPerStep = 44100 / (120/60) / 4 = 5512.5
// Pattern length with totalSteps=4: 4 * 5512.5 = 22050 samples.
// Use 10000 for single-step tests (covers flam/ratchet within one step).
// Use 22000 for multi-step tie tests (covers all 4 steps + NoteOffs, just under pattern repeat).
static constexpr int kSingleStepBlock = 10000;
static constexpr int kMultiStepBlock  = 22000;

class PlaybackTests final : public juce::UnitTest
{
public:
    PlaybackTests() : juce::UnitTest ("StepSequencer.Playback", "APEX.StepSequencer") {}

    static DAW::StepSequencerModel::Snapshot makeSnapshot (
        int totalSteps, int stepsPerBeat, int beatsPerBar,
        const juce::Array<DAW::StepEvent>& events)
    {
        DAW::StepSequencerModel::Snapshot snap;
        snap.totalSteps = totalSteps;
        snap.stepsPerBeat = stepsPerBeat;
        snap.beatsPerBar = beatsPerBar;
        snap.barsPerPattern = 1;
        snap.globalSwing = 0.0f;

        DAW::StepSequencerModel::Snapshot::LaneSnapshot lane;
        lane.volume = 1.0f;
        lane.pan = 0.0f;
        lane.muted = false;
        lane.soloed = false;
        lane.midiNote = 60;
        lane.midiChannel = 0;
        lane.swingAmount = 0.0f;
        lane.laneLength = 0;
        lane.steps = events;
        snap.lanes.add(lane);
        return snap;
    }

    static int countNoteOns (const juce::MidiBuffer& midi)
    {
        int n = 0;
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn() && metadata.getMessage().getVelocity() > 0)
                ++n;
        return n;
    }

    static int countNoteOffs (const juce::MidiBuffer& midi)
    {
        int n = 0;
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOff())
                ++n;
        return n;
    }

    void runTest() override
    {
        beginTest ("Basic playback produces NoteOn");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = (i == 0);
                e.velocity = 1.0f;
                e.ratchetCount = 1;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, kSingleStepBlock);
            juce::MidiBuffer midi;
            playback.processBlock (midi, kSingleStepBlock, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 1, "Single active step should produce exactly 1 NoteOn");
        }

        beginTest ("Ratchet expansion produces correct NoteOn count");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = (i == 0);
                e.velocity = 1.0f;
                e.ratchetCount = (i == 0) ? 3 : 1;
                e.ratchetSpacing = 0;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, kSingleStepBlock);
            juce::MidiBuffer midi;
            playback.processBlock (midi, kSingleStepBlock, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 3, "ratchetCount=3 should produce exactly 3 NoteOns");
        }

        beginTest ("Ratchet max bound caps at 16");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = (i == 0);
                e.velocity = 1.0f;
                e.ratchetCount = (i == 0) ? 20 : 1;
                e.ratchetSpacing = 0;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, kSingleStepBlock);
            juce::MidiBuffer midi;
            playback.processBlock (midi, kSingleStepBlock, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 16, "ratchetCount=20 should be capped to 16 NoteOns");
        }

        beginTest ("Flam adds extra note");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = (i == 0);
                e.velocity = 1.0f;
                e.ratchetCount = 1;
                e.flamEnabled = (i == 0);
                e.flamOffsetTicks = -40;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, kSingleStepBlock);
            juce::MidiBuffer midi;
            playback.processBlock (midi, kSingleStepBlock, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 2, "flamEnabled should produce 2 NoteOns (flam + main)");
        }

        beginTest ("Flam timing appears before main note");
        {
            // Step 1 at tick 1*(960/4)=240, sample ≈ 5512.
            // Flam offset = -40 ticks ≈ -919 samples. 5512-919=4593 > 0, fits in buffer.
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = (i == 1);
                e.velocity = 1.0f;
                e.ratchetCount = 1;
                e.flamEnabled = (i == 1);
                e.flamOffsetTicks = -40;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, kSingleStepBlock);
            juce::MidiBuffer midi;
            playback.processBlock (midi, kSingleStepBlock, 0, 120.0, snap);

            int firstNoteOnSample = -1;
            int secondNoteOnSample = -1;
            for (const auto metadata : midi)
            {
                if (metadata.getMessage().isNoteOn() && metadata.getMessage().getVelocity() > 0)
                {
                    if (firstNoteOnSample < 0)
                        firstNoteOnSample = metadata.samplePosition;
                    else
                        secondNoteOnSample = metadata.samplePosition;
                }
            }
            expect (firstNoteOnSample >= 0, "Should have first NoteOn");
            expect (secondNoteOnSample >= 0, "Should have second NoteOn");
            expect (firstNoteOnSample < secondNoteOnSample,
                   "Flam note should appear before main note (smaller sample offset)");
        }

        beginTest ("Tie suppresses NoteOff");
        {
            // Steps 0 tied, 1-3 not tied.
            // Step 3 NoteOff at sample 3*5512 + 0.9*5512 = 16537+4961 = 21498.
            // Block at 22000 samples covers all NoteOffs.
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.velocity = 1.0f;
                e.ratchetCount = 1;
                e.tieToNext = (i == 0);
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, kMultiStepBlock);
            juce::MidiBuffer midi;
            playback.processBlock (midi, kMultiStepBlock, 0, 120.0, snap);

            // Step 0 tied: NoteOn only (no NoteOff)
            // Steps 1-3 not tied: NoteOn + NoteOff each
            expectEquals (countNoteOns(midi), 4, "All 4 steps produce NoteOns");
            expectEquals (countNoteOffs(midi), 3, "Tied step suppresses its NoteOff; 3 NoteOffs expected");
        }

        beginTest ("Tie across multiple steps extends note");
        {
            // Steps 0-2 tied, step 3 not tied.
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.velocity = 1.0f;
                e.ratchetCount = 1;
                e.tieToNext = (i < 3);
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, kMultiStepBlock);
            juce::MidiBuffer midi;
            playback.processBlock (midi, kMultiStepBlock, 0, 120.0, snap);

            // Steps 0-2 tied: NoteOn only
            // Step 3 not tied: NoteOn + NoteOff
            expectEquals (countNoteOns(midi), 4, "All 4 steps active should produce 4 NoteOns");
            expectEquals (countNoteOffs(midi), 1, "Only the last untied step should produce a NoteOff");
        }
    }
};

static PlaybackTests playbackTests;
