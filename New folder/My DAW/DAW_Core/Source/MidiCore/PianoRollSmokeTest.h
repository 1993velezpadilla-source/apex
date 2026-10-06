#pragma once

#include <JuceHeader.h>
#include "MidiClip.h"
#include "MidiTrackCore.h"

namespace DAW
{

/**
    PianoRollSmokeTest
    ==================

    Quick smoke test to verify PianoRollClipModel + MidiClip work correctly.

    Usage:
    - Add a button or menu item in your UI that calls runSmokeTest()
    - Check the JUCE debug console for output
    - All tests should pass

    Expected output:
    ✓ Note count: 3
    ✓ Snapshot notes: 3
    ✓ Serialization round-trip succeeded
    ✓ Restored count: 3
    ✓ MidiClip integration test passed
    ✓ All smoke tests PASSED
*/
class PianoRollSmokeTest
{
public:
    static void runSmokeTest()
    {
        DBG("========================================");
        DBG("PIANO ROLL SMOKE TEST");
        DBG("========================================");

        testBasicNoteOperations();
        testSnapshot();
        testSerialization();
        testMidiClipIntegration();
        testMidiTrackIntegration();

        DBG("========================================");
        DBG("OK All smoke tests PASSED");
        DBG("========================================");
    }

private:
    static void testBasicNoteOperations()
    {
        DBG("\n--- Test: Basic Note Operations ---");

        PianoRollClipModel clip;

        // Add a C major chord at the start of bar 1
        using N = PianoRollClipModel::NoteEvent;

        N c4 = { .startTick = 0, .lengthTicks = 480, .pitch = 60, .velocity = 100 };
        N e4 = { .startTick = 0, .lengthTicks = 480, .pitch = 64, .velocity = 100 };
        N g4 = { .startTick = 0, .lengthTicks = 480, .pitch = 67, .velocity = 100 };

        auto id1 = clip.addNote(c4);
        auto id2 = clip.addNote(e4);
        auto id3 = clip.addNote(g4);

        jassert(clip.getNoteCount() == 3);
        DBG("OK Note count: " << clip.getNoteCount());

        // Test note lookup
        auto* found = clip.findNote(id2);
        jassert(found != nullptr);
        jassert(found->pitch == 64);

        // Test note removal
        clip.removeNote(id2);
        jassert(clip.getNoteCount() == 2);

        // Re-add for subsequent tests
        clip.addNote(e4);
    }

    static void testSnapshot()
    {
        DBG("\n--- Test: Audio-Thread Safe Snapshot ---");

        PianoRollClipModel clip;

        using N = PianoRollClipModel::NoteEvent;
        clip.addNote({ .startTick = 0, .lengthTicks = 480, .pitch = 60, .velocity = 100 });
        clip.addNote({ .startTick = 0, .lengthTicks = 480, .pitch = 64, .velocity = 100 });
        clip.addNote({ .startTick = 0, .lengthTicks = 480, .pitch = 67, .velocity = 100 });

        // Get snapshot (audio-thread safe)
        auto snap = clip.getSnapshot();
        jassert(snap.notes != nullptr);
        jassert(snap.notes->size() == 3);
        jassert(snap.ppq == 960);
        jassert(snap.lengthTicks == 3840);

        DBG("OK Snapshot notes: " << (int)snap.notes->size());
        DBG("OK Snapshot PPQ: " << snap.ppq);
        DBG("OK Snapshot length: " << snap.lengthTicks << " ticks");
    }

    static void testSerialization()
    {
        DBG("\n--- Test: Serialization Round-Trip ---");

        PianoRollClipModel original;

        // Add test data
        using N = PianoRollClipModel::NoteEvent;
        original.addNote({ .startTick = 0,    .lengthTicks = 480, .pitch = 60, .velocity = 100 });
        original.addNote({ .startTick = 480,  .lengthTicks = 480, .pitch = 62, .velocity = 90 });
        original.addNote({ .startTick = 960,  .lengthTicks = 480, .pitch = 64, .velocity = 80 });
        original.setLengthTicks(1920);
        original.setPPQ(960);

        // Serialize
        auto tree = original.toValueTree();
        DBG("Serialized XML:\n" << tree.toXmlString().substring(0, 300) << "...");

        // Deserialize
        PianoRollClipModel restored;
        restored.fromValueTree(tree);

        // Verify
        jassert(restored.getNoteCount() == 3);
        jassert(restored.getLengthTicks() == 1920);
        jassert(restored.getPPQ() == 960);

        auto notes = restored.getAllNotes();
        jassert(notes[0].pitch == 60);
        jassert(notes[1].pitch == 62);
        jassert(notes[2].pitch == 64);

        DBG("OK Serialization round-trip succeeded");
        DBG("OK Restored count: " << restored.getNoteCount());
    }

    static void testMidiClipIntegration()
    {
        DBG("\n--- Test: MidiClip Integration ---");

        auto id = juce::Uuid().toString();
        MidiClip midiClip(id, "Test MIDI Clip");

        // Add notes via MidiClip's piano roll model
        auto& model = midiClip.getPianoRollModel();
        using N = PianoRollClipModel::NoteEvent;

        model.addNote({ .startTick = 0,   .lengthTicks = 480, .pitch = 60, .velocity = 100 });
        model.addNote({ .startTick = 480, .lengthTicks = 480, .pitch = 64, .velocity = 100 });

        jassert(model.getNoteCount() == 2);

        // Test sample↔tick conversion
        midiClip.setTempo(120.0);
        midiClip.setSampleRate(44100.0);

        auto ticks = midiClip.samplesToTicks(88200);  // ~2 seconds at 44.1kHz
        DBG("OK 88200 samples = " << ticks << " ticks at 120 BPM");

        auto samples = midiClip.ticksToSamples(960);  // 1 beat at 960 PPQ
        DBG("OK 960 ticks = " << samples << " samples at 120 BPM");

        // Test serialization
        auto state = midiClip.getState();
        MidiClip restored(juce::Uuid().toString(), "Restored");
        restored.restoreState(state);

        jassert(restored.getPianoRollModel().getNoteCount() == 2);
        jassert(restored.getTempo() == 120.0);

        DBG("OK MidiClip integration test passed");
    }

    static void testMidiTrackIntegration()
    {
        DBG("\n--- Test: MidiTrackCore Integration ---");

        auto id = juce::Uuid().toString();
        MidiTrackCore midiTrack(id, "Test MIDI Track");

        jassert(midiTrack.getRole() == TrackRole::MIDI);

        midiTrack.setMidiInputDevice("My Keyboard");
        midiTrack.setMidiInputChannel(10);
        midiTrack.setTransposeSemitones(12);
        midiTrack.setMpeEnabled(true);

        jassert(midiTrack.getMidiInputDevice() == "My Keyboard");
        jassert(midiTrack.getMidiInputChannel() == 10);
        jassert(midiTrack.getTransposeSemitones() == 12);
        jassert(midiTrack.isMpeEnabled());

        auto state = midiTrack.getState();
        MidiTrackCore restored(juce::Uuid().toString(), "Restored MIDI Track");
        restored.restoreState(state);

        jassert(restored.getRole() == TrackRole::MIDI);
        jassert(restored.getMidiInputDevice() == "My Keyboard");
        jassert(restored.getMidiInputChannel() == 10);
        jassert(restored.getTransposeSemitones() == 12);
        jassert(restored.isMpeEnabled());

        DBG("OK MidiTrackCore integration test passed");
    }
};

} // namespace DAW
