// =============================================================================
//  ApexTunePianoGridComponent.h
//  Left-side piano keyboard + horizontal pitch grid lines (full).
//
//  Drop-in: Source/VocalTuneUI/ApexTunePianoGridComponent.h
//  Depends on: JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Phase 6a: header is full. cpp is a stub that paints a labeled placeholder.
//  Phase 6b: cpp gets the real piano-key drawing + label rendering.
//
//  MIDI range is configurable; default 36..84 (C2..C6), 4 octaves -- covers
//  typical vocal range with some headroom. The pitch trace component shares
//  this range so the keys line up with grid rows pixel-for-pixel.
// =============================================================================

#pragma once

#include <JuceHeader.h>

namespace apex { namespace vocaltune {

class ApexTunePianoGridComponent : public juce::Component
{
public:
    ApexTunePianoGridComponent();
    ~ApexTunePianoGridComponent() override;

    // Inclusive MIDI range.
    void setMidiRange (int lowMidi, int highMidi);
    int  getLowMidi()  const { return lowMidi_;  }
    int  getHighMidi() const { return highMidi_; }

    // Highlight one key (e.g., the currently playing pitch). -1 = no highlight.
    void setHighlightedMidi (int midi);

    // Y coordinate (in this component's local space) for a given MIDI value.
    // Used by the pitch trace component to align note blocks with key rows.
    float midiToY (float midi) const noexcept;

    // Inverse mapping for hit testing.
    float yToMidi (float y) const noexcept;

    void paint   (juce::Graphics&) override;
    void resized() override;

private:
    int lowMidi_         = 36;   // C2
    int highMidi_        = 84;   // C6
    int highlightedMidi_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexTunePianoGridComponent)
};

}} // namespace apex::vocaltune
