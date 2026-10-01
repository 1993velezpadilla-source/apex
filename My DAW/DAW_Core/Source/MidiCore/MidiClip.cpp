#include "MidiClip.h"

namespace DAW
{

//==============================================================================
MidiClip::MidiClip(const ClipID& id, const juce::String& name, double sampleRate)
    : Clip(id, name, ClipType::MIDI),
      sampleRate_ (sampleRate > 0.0 ? sampleRate : 44100.0)
{
    // Default 4-bar MIDI clip at 120 BPM = 8 seconds, derived from the
    // engine/device rate. (Was hardcoded 352800 samples = 8 s at 44.1 kHz
    // only; the 44100 ctor default remains as a legacy/fallback value for
    // creation paths that genuinely do not know the active rate.)
    setLength ((SamplePosition) std::llround (8.0 * sampleRate_));
    pianoRollModel_.setLengthTicks(3840);
}

MidiClip::~MidiClip()
{
}

void MidiClip::setSampleRate (double sr)
{
    if (sr <= 0.0 || sr == sampleRate_)
        return;

    // Seconds-preserving rescale: ticks<->samples is linear at fixed tempo,
    // so scaling the sample length by newRate/oldRate keeps BOTH the seconds
    // extent and the tick extent constant (<=1 sample rounding). Message
    // thread only (device reconfiguration / tempo paths) — never the
    // audio thread.
    const double ratio = sr / sampleRate_;
    sampleRate_ = sr;
    setLength ((SamplePosition) std::llround ((double) getLength() * ratio));
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
