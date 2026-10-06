// =============================================================================
//  ApexTuneColors.h
//  Placeholder color palette for the VocalTune editor.
//
//  Drop-in: Source/VocalTuneUI/ApexTuneColors.h
//           (Or move to wherever your existing Col module lives; see below.)
//
//  Depends on: JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  === IMPORTANT — REPLACE WITH YOUR REAL Col:: PALETTE WIRING ================
//
//  Every value below is a placeholder. The architecture is correct -- all
//  component code calls Col::VocalTune::xxx() instead of using hex literals --
//  so swapping in your real APEX palette is purely a find/replace job inside
//  this one file. None of the component cpps need to change.
//
//  Two ways to wire to your real palette:
//
//   1. (Simplest) Edit the inline returns below to call your existing
//      Col:: functions:
//          inline juce::Colour background() { return Col::darkPanel(); }
//
//   2. Move these declarations into your existing Col header and provide
//      definitions in your existing Col module. Then delete this file.
//
//  If your existing Col namespace lives elsewhere (e.g. apex::Col), wrap
//  this in the correct outer namespace.
// =============================================================================

#pragma once

#include <JuceHeader.h>

namespace Col { namespace VocalTune {

// ---- Surfaces & chrome ------------------------------------------------------
inline juce::Colour background()        { return juce::Colour::fromRGB ( 28,  30,  35); }
inline juce::Colour toolbarBg()         { return juce::Colour::fromRGB ( 38,  40,  45); }
inline juce::Colour panelDivider()      { return juce::Colour::fromRGB ( 60,  62,  70); }
inline juce::Colour text()              { return juce::Colour::fromRGB (220, 220, 225); }
inline juce::Colour textDim()           { return juce::Colour::fromRGB (140, 140, 150); }

// ---- Piano keys -------------------------------------------------------------
inline juce::Colour pianoKeyWhite()     { return juce::Colour::fromRGB ( 70,  72,  78); }
inline juce::Colour pianoKeyBlack()     { return juce::Colour::fromRGB ( 18,  20,  24); }
inline juce::Colour pianoKeyEdge()      { return juce::Colour::fromRGB (  8,  10,  14); }
inline juce::Colour pianoKeyLabel()     { return juce::Colour::fromRGB (170, 170, 180); }
inline juce::Colour pianoKeyHighlight() { return juce::Colour::fromRGB (150, 200, 255); }

// ---- Pitch grid -------------------------------------------------------------
inline juce::Colour scaleLine()         { return juce::Colour::fromRGB ( 50,  52,  58); }
inline juce::Colour scaleLineOctave()   { return juce::Colour::fromRGB ( 80,  82,  88); }

// ---- Waveform & pitch trace -------------------------------------------------
inline juce::Colour waveform()          { return juce::Colour::fromRGB ( 60,  65,  75).withAlpha (0.55f); }
inline juce::Colour pitchTrace()        { return juce::Colour::fromRGB (255, 240, 200); }
inline juce::Colour targetLine()        { return juce::Colour::fromRGB (200, 200, 220).withAlpha (0.55f); }

// ---- Note blocks (color-coded by deviation, Vovious-style) ------------------
// < 15 cents off:  in-tune (green)
// 15-35:           slight  (yellow)
// 35-60:           off     (orange)
// > 60:            far off (red)
inline juce::Colour noteInTune()        { return juce::Colour::fromRGB ( 80, 220, 130); }
inline juce::Colour noteSlight()        { return juce::Colour::fromRGB (220, 200,  80); }
inline juce::Colour noteOff()           { return juce::Colour::fromRGB (230, 130,  70); }
inline juce::Colour noteFarOff()        { return juce::Colour::fromRGB (230,  80,  80); }

// ---- Note states ------------------------------------------------------------
inline juce::Colour noteSelectedRing()  { return juce::Colour::fromRGB (255, 255, 255); }
inline juce::Colour noteOutline()       { return juce::Colour::fromRGB ( 20,  20,  25).withAlpha (0.5f); }
inline juce::Colour noteSibilantTint()  { return juce::Colour::fromRGB (160, 130, 230); }

// ---- Misc -------------------------------------------------------------------
inline juce::Colour playhead()          { return juce::Colour::fromRGB (255, 255, 255).withAlpha (0.7f); }

// ---- Helper: pick a note color from |cents off| ------------------------------
inline juce::Colour noteForCentsOff (float absCents)
{
    if (absCents < 15.0f) return noteInTune();
    if (absCents < 35.0f) return noteSlight();
    if (absCents < 60.0f) return noteOff();
    return noteFarOff();
}

}} // namespace Col::VocalTune
