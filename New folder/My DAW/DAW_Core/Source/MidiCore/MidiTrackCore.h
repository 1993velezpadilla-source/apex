#pragma once

#include <JuceHeader.h>
#include "../TrackCore/Track.h"

namespace DAW {

/**
    MidiTrackCore
    =============

    Thin MIDI-specific track nucleus.
    Keeps MIDI metadata on the track while reusing the base Track model
    for shared timeline/mixer state.
*/
class MidiTrackCore : public Track
{
public:
    MidiTrackCore(const TrackID& id, const juce::String& name);
    ~MidiTrackCore() override;

    juce::String getMidiInputDevice() const { return midiInputDevice_; }
    void setMidiInputDevice(const juce::String& deviceName);

    int getMidiInputChannel() const { return midiInputChannel_; }
    void setMidiInputChannel(int channel);

    int getTransposeSemitones() const { return transposeSemitones_; }
    void setTransposeSemitones(int semitones);

    bool isMpeEnabled() const { return mpeEnabled_; }
    void setMpeEnabled(bool enabled);

    juce::ValueTree getState() const override;
    void restoreState(const juce::ValueTree& state) override;

private:
    juce::String midiInputDevice_;
    int midiInputChannel_ = 0; // 0 = any, 1-16 = explicit MIDI channel
    int transposeSemitones_ = 0;
    bool mpeEnabled_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTrackCore)
};

} // namespace DAW
