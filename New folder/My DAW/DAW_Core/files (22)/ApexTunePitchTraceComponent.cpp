// =============================================================================
//  ApexTunePitchTraceComponent.cpp
//  PHASE 6a STUB. Replace this entire file in Phase 6b with the full
//  waveform/pitch-trace/note-block painting + drag interaction implementation.
//
//  Drop-in: Source/VocalTuneUI/ApexTunePitchTraceComponent.cpp
// =============================================================================

#include "ApexTunePitchTraceComponent.h"
#include "ApexTuneColors.h"

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
ApexTunePitchTraceComponent::ApexTunePitchTraceComponent() = default;
ApexTunePitchTraceComponent::~ApexTunePitchTraceComponent() = default;

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::setClipState (ApexTuneClipState* state)
{
    state_ = state;
    selectedNoteIndex_ = -1;
    repaint();
}

void ApexTunePitchTraceComponent::setAnalysis (const ApexTuneAnalysis* analysis)
{
    analysis_ = analysis;
    repaint();
}

void ApexTunePitchTraceComponent::setSourceAudio (const float* mono, int numSamples, double sampleRate)
{
    sourceAudio_ = mono;
    sourceLen_   = numSamples;
    sourceSr_    = sampleRate;
    repaint();
}

void ApexTunePitchTraceComponent::setMidiRange (int lowMidi, int highMidi)
{
    if (highMidi <= lowMidi) return;
    lowMidi_  = lowMidi;
    highMidi_ = highMidi;
    repaint();
}

void ApexTunePitchTraceComponent::setScaleInfo (const juce::String& scaleRoot, const juce::String& scaleType)
{
    scaleRoot_ = scaleRoot;
    scaleType_ = scaleType;
    repaint();
}

// -----------------------------------------------------------------------------
float ApexTunePitchTraceComponent::midiToY (float midi) const noexcept
{
    const float range = (float) (highMidi_ - lowMidi_);
    if (range <= 0.0f || getHeight() <= 0) return 0.0f;
    const float frac = (midi - (float) lowMidi_) / range;
    return (float) getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, frac));
}

float ApexTunePitchTraceComponent::yToMidi (float y) const noexcept
{
    if (getHeight() <= 0) return (float) lowMidi_;
    const float frac = 1.0f - juce::jlimit (0.0f, 1.0f, y / (float) getHeight());
    return (float) lowMidi_ + frac * (float) (highMidi_ - lowMidi_);
}

float ApexTunePitchTraceComponent::sampleToX (int64_t s) const noexcept
{
    if (sourceLen_ <= 0 || getWidth() <= 0) return 0.0f;
    const float frac = (float) s / (float) sourceLen_;
    return (float) getWidth() * juce::jlimit (0.0f, 1.0f, frac);
}

int64_t ApexTunePitchTraceComponent::xToSample (float x) const noexcept
{
    if (sourceLen_ <= 0 || getWidth() <= 0) return 0;
    const float frac = juce::jlimit (0.0f, 1.0f, x / (float) getWidth());
    return (int64_t) ((double) frac * (double) sourceLen_);
}

int ApexTunePitchTraceComponent::findNoteAt (juce::Point<float> /*p*/) const
{
    // Phase 6b: hit test against note rectangles. Stub returns no hit.
    return -1;
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::paint (juce::Graphics& g)
{
    g.fillAll (Col::VocalTune::background());

    g.setColour (Col::VocalTune::panelDivider());
    g.drawRect (getLocalBounds(), 1);

    g.setColour (Col::VocalTune::textDim());
    g.setFont (juce::Font (12.0f, juce::Font::plain));

    juce::String info = "Pitch Trace (Phase 6b)";
    if (state_ != nullptr)
        info += "\nnotes: " + juce::String ((int) state_->notes.size());
    if (analysis_ != nullptr)
        info += "   frames: " + juce::String ((int) analysis_->frames.size());
    if (sourceLen_ > 0)
        info += "   samples: " + juce::String (sourceLen_);

    g.drawFittedText (info, getLocalBounds().reduced (8),
                      juce::Justification::centred, 4);
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    // STUB: nothing to lay out yet.
}

// -----------------------------------------------------------------------------
// Mouse handling — stubs only. Phase 6b implements the real interactions.
// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseDown (const juce::MouseEvent&)        {}
void ApexTunePitchTraceComponent::mouseDrag (const juce::MouseEvent&)        {}
void ApexTunePitchTraceComponent::mouseUp   (const juce::MouseEvent&)        {}
void ApexTunePitchTraceComponent::mouseDoubleClick (const juce::MouseEvent&) {}

}} // namespace apex::vocaltune
