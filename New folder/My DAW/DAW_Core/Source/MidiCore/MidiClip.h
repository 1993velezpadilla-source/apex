#pragma once

#include <JuceHeader.h>
#include "../ClipCore/Clip.h"
#include "PianoRollClipModel.h"

namespace DAW
{

/**
    MidiClip
    ========

    Extends the base Clip class to handle MIDI clip-specific concerns:
    - Timeline position, length, color, mute (inherited from Clip)
    - Contains a PianoRollClipModel (the actual note/CC data)
    - Bridges sample-based timeline position with tick-based MIDI data
    - Integrates with ClipManager and TrackManager

    Architecture:
    - Clip-level metadata → Clip base class
    - Note/CC data → PianoRollClipModel
    - Playback logic → PianoRollPlaybackCore (future sprint)
*/
class MidiClip : public Clip
{
public:
    MidiClip(const ClipID& id, const juce::String& name);
    ~MidiClip() override;

    // Access to the piano roll data model
    PianoRollClipModel& getPianoRollModel() { return pianoRollModel_; }
    const PianoRollClipModel& getPianoRollModel() const { return pianoRollModel_; }

    // Tempo context for sample↔tick conversion
    // (Set by ApplicationCore when tempo changes or clip is created)
    void setTempo(double bpm) { tempo_ = bpm; }
    double getTempo() const { return tempo_; }

    void setSampleRate(double sr) { sampleRate_ = sr; }
    double getSampleRate() const { return sampleRate_; }

    // Sample↔Tick conversion helpers
    // (Note: proper tempo map support comes in Sprint 2)
    juce::int64 samplesToTicks(SamplePosition samples) const;
    SamplePosition ticksToSamples(juce::int64 ticks) const;

    // Sync clip length with piano roll length
    // (When piano roll length changes, update the Clip base class length)
    void syncLengthFromModel();

    // Serialization
    juce::ValueTree getState() const override;
    void restoreState(const juce::ValueTree& state) override;

private:
    PianoRollClipModel pianoRollModel_;
    double tempo_ = 120.0;
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiClip)
};

} // namespace DAW
