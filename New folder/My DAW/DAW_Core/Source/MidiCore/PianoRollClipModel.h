#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include <memory>

namespace DAW
{

//==============================================================================
/**
    PianoRollClipModel
    ==================

    The single source of truth for all note + CC data in a MIDI clip.

    Architecture rules followed:
      - Pure data + edit operations. NO UI. NO playback logic. NO MIDI I/O.
      - Audio thread reads via lock-free immutable snapshot
        (matches PluginChainCore convention).
      - All mutations from the message thread go through this class
        so the CommandManager / undo system stays consistent.
      - Persists to ValueTree for ProjectManager save/load.

    Bubblegum integration:
      - Exposes a PlaybackListener interface. The future
        BubblegumMidiCableCore subscribes to it. Every time a note
        fires during playback, the cable renderer is notified and
        can emit a visual pulse traveling along the MIDI cable.
*/
class PianoRollClipModel : public juce::ChangeBroadcaster
{
public:
    //==========================================================================
    // DATA STRUCTS
    //==========================================================================

    /** A single MIDI note event living in the clip. */
    struct NoteEvent
    {
        juce::int64 id          = 0;     // Stable unique id for selection/edit
        juce::int64 startTick   = 0;     // PPQ-based start position
        juce::int64 lengthTicks = 0;     // Duration in ticks
        juce::uint8 pitch       = 60;    // 0-127, 60 = middle C
        juce::uint8 velocity    = 100;   // 0-127, attack strength
        juce::uint8 releaseVel  = 64;    // 0-127, release velocity
        juce::uint8 channel     = 0;     // 0-15
        bool        muted       = false;
        bool        selected    = false;

        // Per-note expressions (MPE / MIDI 2.0 — Bitwig-style)
        float microPitchSemis  = 0.0f;   // -1.0 to +1.0 (semitone bend offset)
        float microTimingTicks = 0.0f;   // micro-timing offset in ticks
        float microGainDb      = 0.0f;   // per-note dB offset
        float pressure         = 0.0f;   // 0.0 to 1.0 poly aftertouch initial
    };

    /** A single CC point on a CC lane. */
    struct CCEvent
    {
        juce::int64 startTick = 0;
        juce::uint8 channel   = 0;
        juce::uint8 ccNumber  = 1;       // 0-127 (1 = mod, 7 = volume, etc.)
        juce::uint8 value     = 0;
    };

    /** A pitch bend point. */
    struct PitchBendEvent
    {
        juce::int64 startTick = 0;
        juce::uint8 channel   = 0;
        int         value     = 0;       // -8192 to +8191
    };

    //==========================================================================
    // CONSTRUCTION
    //==========================================================================

    PianoRollClipModel();
    ~PianoRollClipModel() override = default;

    //==========================================================================
    // NOTE OPERATIONS  (message thread)
    //==========================================================================

    /** Adds a note. Returns the assigned id. */
    juce::int64 addNote (const NoteEvent& note);

    /** Removes a note by id. Returns true on success. */
    bool        removeNote (juce::int64 noteId);

    /** Replaces all fields of an existing note. */
    bool        modifyNote (juce::int64 noteId, const NoteEvent& newValues);

    /** Bulk move (drag): shifts a group of notes in time/pitch. */
    void        moveNotes (const juce::Array<juce::int64>& ids,
                           juce::int64 deltaTicks,
                           int deltaPitch);

    /** Bulk resize: extend or shorten a group of notes.
        If resizeStart=true, drags the start edge (changes both position + length). */
    void        resizeNotes (const juce::Array<juce::int64>& ids,
                             juce::int64 deltaTicks,
                             bool resizeStart);

    int  getNoteCount() const;
    const NoteEvent* findNote (juce::int64 id) const;

    juce::Array<NoteEvent> getNotesInRange (juce::int64 startTick,
                                            juce::int64 endTick) const;
    juce::Array<NoteEvent> getAllNotes() const;

    //==========================================================================
    // CC OPERATIONS  (message thread)
    //==========================================================================

    void addCCEvent (const CCEvent& cc);
    void removeCCEventsInRange (juce::uint8 ccNumber,
                                juce::int64 startTick,
                                juce::int64 endTick);
    juce::Array<CCEvent> getCCEvents (juce::uint8 ccNumber) const;

    void addPitchBendEvent (const PitchBendEvent& pb);
    juce::Array<PitchBendEvent> getPitchBendEvents() const;

    //==========================================================================
    // CLIP-LEVEL PROPERTIES
    //==========================================================================

    juce::int64 getLengthTicks() const            { return clipLengthTicks; }
    void        setLengthTicks (juce::int64 ticks);

    int  getPPQ() const                           { return ppq; }
    void setPPQ (int newPPQ);

    //==========================================================================
    // AUDIO-THREAD SAFE READ (lock-free snapshot pattern)
    //==========================================================================

    /** Immutable snapshot of clip data, safe to read from the audio thread. */
    struct Snapshot
    {
        std::shared_ptr<const std::vector<NoteEvent>>      notes;
        std::shared_ptr<const std::vector<CCEvent>>        ccEvents;
        std::shared_ptr<const std::vector<PitchBendEvent>> pitchBendEvents;
        juce::int64 lengthTicks = 0;
        int         ppq         = 960;
    };

    /** Lock-free, audio-thread safe.
        Audio thread calls this once per processBlock. */
    Snapshot getSnapshot() const;

    //==========================================================================
    // SERIALIZATION  (for ProjectManager XML save/load)
    //==========================================================================

    juce::ValueTree toValueTree() const;
    void            fromValueTree (const juce::ValueTree& tree);

    //==========================================================================
    // BUBBLEGUM HOOK — this is where MIDI cables tap in
    //==========================================================================

    /**
        BubblegumMidiCableCore (future) will implement this and subscribe.
        Every time a note fires during playback, all listeners are notified
        and the cable renderer emits a visual pulse traveling on the cable.
    */
    struct PlaybackListener
    {
        virtual ~PlaybackListener() = default;

        /** Called from the message thread (queued from audio thread)
            when PianoRollPlaybackCore detects a note has fired. */
        virtual void noteFired (const NoteEvent& note) = 0;
    };

    void addPlaybackListener    (PlaybackListener* l);
    void removePlaybackListener (PlaybackListener* l);

    /** Called by PianoRollPlaybackCore when a note fires.
        Forwards to all listeners (including the Bubblegum cable). */
    void notifyNoteFired (const NoteEvent& note);

    //==========================================================================
private:
    //==========================================================================

    std::atomic<juce::int64> nextNoteId { 1 };

    // Mutable state — message thread only
    std::vector<NoteEvent>      notes;
    std::vector<CCEvent>        ccEvents;
    std::vector<PitchBendEvent> pitchBendEvents;
    juce::int64 clipLengthTicks = 3840;   // 4 bars at 960 PPQ default
    int         ppq             = 960;

    // Lock-free snapshot — audio thread reads this
    // (use std::atomic_load / atomic_store with shared_ptr in C++20,
    //  or std::experimental::atomic_shared_ptr; for now we wrap
    //  via a simple atomic pointer swap pattern matching PluginChainCore.)
    mutable juce::CriticalSection snapshotLock;
    std::shared_ptr<Snapshot>     currentSnapshot;

    juce::ListenerList<PlaybackListener> playbackListeners;

    /** Rebuilds the snapshot after any mutation.
        Call at the end of every public mutator. */
    void rebuildSnapshot();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollClipModel)
};

} // namespace DAW
