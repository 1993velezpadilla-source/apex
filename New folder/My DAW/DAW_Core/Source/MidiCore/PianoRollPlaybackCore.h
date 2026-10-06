#pragma once

#include <JuceHeader.h>
#include "PianoRollClipModel.h"

namespace DAW {

class MidiClip;
class ClipManager;
class TrackManager;

//==============================================================================
/**
    PianoRollPlaybackCore
    =====================

    Audio-thread-safe MIDI playback engine for piano roll clips.

    - Reads lock-free snapshots from PianoRollClipModel.
    - Per block, finds notes starting in the block and emits sample-accurate
      note-on events into a MidiBuffer.
    - Tracks active notes and emits note-off when their length elapses.
    - Calls PianoRollClipModel::notifyNoteFired() (dispatched to message thread)
      for Bubblegum cable integration.

    Threading:
    - processBlock() runs on the audio thread — all reads are lock-free.
    - allNotesOff() may be called from either thread under activeNotesLock_.
*/
class PianoRollPlaybackCore
{
public:
    PianoRollPlaybackCore();
    ~PianoRollPlaybackCore();

    //==========================================================================
    void prepare (double sampleRate, int maxBlockSize);
    void setTempo (double bpm);
    void setTimeSignature (int numerator, int denominator);

    void setSubsystems (ClipManager* clips, TrackManager* tracks) noexcept
    {
        clips_  = clips;
        tracks_ = tracks;
    }

    //==========================================================================
    struct TransportInfo
    {
        bool        isPlaying          = false;
        juce::int64 timelinePosSamples = 0;
        int         numSamples         = 0;
    };

    /**
        Process one block for one MIDI track (identified by its trackId).
        Populates midiOut with sample-accurate note events.
        Audio-thread safe, lock-free for snapshot reads.
    */
    void processBlock (const juce::String& trackId,
                       const TransportInfo& transport,
                       juce::MidiBuffer& midiOut);

    /** All notes off for a specific track (panic / stop). */
    void allNotesOff (const juce::String& trackId, juce::MidiBuffer& midiOut);

    /** All notes off for every active track (global panic). */
    void allNotesOffGlobal (juce::MidiBuffer& midiOut);

    //==========================================================================
private:
    ClipManager*  clips_  = nullptr;
    TrackManager* tracks_ = nullptr;

    double sampleRate_   = 44100.0;
    double tempoBpm_     = 120.0;
    int    sigNumerator_  = 4;
    int    sigDenominator_= 4;

    juce::int64 samplesToTicks (juce::int64 samples, int ppq) const noexcept;
    juce::int64 ticksToSamples (juce::int64 ticks,   int ppq) const noexcept;

    struct ActiveNote
    {
        juce::int64 noteId       = 0;
        juce::int64 endSamples   = 0;   // global timeline sample at which to fire note-off
        juce::uint8 pitch        = 0;
        juce::uint8 channel      = 0;
    };

    // Per-track active note lists; keyed by trackId string
    std::unordered_map<juce::String, std::vector<ActiveNote>> activeNotes_;
    juce::CriticalSection activeNotesLock_;

    //--------------------------------------------------------------------------
    // Dispatches notifyNoteFired() calls from audio thread → message thread
    class NoteFiredDispatcher : public juce::AsyncUpdater
    {
    public:
        void enqueue (PianoRollClipModel* model,
                      const PianoRollClipModel::NoteEvent& note);
        void handleAsyncUpdate() override;
    private:
        struct PendingFire { PianoRollClipModel* model = nullptr; PianoRollClipModel::NoteEvent note; };
        juce::CriticalSection lock_;
        juce::Array<PendingFire> queued_;
    } dispatcher_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollPlaybackCore)
};

} // namespace DAW
