#include "MidiInputCore.h"

namespace xziel::midi
{

//==============================================================================
MidiInputCore::MidiInputCore()
{
    startTimerHz (30); // drain UI/record events at 30 fps
}

MidiInputCore::~MidiInputCore()
{
    stopTimer();
    closeAllDevices();
}

//==============================================================================
// PREPARE
//==============================================================================

void MidiInputCore::prepare (double newSampleRate, int blockSize)
{
    sampleRate   = juce::jmax (1.0, newSampleRate);
    currentBlock = juce::jmax (1, blockSize);
    messageCollector.reset (sampleRate);
}

//==============================================================================
// DEVICES
//==============================================================================

juce::StringArray MidiInputCore::getAvailableInputDeviceNames() const
{
    juce::StringArray names;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        names.add (d.name);
    return names;
}

bool MidiInputCore::openDevice (const juce::String& deviceName)
{
    auto devices = juce::MidiInput::getAvailableDevices();

    for (auto& d : devices)
    {
        if (d.name == deviceName || deviceName.isEmpty())
        {
            auto input = juce::MidiInput::openDevice (d.identifier, this);
            if (input != nullptr)
            {
                input->start();
                openDevices.add (std::move (input));
                if (! deviceName.isEmpty()) return true;
            }
        }
    }
    return ! openDevices.isEmpty();
}

void MidiInputCore::closeDevice (const juce::String& deviceName)
{
    for (int i = openDevices.size(); --i >= 0;)
    {
        if (openDevices[i]->getName() == deviceName)
        {
            openDevices[i]->stop();
            openDevices.remove (i);
        }
    }
}

void MidiInputCore::closeAllDevices()
{
    for (auto* d : openDevices) d->stop();
    openDevices.clear();
}

//==============================================================================
// VIRTUAL INPUT
//==============================================================================

void MidiInputCore::pushFromVirtualKeyboard (const juce::MidiMessage& msg)
{
    handleIncomingMidiMessage (nullptr, msg);
}

void MidiInputCore::virtualNoteOn (int channel1based, int pitch, float velocity)
{
    auto msg = juce::MidiMessage::noteOn (
        juce::jlimit (1, 16, channel1based),
        juce::jlimit (0, 127, pitch),
        velocity);
    msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    pushFromVirtualKeyboard (msg);
}

void MidiInputCore::virtualNoteOff (int channel1based, int pitch)
{
    auto msg = juce::MidiMessage::noteOff (
        juce::jlimit (1, 16, channel1based),
        juce::jlimit (0, 127, pitch));
    msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    pushFromVirtualKeyboard (msg);
}

//==============================================================================
// MidiInputCallback (called on MIDI input thread)
//==============================================================================

void MidiInputCore::handleIncomingMidiMessage (juce::MidiInput* /*source*/,
                                               const juce::MidiMessage& message)
{
    // Push into the lock-free FIFO. Audio thread will drain it via fillBlockBuffer().
    messageCollector.addMessageToQueue (message);

    // Also push into the recent-events buffer for UI / recording drain.
    {
        const juce::ScopedLock sl (recentLock);
        const auto sampleStamp = (juce::int64)
            (message.getTimeStamp() * sampleRate);
        recentEventsForRecord.addEvent (message, (int) (sampleStamp & 0x7FFFFFFF));
    }
}

//==============================================================================
// fillBlockBuffer — called by AudioEngine BEFORE PianoRollPlaybackCore::processBlock
//==============================================================================

void MidiInputCore::fillBlockBuffer (MidiTrackCore& track,
                                     juce::MidiBuffer& outBuffer,
                                     int numSamples)
{
    if (! track.isArmed()) return;

    // Pull all queued events for this block from the lock-free FIFO.
    juce::MidiBuffer raw;
    messageCollector.removeNextBlockOfMessages (raw, numSamples);

    // Filter by track's input channel
    const int filter = track.getInputChannelFilter(); // -1 = omni

    for (const auto meta : raw)
    {
        const auto& msg = meta.getMessage();
        const int  ch   = msg.getChannel(); // 1-based, 0 if not channel msg
        if (filter != -1 && ch != 0 && (ch - 1) != filter) continue;

        outBuffer.addEvent (msg, meta.samplePosition);

        // Notify Bubblegum: live note played
        if (msg.isNoteOn())
        {
            const auto channel  = msg.getChannel();
            const auto pitch    = msg.getNoteNumber();
            const auto velocity = msg.getFloatVelocity();
            const auto stamp    = recordOriginSamples + meta.samplePosition;
            liveListeners.call ([&] (LiveNoteListener& l)
                                { l.liveNotePlayed (channel, pitch, velocity, stamp); });
        }
    }
}

//==============================================================================
// RECORDING
//==============================================================================

void MidiInputCore::setRecordingTarget (MidiTrackCore* track, MidiClipCore* clip)
{
    const juce::ScopedLock sl (recordLock);
    recordTrack = track;
    recordClip  = clip;
    pendingRecord.clearQuick();
}

void MidiInputCore::clearRecordingTarget()
{
    setRecordingTarget (nullptr, nullptr);
}

void MidiInputCore::onRecordingStateChanged (bool isNowRecording)
{
    isRecording.store (isNowRecording);

    if (! isNowRecording)
    {
        // Close any pending notes that didn't get a note-off
        const juce::ScopedLock sl (recordLock);
        if (recordClip != nullptr)
        {
            for (auto& p : pendingRecord)
            {
                PianoRollClipModel::NoteEvent n;
                n.startTick   = samplesToTicks (p.startSamples - recordOriginSamples);
                n.lengthTicks = juce::jmax<juce::int64> (1,
                                  samplesToTicks ((juce::int64) sampleRate / 4)); // 250ms tail
                n.pitch       = p.pitch;
                n.velocity    = p.velocity;
                n.channel     = p.channel;
                recordClip->getModel().addNote (n);
            }
        }
        pendingRecord.clearQuick();
    }
}

void MidiInputCore::setTempo (double bpm)        { tempoBpm = juce::jmax (1.0, bpm); }
void MidiInputCore::setPPQ   (int newPPQ)        { ppq = juce::jmax (1, newPPQ); }
void MidiInputCore::setRecordOriginSamples (juce::int64 s) { recordOriginSamples = s; }

//==============================================================================
// Timer  (writes recorded events into the clip on the message thread)
//==============================================================================

void MidiInputCore::timerCallback()
{
    if (! isRecording.load()) return;

    juce::MidiBuffer drained;
    {
        const juce::ScopedLock sl (recentLock);
        drained.swapWith (recentEventsForRecord);
    }
    if (drained.isEmpty()) return;

    const juce::ScopedLock sl (recordLock);
    if (recordClip == nullptr) return;

    auto& model = recordClip->getModel();

    for (const auto meta : drained)
    {
        const auto& msg = meta.getMessage();
        const auto sampleStamp = (juce::int64) meta.samplePosition; // already in samples

        if (msg.isNoteOn())
        {
            PendingNote pn;
            pn.startSamples = sampleStamp;
            pn.pitch        = (juce::uint8) msg.getNoteNumber();
            pn.velocity     = (juce::uint8) msg.getVelocity();
            pn.channel      = (juce::uint8) (msg.getChannel() - 1);
            pendingRecord.add (pn);
        }
        else if (msg.isNoteOff())
        {
            const int pitch = msg.getNoteNumber();
            for (int i = pendingRecord.size(); --i >= 0;)
            {
                if (pendingRecord[i].pitch == pitch)
                {
                    auto p = pendingRecord[i];
                    pendingRecord.remove (i);

                    PianoRollClipModel::NoteEvent n;
                    n.startTick   = samplesToTicks (p.startSamples - recordOriginSamples);
                    n.lengthTicks = juce::jmax<juce::int64> (1,
                                      samplesToTicks (sampleStamp - p.startSamples));
                    n.pitch       = p.pitch;
                    n.velocity    = p.velocity;
                    n.channel     = p.channel;
                    model.addNote (n);
                    break;
                }
            }
        }
    }
}

//==============================================================================
// TIME CONVERSION
//==============================================================================

juce::int64 MidiInputCore::samplesToTicks (juce::int64 samples) const noexcept
{
    const double seconds = (double) samples / sampleRate;
    const double beats   = seconds * (tempoBpm / 60.0);
    return (juce::int64) (beats * (double) ppq);
}

} // namespace xziel::midi
