#include <JuceHeader.h>
#include "../../Source/StepSequencerCore/StepSequencerPlaybackCore.h"
#include "../../Source/StepSequencerCore/StepSequencerTypes.h"

// At 120 BPM, stepsPerBeat=4: samplesPerStep = 44100 / (120/60) / 4 = 5512.5
// 4 steps = 22050 samples, 8 steps = 44100, 16 steps = 88200, 100 steps = 551250

class DeterministicTests final : public juce::UnitTest
{
public:
    DeterministicTests() : juce::UnitTest ("StepSequencer.Deterministic", "APEX.StepSequencer") {}

    static DAW::StepSequencerModel::Snapshot makeSnapshot (
        int totalSteps, int stepsPerBeat, int beatsPerBar,
        const juce::Array<DAW::StepEvent>& events, uint32_t patternHash = 42)
    {
        DAW::StepSequencerModel::Snapshot snap;
        snap.totalSteps = totalSteps;
        snap.stepsPerBeat = stepsPerBeat;
        snap.beatsPerBar = beatsPerBar;
        snap.barsPerPattern = 1;
        snap.globalSwing = 0.0f;
        snap.patternId_hash = patternHash;

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

    void runTest() override
    {
        beginTest ("Deterministic probability — same seed produces same roll");
        {
            uint32_t hash1 = 12345;
            uint32_t hash2 = 12345;
            uint32_t seed1a = DAW::StepSequencerPlaybackCore::deterministicSeed(hash1, 0, 5, 0);
            uint32_t seed1b = DAW::StepSequencerPlaybackCore::deterministicSeed(hash2, 0, 5, 0);
            expect (seed1a == seed1b, "Same inputs must produce same seed");
        }

        beginTest ("Deterministic probability — different seeds produce different rolls");
        {
            uint32_t seed1 = DAW::StepSequencerPlaybackCore::deterministicSeed(111, 0, 5, 0);
            uint32_t seed2 = DAW::StepSequencerPlaybackCore::deterministicSeed(222, 0, 5, 0);
            uint32_t seed3 = DAW::StepSequencerPlaybackCore::deterministicSeed(111, 1, 5, 0);
            uint32_t seed4 = DAW::StepSequencerPlaybackCore::deterministicSeed(111, 0, 6, 0);
            uint32_t seed5 = DAW::StepSequencerPlaybackCore::deterministicSeed(111, 0, 5, 1);
            expect (seed1 != seed2 || seed1 != seed3 || seed1 != seed4 || seed1 != seed5,
                    "Different inputs should produce different seeds (at least one pair)");
        }

        beginTest ("Deterministic across block sizes — same position gives same result");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.probability = 0.5f;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback1;
            playback1.prepareToPlay (44100.0, 512);
            juce::MidiBuffer midi1;
            playback1.processBlock (midi1, 512, 0, 120.0, snap);

            DAW::StepSequencerPlaybackCore playback2;
            playback2.prepareToPlay (44100.0, 1024);
            juce::MidiBuffer midi2;
            playback2.processBlock (midi2, 1024, 0, 120.0, snap);

            DAW::StepSequencerPlaybackCore playback3;
            playback3.prepareToPlay (44100.0, 256);
            juce::MidiBuffer midi3;
            playback3.processBlock (midi3, 256, 0, 120.0, snap);

            const int count1 = countNoteOns(midi1);
            const int count2 = countNoteOns(midi2);
            const int count3 = countNoteOns(midi3);
            expect (count1 == count2, "Block size 512 vs 1024 must produce same note count");
            expect (count1 == count3, "Block size 512 vs 256 must produce same note count");
        }

        beginTest ("Condition Every — always passes");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::Every;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, 22050);
            juce::MidiBuffer midi;
            playback.processBlock (midi, 22050, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 4, "Every condition should pass all 4 steps");
        }

        beginTest ("Condition Fill — only last beat passes");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 16; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::Fill;
                events.add(e);
            }
            auto snap = makeSnapshot (16, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, 88200);
            juce::MidiBuffer midi;
            playback.processBlock (midi, 88200, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 4, "Fill should only trigger last 4 steps (stepsPerBeat=4)");
        }

        beginTest ("Condition NotFill — only non-last-beat passes");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 16; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::NotFill;
                events.add(e);
            }
            auto snap = makeSnapshot (16, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, 88200);
            juce::MidiBuffer midi;
            playback.processBlock (midi, 88200, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 12, "NotFill should trigger first 12 steps (16 - 4)");
        }

        beginTest ("Condition PrevActive — true when previous step was active");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::PrevActive;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, 22050);
            juce::MidiBuffer midi;
            playback.processBlock (midi, 22050, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 3, "PrevActive: step 0 fails (no prev), steps 1-3 pass");
        }

        beginTest ("Condition Random4 — approximately 25% pass rate");
        {
            const int numSteps = 32;
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < numSteps; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::Random4;
                events.add(e);
            }
            // 32 steps * 5512.5 samples/step = 176400 samples needed
            auto snap = makeSnapshot (numSteps, 4, 8, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, 180000);
            juce::MidiBuffer midi;
            playback.processBlock (midi, 180000, 0, 120.0, snap);

            const int count = countNoteOns(midi);
            expect (count >= 5 && count <= 13,
                    "Random4 should be ~25% of " + juce::String(numSteps)
                    + " (got " + juce::String(count) + ")");
        }

        beginTest ("Condition First/Last — boundary conditions");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 8; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::First;
                events.add(e);
            }
            auto snapFirst = makeSnapshot (8, 4, 2, events);

            DAW::StepSequencerPlaybackCore playback1;
            playback1.prepareToPlay (44100.0, 44100);
            juce::MidiBuffer midi1;
            playback1.processBlock (midi1, 44100, 0, 120.0, snapFirst);

            expectEquals (countNoteOns(midi1), 1, "First condition should only trigger step 0");

            for (auto& e : events)
                e.condition = DAW::StepEvent::Condition::Last;
            auto snapLast = makeSnapshot (8, 4, 2, events);

            DAW::StepSequencerPlaybackCore playback2;
            playback2.prepareToPlay (44100.0, 44100);
            juce::MidiBuffer midi2;
            playback2.processBlock (midi2, 44100, 0, 120.0, snapLast);

            expectEquals (countNoteOns(midi2), 1, "Last condition should only trigger step 7");
        }

        beginTest ("Condition FirstOf — triggers at beat boundaries");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 16; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::FirstOf;
                events.add(e);
            }
            auto snap = makeSnapshot (16, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, 88200);
            juce::MidiBuffer midi;
            playback.processBlock (midi, 88200, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 4, "FirstOf should trigger steps 0, 4, 8, 12");
        }

        beginTest ("Condition PrevInactive — true when previous step was inactive");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::PrevInactive;
                events.add(e);
            }
            auto snap = makeSnapshot (4, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, 22050);
            juce::MidiBuffer midi;
            playback.processBlock (midi, 22050, 0, 120.0, snap);

            expectEquals (countNoteOns(midi), 1, "PrevInactive: step 0 passes (no prev = inactive), steps 1-3 fail");
        }

        beginTest ("Condition NotFirst/NotSecond/NotThird — counting active steps");
        {
            // NotFirst: step 0 fails, steps 1-3 pass = 3 notes
            {
                juce::Array<DAW::StepEvent> events;
                for (int i = 0; i < 4; ++i)
                {
                    DAW::StepEvent e;
                    e.active = true;
                    e.condition = DAW::StepEvent::Condition::NotFirst;
                    events.add(e);
                }
                auto snap = makeSnapshot (4, 4, 4, events);
                DAW::StepSequencerPlaybackCore playback;
                playback.prepareToPlay (44100.0, 22050);
                juce::MidiBuffer midi;
                playback.processBlock (midi, 22050, 0, 120.0, snap);
                expectEquals (countNoteOns(midi), 3, "NotFirst: step 0 fails, 1-3 pass");
            }

            // NotSecond: step 0 passes, step 1 fails, steps 2-3 pass = 3 notes
            {
                juce::Array<DAW::StepEvent> events;
                for (int i = 0; i < 4; ++i)
                {
                    DAW::StepEvent e;
                    e.active = true;
                    e.condition = DAW::StepEvent::Condition::NotSecond;
                    events.add(e);
                }
                auto snap = makeSnapshot (4, 4, 4, events);
                DAW::StepSequencerPlaybackCore playback;
                playback.prepareToPlay (44100.0, 22050);
                juce::MidiBuffer midi;
                playback.processBlock (midi, 22050, 0, 120.0, snap);
                expectEquals (countNoteOns(midi), 3, "NotSecond: step 1 fails, 0,2,3 pass");
            }

            // NotThird: steps 0,1 pass, step 2 fails, step 3 passes = 3 notes
            {
                juce::Array<DAW::StepEvent> events;
                for (int i = 0; i < 4; ++i)
                {
                    DAW::StepEvent e;
                    e.active = true;
                    e.condition = DAW::StepEvent::Condition::NotThird;
                    events.add(e);
                }
                auto snap = makeSnapshot (4, 4, 4, events);
                DAW::StepSequencerPlaybackCore playback;
                playback.prepareToPlay (44100.0, 22050);
                juce::MidiBuffer midi;
                playback.processBlock (midi, 22050, 0, 120.0, snap);
                expectEquals (countNoteOns(midi), 3, "NotThird: step 2 fails, 0,1,3 pass");
            }
        }

        beginTest ("Condition probability interaction — both must pass");
        {
            juce::Array<DAW::StepEvent> events;
            for (int i = 0; i < 4; ++i)
            {
                DAW::StepEvent e;
                e.active = true;
                e.condition = DAW::StepEvent::Condition::Fill;
                e.probability = 0.5f;
                events.add(e);
            }
            auto snap = makeSnapshot (16, 4, 4, events);

            DAW::StepSequencerPlaybackCore playback;
            playback.prepareToPlay (44100.0, 88200);
            juce::MidiBuffer midi;
            playback.processBlock (midi, 88200, 0, 120.0, snap);

            const int count = countNoteOns(midi);
            expect (count <= 4, "Fill limits to 4 steps max, probability may reduce further");
            expect (count >= 0, "Count should be non-negative");
        }
    }
};

static DeterministicTests deterministicTests;
