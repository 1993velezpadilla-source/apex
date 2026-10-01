// =============================================================================
//  ApexTunePianoGridComponent.cpp
//  Full implementation: piano-key rows, octave labels, highlighted-key tint.
//
//  Drop-in: Source/VocalTuneUI/ApexTunePianoGridComponent.cpp
//  REPLACES the Phase 6a stub.
//
//  Key Y math is shared with ApexTunePitchTraceComponent (identical formula
//  so rows line up pixel-perfect). If you ever change midiToY here, change
//  it in PitchTrace too.
// =============================================================================

#include "ApexTunePianoGridComponent.h"
#include "ApexTuneColors.h"

namespace apex { namespace vocaltune {

namespace
{
    bool isBlackKey (int pitchClass) noexcept
    {
        // C# D# F# G# A#  -> 1 3 6 8 10
        return pitchClass == 1 || pitchClass == 3
            || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
    }

    juce::String noteName (int midi)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };
        const int pc     = ((midi % 12) + 12) % 12;
        const int octave = (midi / 12) - 1;
        return juce::String (names[pc]) + juce::String (octave);
    }
}

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
//  Shared Y mapping. MUST match ApexTunePitchTraceComponent::midiToY().
//  Visible MIDI range covers (lowMidi - 0.5) .. (highMidi + 0.5) so the
//  lowest and highest rows are fully visible (not half-clipped at the edges).
// -----------------------------------------------------------------------------
float ApexTunePianoGridComponent::midiToY (float midi) const noexcept
{
    const int   numRows = highMidi_ - lowMidi_ + 1;
    const float h       = (float) getHeight();
    if (numRows <= 0 || h <= 0.0f) return 0.0f;

    const float topMidi = (float) highMidi_ + 0.5f;
    const float botMidi = (float) lowMidi_  - 0.5f;
    const float frac    = (topMidi - midi) / (topMidi - botMidi);
    return h * juce::jlimit (0.0f, 1.0f, frac);
}

float ApexTunePianoGridComponent::yToMidi (float y) const noexcept
{
    const int   numRows = highMidi_ - lowMidi_ + 1;
    const float h       = (float) getHeight();
    if (numRows <= 0 || h <= 0.0f) return (float) lowMidi_;

    const float topMidi = (float) highMidi_ + 0.5f;
    const float botMidi = (float) lowMidi_  - 0.5f;
    const float frac    = juce::jlimit (0.0f, 1.0f, y / h);
    return topMidi - frac * (topMidi - botMidi);
}

// -----------------------------------------------------------------------------
void ApexTunePianoGridComponent::paint (juce::Graphics& g)
{
    g.fillAll (Col::VocalTune::background());

    const float w = (float) getWidth();
    if (w <= 0.0f || getHeight() <= 0) return;

    // -------------------------------------------------------------------------
    // Draw a row per MIDI value, from low to high.
    // -------------------------------------------------------------------------
    for (int midi = lowMidi_; midi <= highMidi_; ++midi)
    {
        const float ytop = midiToY ((float) midi + 0.5f);
        const float ybot = midiToY ((float) midi - 0.5f);
        const float rowH = ybot - ytop;
        if (rowH <= 0.0f) continue;

        const int  pc      = ((midi % 12) + 12) % 12;
        const bool black   = isBlackKey (pc);
        const bool octaveC = (pc == 0);

        // Key body
        g.setColour (black ? Col::VocalTune::pianoKeyBlack()
                            : Col::VocalTune::pianoKeyWhite());
        g.fillRect (juce::Rectangle<float> (0.0f, ytop, w, rowH));

        // Faint horizontal separator below each row (skip on smallest rowH)
        if (rowH >= 4.0f)
        {
            g.setColour (Col::VocalTune::pianoKeyEdge().withAlpha (0.4f));
            g.drawHorizontalLine ((int) ybot, 0.0f, w);
        }

        // Highlighted key tint
        if (midi == highlightedMidi_)
        {
            g.setColour (Col::VocalTune::pianoKeyHighlight().withAlpha (0.30f));
            g.fillRect (juce::Rectangle<float> (0.0f, ytop, w, rowH));
        }

        // Octave label on each C
        if (octaveC && rowH >= 9.0f)
        {
            g.setColour (Col::VocalTune::pianoKeyLabel());
            const float fontH = juce::jlimit (8.0f, 11.0f, rowH - 2.0f);
            g.setFont (juce::Font (fontH, juce::Font::plain));
            const juce::Rectangle<float> labelArea (4.0f, ytop, w - 6.0f, rowH);
            g.drawText (noteName (midi), labelArea,
                        juce::Justification::centredLeft, false);
        }
    }

    // Right-edge divider (separates the piano strip from the pitch trace canvas)
    g.setColour (Col::VocalTune::pianoKeyEdge());
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

// -----------------------------------------------------------------------------
void ApexTunePianoGridComponent::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    // Nothing to lay out -- painting handles everything.
}

}} // namespace apex::vocaltune
