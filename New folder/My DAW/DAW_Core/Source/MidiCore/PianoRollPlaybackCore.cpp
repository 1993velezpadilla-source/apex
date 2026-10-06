#include "PianoRollPlaybackCore.h"
#include "../ClipCore/Clip.h"
#include "../MidiCore/MidiClip.h"
#include "../TrackCore/Track.h"

namespace DAW {

//==============================================================================
PianoRollPlaybackCore::PianoRollPlaybackCore()  = default;
PianoRollPlaybackCore::~PianoRollPlaybackCore() = default;

void PianoRollPlaybackCore::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = juce::jmax (1.0, sampleRate);
}

void PianoRollPlaybackCore::setTempo (double bpm)
{
    tempoBpm_ = juce::jmax (1.0, bpm);
}

void PianoRollPlaybackCore::setTimeSignature (int n, int d)
{
    sigNumerator_  = juce::jmax (1, n);
    sigDenominator_= juce::jmax (1, d);
}

//==============================================================================
juce::int64 PianoRollPlaybackCore::samplesToTicks (juce::int64 samples, int ppq) const noexcept
{
    const double seconds = (double) samples / sampleRate_;
    const double beats   = seconds * (tempoBpm_ / 60.0);
    return (juce::int64) (beats * (double) ppq);
}

juce::int64 PianoRollPlaybackCore::ticksToSamples (juce::int64 ticks, int ppq) const noexcept
{
    const double beats   = (double) ticks / (double) ppq;
    const double seconds = beats * (60.0 / tempoBpm_);
    return (juce::int64) (seconds * sampleRate_);
}

//==============================================================================
void PianoRollPlaybackCore::processBlock (const juce::String& trackId,
                                          const TransportInfo& transport,
                                          juce::MidiBuffer& midiOut)
{
    if (transport.numSamples <= 0 || clips_ == nullptr || tracks_ == nullptr)
        return;

    // Check if this track is muted
    if (auto* track = tracks_->getTrack(trackId))
        if (track->isMuted())
        {
            allNotesOff (trackId, midiOut);
            return;
        }

    const juce::int64 blockStart = transport.timelinePosSamples;
    const juce::int64 blockEnd   = blockStart + (juce::int64) transport.numSamples;

    // Phase 1: fire note-OFFs for active notes whose end falls in this block.
    {
        const juce::ScopedLock sl (activeNotesLock_);
        auto& active = activeNotes_[trackId];

        for (int i = (int) active.size(); --i >= 0;)
        {
            auto& n = active[(size_t) i];
            if (n.endSamples >= blockStart && n.endSamples < blockEnd)
            {
                const int offset = (int) (n.endSamples - blockStart);
                midiOut.addEvent (juce::MidiMessage::noteOff (n.channel + 1, n.pitch), offset);
                active.erase (active.begin() + i);
            }
        }
    }

    if (! transport.isPlaying)
        return;

    // Phase 2: iterate clips on this track that overlap the block.
    // We hold the clip lock for the whole loop (matches AudioEngine convention).
    const juce::ScopedTryLock cl (clips_->getLock());
    if (! cl.isLocked())
        return;

    auto clipsOnTrack = clips_->getClipsOnTrack (trackId);
    for (auto* clip : clipsOnTrack)
    {
        if (clip->isMuted() || clip->getType() != ClipType::MIDI)
            continue;

        auto* midiClip = static_cast<MidiClip*> (clip);
        const auto clipStartSamples = clip->getStartPosition();
        const auto clipEndSamples   = clipStartSamples + clip->getLength();

        // Skip clips that don't overlap this block
        if (blockEnd <= clipStartSamples || blockStart >= clipEndSamples)
            continue;

        auto snap = midiClip->getPianoRollModel().getSnapshot();
        if (snap.notes == nullptr) continue;

        const int ppq = snap.ppq;

        // Block window relative to clip start, clamped to clip bounds
        const juce::int64 relBlockStart = juce::jmax<juce::int64> (0, blockStart - clipStartSamples);
        const juce::int64 relBlockEnd   = juce::jmin (clipEndSamples - clipStartSamples,
                                                      blockEnd - clipStartSamples);
        if (relBlockEnd <= relBlockStart) continue;

        const juce::int64 blockStartTicks = samplesToTicks (relBlockStart, ppq);
        const juce::int64 blockEndTicks   = samplesToTicks (relBlockEnd,   ppq);

        for (const auto& note : *snap.notes)
        {
            if (note.muted) continue;
            if (note.startTick <  blockStartTicks) continue;
            if (note.startTick >= blockEndTicks)   continue;

            // Sample-accurate offset within this block
            const juce::int64 noteRelSamples = ticksToSamples (note.startTick, ppq);
            const juce::int64 noteAbsSamples = clipStartSamples + noteRelSamples;
            const int sampleOffset = (int) juce::jlimit<juce::int64> (
                0, (juce::int64) transport.numSamples - 1,
                noteAbsSamples - blockStart);

            const int finalPitch = juce::jlimit (0, 127, (int) note.pitch);
            const float velocity = (float) note.velocity / 127.0f;

            midiOut.addEvent (juce::MidiMessage::noteOn (note.channel + 1, finalPitch, velocity),
                              sampleOffset);

            // Schedule note-off
            const juce::int64 noteEndSamples = clipStartSamples
                + ticksToSamples (note.startTick + note.lengthTicks, ppq);

            {
                const juce::ScopedLock sl (activeNotesLock_);
                ActiveNote a;
                a.noteId     = note.id;
                a.endSamples = noteEndSamples;
                a.pitch      = (juce::uint8) finalPitch;
                a.channel    = note.channel;
                activeNotes_[trackId].push_back (a);
            }

            // Notify Bubblegum hook on the message thread
            dispatcher_.enqueue (&midiClip->getPianoRollModel(), note);
        }
    }
}

//==============================================================================
void PianoRollPlaybackCore::allNotesOff (const juce::String& trackId, juce::MidiBuffer& midiOut)
{
    const juce::ScopedLock sl (activeNotesLock_);
    auto it = activeNotes_.find (trackId);
    if (it == activeNotes_.end()) return;

    for (const auto& n : it->second)
        midiOut.addEvent (juce::MidiMessage::noteOff (n.channel + 1, n.pitch), 0);

    it->second.clear();
}

void PianoRollPlaybackCore::allNotesOffGlobal (juce::MidiBuffer& midiOut)
{
    const juce::ScopedLock sl (activeNotesLock_);
    for (auto& [id, notes] : activeNotes_)
    {
        for (const auto& n : notes)
            midiOut.addEvent (juce::MidiMessage::noteOff (n.channel + 1, n.pitch), 0);
        notes.clear();
    }
}

//==============================================================================
void PianoRollPlaybackCore::NoteFiredDispatcher::enqueue (
    PianoRollClipModel* model,
    const PianoRollClipModel::NoteEvent& note)
{
    {
        const juce::ScopedLock sl (lock_);
        PendingFire pf;
        pf.model = model;
        pf.note  = note;
        queued_.add (pf);
    }
    triggerAsyncUpdate();
}

void PianoRollPlaybackCore::NoteFiredDispatcher::handleAsyncUpdate()
{
    juce::Array<PendingFire> snapshot;
    {
        const juce::ScopedLock sl (lock_);
        snapshot.swapWith (queued_);
    }
    for (auto& pf : snapshot)
        if (pf.model != nullptr)
            pf.model->notifyNoteFired (pf.note);
}

} // namespace DAW
