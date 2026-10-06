#include "MidiTrackCore.h"

namespace DAW {

namespace MidiTrackIDs
{
    static const juce::Identifier MIDI_INPUT_DEVICE("midiInputDevice");
    static const juce::Identifier MIDI_INPUT_CHANNEL("midiInputChannel");
    static const juce::Identifier TRANSPOSE_SEMITONES("transposeSemitones");
    static const juce::Identifier MPE_ENABLED("mpeEnabled");
}

MidiTrackCore::MidiTrackCore(const TrackID& id, const juce::String& name)
    : Track(id, name)
{
    setRole(TrackRole::MIDI);
}

MidiTrackCore::~MidiTrackCore()
{
}

void MidiTrackCore::setMidiInputDevice(const juce::String& deviceName)
{
    if (midiInputDevice_ == deviceName)
        return;

    midiInputDevice_ = deviceName;
    notifyPropertyChanged();
}

void MidiTrackCore::setMidiInputChannel(int channel)
{
    const auto clamped = juce::jlimit(0, 16, channel);
    if (midiInputChannel_ == clamped)
        return;

    midiInputChannel_ = clamped;
    notifyPropertyChanged();
}

void MidiTrackCore::setTransposeSemitones(int semitones)
{
    const auto clamped = juce::jlimit(-48, 48, semitones);
    if (transposeSemitones_ == clamped)
        return;

    transposeSemitones_ = clamped;
    notifyPropertyChanged();
}

void MidiTrackCore::setMpeEnabled(bool enabled)
{
    if (mpeEnabled_ == enabled)
        return;

    mpeEnabled_ = enabled;
    notifyPropertyChanged();
}

juce::ValueTree MidiTrackCore::getState() const
{
    auto state = Track::getState();
    state.setProperty(MidiTrackIDs::MIDI_INPUT_DEVICE, midiInputDevice_, nullptr);
    state.setProperty(MidiTrackIDs::MIDI_INPUT_CHANNEL, midiInputChannel_, nullptr);
    state.setProperty(MidiTrackIDs::TRANSPOSE_SEMITONES, transposeSemitones_, nullptr);
    state.setProperty(MidiTrackIDs::MPE_ENABLED, mpeEnabled_, nullptr);
    return state;
}

void MidiTrackCore::restoreState(const juce::ValueTree& state)
{
    Track::restoreState(state);
    midiInputDevice_ = state.getProperty(MidiTrackIDs::MIDI_INPUT_DEVICE, "").toString();
    midiInputChannel_ = state.getProperty(MidiTrackIDs::MIDI_INPUT_CHANNEL, 0);
    transposeSemitones_ = state.getProperty(MidiTrackIDs::TRANSPOSE_SEMITONES, 0);
    mpeEnabled_ = state.getProperty(MidiTrackIDs::MPE_ENABLED, false);
}

} // namespace DAW
