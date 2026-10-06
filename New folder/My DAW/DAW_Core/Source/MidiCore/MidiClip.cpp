#include "MidiClip.h"

namespace DAW
{

//==============================================================================
MidiClip::MidiClip(const ClipID& id, const juce::String& name)
    : Clip(id, name, ClipType::MIDI)
{
    // Default 4-bar MIDI clip at 120 BPM, 960 PPQ
    // 4 bars = 3840 ticks at 960 PPQ
    // At 120 BPM: 1 bar = 2 seconds = 88200 samples at 44.1kHz
    // 4 bars = 352800 samples
    setLength(352800);
    pianoRollModel_.setLengthTicks(3840);
}

MidiClip::~MidiClip()
{
}

//==============================================================================
// SAMPLE↔TICK CONVERSION
//==============================================================================

juce::int64 MidiClip::samplesToTicks(SamplePosition samples) const
{
    // Basic conversion (no tempo map yet — Sprint 2)
    // BPM → beats per second = BPM / 60
    // Samples per beat = sampleRate / (BPM / 60)
    // Ticks per beat = PPQ
    // Samples per tick = (sampleRate * 60) / (BPM * PPQ)

    const int ppq = pianoRollModel_.getPPQ();
    const double samplesPerTick = (sampleRate_ * 60.0) / (tempo_ * ppq);

    return static_cast<juce::int64>(samples / samplesPerTick);
}

SamplePosition MidiClip::ticksToSamples(juce::int64 ticks) const
{
    const int ppq = pianoRollModel_.getPPQ();
    const double samplesPerTick = (sampleRate_ * 60.0) / (tempo_ * ppq);

    return static_cast<SamplePosition>(ticks * samplesPerTick);
}

void MidiClip::syncLengthFromModel()
{
    // Update Clip base class length to match piano roll ticks
    const auto tickLength = pianoRollModel_.getLengthTicks();
    const auto sampleLength = ticksToSamples(tickLength);
    setLength(sampleLength);
}

//==============================================================================
// SERIALIZATION
//==============================================================================

juce::ValueTree MidiClip::getState() const
{
    // Start with base Clip state
    auto tree = Clip::getState();

    // Add tempo context
    tree.setProperty("tempo", tempo_, nullptr);
    tree.setProperty("sampleRate", sampleRate_, nullptr);

    // Add piano roll data as child tree
    auto pianoRollTree = pianoRollModel_.toValueTree();
    tree.addChild(pianoRollTree, -1, nullptr);

    return tree;
}

void MidiClip::restoreState(const juce::ValueTree& state)
{
    // Restore base Clip properties
    Clip::restoreState(state);

    // Restore tempo context
    tempo_ = state.getProperty("tempo", 120.0);
    sampleRate_ = state.getProperty("sampleRate", 44100.0);

    // Restore piano roll data
    auto pianoRollTree = state.getChildWithName(juce::Identifier("PianoRollClip"));
    if (pianoRollTree.isValid())
    {
        pianoRollModel_.fromValueTree(pianoRollTree);
        syncLengthFromModel();
    }
}

} // namespace DAW
