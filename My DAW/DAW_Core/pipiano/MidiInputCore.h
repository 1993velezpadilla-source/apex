#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "../piano_roll/MidiTrackCore.h"

namespace xziel::midi
{

//==============================================================================
/**
    MidiInputCore
    =============

    Bridges incoming MIDI events (from physical keyboards, virtual touch
    keyboards, or any MidiInputCallback source) into the DAW.

    Responsibilities:
      - Receive MIDI events on the MIDI input thread (via JUCE's
        MidiInputCallback or pushFromVirtualKeyboard).
      - Buffer them in a lock-free FIFO so the audio thread can pull them
        with sample-accurate timestamping into the live process block.
      - Optionally write them into the armed track's active clip during
        recording (live capture).
      - Emit a "live note fired" event so the Bubblegum cable can also
        pulse for live-played notes (not just sequenced ones).

    Threading model (the canonical 3-thread pattern):

        MIDI input thread   ─push──▶  Lock-free FIFO  ─pull──▶  Audio thread
                                              │
                                              └─pull (delayed)─▶  Message thread
                                                                  (UI feedback,
                                                                   recording)

    JUCE's MidiMessageCollector handles the FIFO + timestamping. We wrap it
    here with track routing, recording capture, and Bubblegum hooks.
*/
class MidiInputCore : public juce::MidiInputCallback,
                     public juce::Timer
{
public:
    //==========================================================================
    MidiInputCore();
    ~MidiInputCore() override;

    //==========================================================================
    // PREPARE  (call when sample rate / buffer size changes)
    //==========================================================================

    void prepare (double sampleRate, int blockSize);

    //==========================================================================
    // DEVICE MANAGEMENT
    //==========================================================================

    juce::StringArray getAvailableInputDeviceNames() const;

    /** Opens an input device by name. Empty string = "all inputs combined". */
    bool openDevice  (const juce::String& deviceName);
    void closeDevice (const juce::String& deviceName);
    void closeAllDevices();

    //==========================================================================
    // VIRTUAL INPUT  (touchscreen / on-screen keyboard / gamepad)
    //==========================================================================

    /** Push a MIDI message from a virtual source. Same effect as a real
        device — message gets timestamped and routed through. */
    void pushFromVirtualKeyboard (const juce::MidiMessage& msg);

    /** Convenience for the virtual keyboard: send note-on/note-off
        without constructing a MidiMessage. */
    void virtualNoteOn  (int midiChannel1based, int pitch, float velocity);
    void virtualNoteOff (int midiChannel1based, int pitch);

    //==========================================================================
    // ROUTING  (which tracks receive incoming MIDI)
    //==========================================================================

    /** When the host calls fillBlockBuffer for an armed track, the input
        events for that track's input filter are written into outBuffer.

        The host typically calls this RIGHT BEFORE PianoRollPlaybackCore::
        processBlock so live notes are merged with sequenced notes. */
    void fillBlockBuffer (MidiTrackCore& track,
                          juce::MidiBuffer& outBuffer,
                          int numSamples);

    //==========================================================================
    // RECORDING  (capture live MIDI into clips)
    //==========================================================================

    /** When recording is active and the track is armed, incoming MIDI
        events get written into a target clip's PianoRollClipModel.
        Set the target before transport enters record. */
    void setRecordingTarget (MidiTrackCore* track, MidiClipCore* clip);
    void clearRecordingTarget();

    /** Called by the transport listener when recording starts/stops. */
    void onRecordingStateChanged (bool isNowRecording);

    /** Tempo + PPQ for converting incoming sample timestamps to ticks
        (so notes get written at musically-correct positions in the clip). */
    void setTempo (double bpm);
    void setPPQ   (int ppq);

    /** Project transport position at the moment recording started.
        Used as the time origin for incoming events. */
    void setRecordOriginSamples (juce::int64 samples);

    //==========================================================================
    // LIVE PLAYBACK HOOK  (Bubblegum cable pulses for live-played notes)
    //==========================================================================

    struct LiveNoteListener
    {
        virtual ~LiveNoteListener() = default;
        virtual void liveNotePlayed (int midiChannel,
                                     int pitch,
                                     float velocity,
                                     juce::int64 timestampSamples) = 0;
    };

    void addLiveNoteListener    (LiveNoteListener* l) { liveListeners.add (l); }
    void removeLiveNoteListener (LiveNoteListener* l) { liveListeners.remove (l); }

    //==========================================================================
    // MidiInputCallback
    //==========================================================================

    void handleIncomingMidiMessage (juce::MidiInput* source,
                                    const juce::MidiMessage& message) override;

    //==========================================================================
    // Timer  (drains "delayed" events to message thread for UI/recording)
    //==========================================================================

    void timerCallback() override;

    //==========================================================================
private:
    //==========================================================================

    double sampleRate    = 44100.0;
    int    currentBlock  = 256;
    double tempoBpm      = 120.0;
    int    ppq           = 960;
    juce::int64 recordOriginSamples = 0;

    // The lock-free MIDI FIFO. JUCE provides this exact pattern.
    juce::MidiMessageCollector messageCollector;

    // Open input devices
    juce::OwnedArray<juce::MidiInput> openDevices;

    // Recording state
    std::atomic<bool>          isRecording { false };
    MidiTrackCore*             recordTrack = nullptr;
    MidiClipCore*              recordClip  = nullptr;
    juce::CriticalSection      recordLock;

    // Pending notes in flight  — note-on without matching note-off yet.
    // Used to compute lengthTicks at note-off time during recording.
    struct PendingNote
    {
        juce::int64 startSamples = 0;
        juce::uint8 pitch        = 0;
        juce::uint8 velocity     = 0;
        juce::uint8 channel      = 0;
    };
    juce::Array<PendingNote> pendingRecord;

    // Buffer for events arriving since last UI/record drain
    juce::MidiBuffer recentEventsForRecord;
    juce::CriticalSection recentLock;

    juce::ListenerList<LiveNoteListener> liveListeners;

    juce::int64 samplesToTicks (juce::int64 samples) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputCore)
};

} // namespace xziel::midi
