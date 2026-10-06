// =============================================================================
//  ApexTunePianoGridComponent.cpp
//  PHASE 6a STUB. Replace this entire file in Phase 6b with the full
//  piano-key drawing + label rendering implementation.
//
//  Drop-in: Source/VocalTuneUI/ApexTunePianoGridComponent.cpp
// =============================================================================

#include "ApexTunePianoGridComponent.h"
#include "ApexTuneColors.h"

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
ApexTunePianoGridComponent::ApexTunePianoGridComponent() = default;
ApexTunePianoGridComponent::~ApexTunePianoGridComponent() = default;

// -----------------------------------------------------------------------------
void ApexTunePianoGridComponent::setMidiRange (int lowMidi, int highMidi)
{
    if (highMidi <= lowMidi) return;
    lowMidi_  = lowMidi;
    highMidi_ = highMidi;
    repaint();
}

void ApexTunePianoGridComponent::setHighlightedMidi (int midi)
{
    if (midi == highlightedMidi_) return;
    highlightedMidi_ = midi;
    repaint();
}

// -----------------------------------------------------------------------------
float ApexTunePianoGridComponent::midiToY (float midi) const noexcept
{
    const float range = (float) (highMidi_ - lowMidi_);
    if (range <= 0.0f || getHeight() <= 0) return 0.0f;
    const float frac  = (midi - (float) lowMidi_) / range;
    // Higher MIDI = higher pitch = top of component.
    return (float) getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, frac));
}

float ApexTunePianoGridComponent::yToMidi (float y) const noexcept
{
    if (getHeight() <= 0) return (float) lowMidi_;
    const float frac = 1.0f - juce::jlimit (0.0f, 1.0f, y / (float) getHeight());
    return (float) lowMidi_ + frac * (float) (highMidi_ - lowMidi_);
}

// -----------------------------------------------------------------------------
void ApexTunePianoGridComponent::paint (juce::Graphics& g)
{
    // STUB: solid panel with a centered label so the layout is visible.
    g.fillAll (Col::VocalTune::pianoKeyWhite());

    g.setColour (Col::VocalTune::panelDivider());
    g.drawRect (getLocalBounds(), 1);

    g.setColour (Col::VocalTune::textDim());
    g.setFont (juce::Font (11.0f, juce::Font::plain));
    g.drawText ("Piano Grid (Phase 6b)",
                getLocalBounds(),
                juce::Justification::centred);
}

// -----------------------------------------------------------------------------
void ApexTunePianoGridComponent::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    // STUB: nothing to lay out yet.
}

}} // namespace apex::vocaltune
