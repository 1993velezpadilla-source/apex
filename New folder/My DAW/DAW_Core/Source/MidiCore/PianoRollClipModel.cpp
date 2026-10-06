#include "PianoRollClipModel.h"

namespace DAW
{

//==============================================================================
PianoRollClipModel::PianoRollClipModel()
{
    rebuildSnapshot();
}

//==============================================================================
// NOTE OPERATIONS
//==============================================================================

juce::int64 PianoRollClipModel::addNote (const NoteEvent& note)
{
    NoteEvent n = note;
    n.id = nextNoteId.fetch_add (1);
    notes.push_back (n);
    rebuildSnapshot();
    sendChangeMessage();
    return n.id;
}

bool PianoRollClipModel::removeNote (juce::int64 noteId)
{
    auto it = std::find_if (notes.begin(), notes.end(),
                            [noteId] (const NoteEvent& n) { return n.id == noteId; });
    if (it == notes.end()) return false;

    notes.erase (it);
    rebuildSnapshot();
    sendChangeMessage();
    return true;
}

bool PianoRollClipModel::modifyNote (juce::int64 noteId, const NoteEvent& newValues)
{
    for (auto& n : notes)
    {
        if (n.id == noteId)
        {
            const auto preservedId = n.id;
            n = newValues;
            n.id = preservedId;
            rebuildSnapshot();
            sendChangeMessage();
            return true;
        }
    }
    return false;
}

void PianoRollClipModel::moveNotes (const juce::Array<juce::int64>& ids,
                                    juce::int64 deltaTicks,
                                    int deltaPitch)
{
    for (auto& n : notes)
    {
        if (ids.contains (n.id))
        {
            n.startTick = juce::jmax<juce::int64> (0, n.startTick + deltaTicks);
            n.pitch = (juce::uint8) juce::jlimit (0, 127, (int) n.pitch + deltaPitch);
        }
    }
    rebuildSnapshot();
    sendChangeMessage();
}

void PianoRollClipModel::resizeNotes (const juce::Array<juce::int64>& ids,
                                      juce::int64 deltaTicks,
                                      bool resizeStart)
{
    for (auto& n : notes)
    {
        if (! ids.contains (n.id)) continue;

        if (resizeStart)
        {
            const auto newStart = juce::jmax<juce::int64> (0, n.startTick + deltaTicks);
            const auto delta    = newStart - n.startTick;
            n.startTick   = newStart;
            n.lengthTicks = juce::jmax<juce::int64> (1, n.lengthTicks - delta);
        }
        else
        {
            n.lengthTicks = juce::jmax<juce::int64> (1, n.lengthTicks + deltaTicks);
        }
    }
    rebuildSnapshot();
    sendChangeMessage();
}

int PianoRollClipModel::getNoteCount() const
{
    return (int) notes.size();
}

const PianoRollClipModel::NoteEvent* PianoRollClipModel::findNote (juce::int64 id) const
{
    for (auto& n : notes)
        if (n.id == id) return &n;
    return nullptr;
}

juce::Array<PianoRollClipModel::NoteEvent>
PianoRollClipModel::getNotesInRange (juce::int64 startTick, juce::int64 endTick) const
{
    juce::Array<NoteEvent> result;
    for (auto& n : notes)
        if (n.startTick + n.lengthTicks > startTick && n.startTick < endTick)
            result.add (n);
    return result;
}

juce::Array<PianoRollClipModel::NoteEvent> PianoRollClipModel::getAllNotes() const
{
    juce::Array<NoteEvent> result;
    for (auto& n : notes) result.add (n);
    return result;
}

//==============================================================================
// CC OPERATIONS  (to be expanded in next sprint)
//==============================================================================

void PianoRollClipModel::addCCEvent (const CCEvent& cc)
{
    ccEvents.push_back (cc);
    rebuildSnapshot();
    sendChangeMessage();
}

void PianoRollClipModel::removeCCEventsInRange (juce::uint8 ccNumber,
                                                juce::int64 startTick,
                                                juce::int64 endTick)
{
    ccEvents.erase (std::remove_if (ccEvents.begin(), ccEvents.end(),
        [&] (const CCEvent& c)
        {
            return c.ccNumber == ccNumber
                && c.startTick >= startTick
                && c.startTick <  endTick;
        }), ccEvents.end());

    rebuildSnapshot();
    sendChangeMessage();
}

juce::Array<PianoRollClipModel::CCEvent>
PianoRollClipModel::getCCEvents (juce::uint8 ccNumber) const
{
    juce::Array<CCEvent> result;
    for (auto& c : ccEvents)
        if (c.ccNumber == ccNumber) result.add (c);
    return result;
}

void PianoRollClipModel::addPitchBendEvent (const PitchBendEvent& pb)
{
    pitchBendEvents.push_back (pb);
    rebuildSnapshot();
    sendChangeMessage();
}

juce::Array<PianoRollClipModel::PitchBendEvent>
PianoRollClipModel::getPitchBendEvents() const
{
    juce::Array<PitchBendEvent> result;
    for (auto& p : pitchBendEvents) result.add (p);
    return result;
}

//==============================================================================
// CLIP-LEVEL PROPERTIES
//==============================================================================

void PianoRollClipModel::setLengthTicks (juce::int64 ticks)
{
    clipLengthTicks = juce::jmax<juce::int64> (1, ticks);
    rebuildSnapshot();
    sendChangeMessage();
}

void PianoRollClipModel::setPPQ (int newPPQ)
{
    ppq = juce::jmax (1, newPPQ);
    rebuildSnapshot();
    sendChangeMessage();
}

//==============================================================================
// SNAPSHOT  (audio-thread safe read)
//==============================================================================

PianoRollClipModel::Snapshot PianoRollClipModel::getSnapshot() const
{
    const juce::ScopedLock sl (snapshotLock);
    return *currentSnapshot;
}

void PianoRollClipModel::rebuildSnapshot()
{
    auto snap = std::make_shared<Snapshot>();
    snap->notes           = std::make_shared<const std::vector<NoteEvent>>      (notes);
    snap->ccEvents        = std::make_shared<const std::vector<CCEvent>>        (ccEvents);
    snap->pitchBendEvents = std::make_shared<const std::vector<PitchBendEvent>> (pitchBendEvents);
    snap->lengthTicks     = clipLengthTicks;
    snap->ppq             = ppq;

    const juce::ScopedLock sl (snapshotLock);
    currentSnapshot = snap;
}

//==============================================================================
// SERIALIZATION  (basic — expand once schema is finalized)
//==============================================================================

namespace ids
{
    static const juce::Identifier clip       ("PianoRollClip");
    static const juce::Identifier ppq        ("ppq");
    static const juce::Identifier length     ("lengthTicks");
    static const juce::Identifier notes      ("Notes");
    static const juce::Identifier note       ("Note");
    static const juce::Identifier id         ("id");
    static const juce::Identifier startTick  ("startTick");
    static const juce::Identifier lengthT    ("len");
    static const juce::Identifier pitch      ("pitch");
    static const juce::Identifier velocity   ("vel");
    static const juce::Identifier channel    ("ch");
    static const juce::Identifier muted      ("muted");
    static const juce::Identifier microPitch ("microPitch");
    static const juce::Identifier microTime  ("microTime");
    static const juce::Identifier microGain  ("microGain");
}

juce::ValueTree PianoRollClipModel::toValueTree() const
{
    juce::ValueTree tree (ids::clip);
    tree.setProperty (ids::ppq,    ppq,             nullptr);
    tree.setProperty (ids::length, clipLengthTicks, nullptr);

    juce::ValueTree notesTree (ids::notes);
    for (auto& n : notes)
    {
        juce::ValueTree nt (ids::note);
        nt.setProperty (ids::id,         (juce::int64) n.id,         nullptr);
        nt.setProperty (ids::startTick,  (juce::int64) n.startTick,  nullptr);
        nt.setProperty (ids::lengthT,    (juce::int64) n.lengthTicks,nullptr);
        nt.setProperty (ids::pitch,      (int) n.pitch,              nullptr);
        nt.setProperty (ids::velocity,   (int) n.velocity,           nullptr);
        nt.setProperty (ids::channel,    (int) n.channel,            nullptr);
        nt.setProperty (ids::muted,      n.muted,                    nullptr);
        nt.setProperty (ids::microPitch, n.microPitchSemis,          nullptr);
        nt.setProperty (ids::microTime,  n.microTimingTicks,         nullptr);
        nt.setProperty (ids::microGain,  n.microGainDb,              nullptr);
        notesTree.addChild (nt, -1, nullptr);
    }
    tree.addChild (notesTree, -1, nullptr);
    return tree;
}

void PianoRollClipModel::fromValueTree (const juce::ValueTree& tree)
{
    if (! tree.hasType (ids::clip)) return;

    ppq             = (int) tree.getProperty (ids::ppq, 960);
    clipLengthTicks = (juce::int64) tree.getProperty (ids::length, 3840);

    notes.clear();
    juce::int64 maxId = 0;

    auto notesTree = tree.getChildWithName (ids::notes);
    for (int i = 0; i < notesTree.getNumChildren(); ++i)
    {
        auto nt = notesTree.getChild (i);
        NoteEvent n;
        n.id              = (juce::int64) nt.getProperty (ids::id);
        n.startTick       = (juce::int64) nt.getProperty (ids::startTick);
        n.lengthTicks     = (juce::int64) nt.getProperty (ids::lengthT);
        n.pitch           = (juce::uint8)(int) nt.getProperty (ids::pitch);
        n.velocity        = (juce::uint8)(int) nt.getProperty (ids::velocity);
        n.channel         = (juce::uint8)(int) nt.getProperty (ids::channel);
        n.muted           = (bool) nt.getProperty (ids::muted);
        n.microPitchSemis = (float) nt.getProperty (ids::microPitch, 0.0);
        n.microTimingTicks= (float) nt.getProperty (ids::microTime,  0.0);
        n.microGainDb     = (float) nt.getProperty (ids::microGain,  0.0);
        notes.push_back (n);
        maxId = juce::jmax (maxId, n.id);
    }

    nextNoteId.store (maxId + 1);
    rebuildSnapshot();
    sendChangeMessage();
}

//==============================================================================
// BUBBLEGUM HOOK
//==============================================================================

void PianoRollClipModel::addPlaybackListener (PlaybackListener* l)
{
    playbackListeners.add (l);
}

void PianoRollClipModel::removePlaybackListener (PlaybackListener* l)
{
    playbackListeners.remove (l);
}

void PianoRollClipModel::notifyNoteFired (const NoteEvent& note)
{
    // Called from message thread (queued from audio thread by PianoRollPlaybackCore).
    playbackListeners.call ([&] (PlaybackListener& l) { l.noteFired (note); });
}

} // namespace DAW
