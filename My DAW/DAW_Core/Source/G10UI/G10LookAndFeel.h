#pragma once
#include <JuceHeader.h>

namespace APEX {
namespace G10 {

// ============================================================================
// G10LookAndFeel — the permanent G10 visual identity.
//
// All colors are taken from the approved G10 HTML blueprint (the product
// contract for the native editor). The measured analyzer TRACE is maroon only;
// the passive chamber may use near-black violet/maroon depth. Derived shades
// stay restrained — never neon pink or RGB spectacle. Band colors follow the
// fixed frequency contract:
//   31 Hz / 16 kHz  -> deep violet (#191235 core, #6653B8 halo)
//   63 Hz / 500 Hz  -> fuchsia (#FD3F92 core, #FF8BC0 halo)
//   125 Hz          -> plum (#B34EC8 core, #F1A7FF halo)
//   250 Hz / 1 kHz / 4 kHz -> luxury gold (#FFE27A core, #F17B3B halo)
//   2 kHz           -> blue (#33B5FF core, #005CB9 halo)
//   8 kHz           -> lavender (#B34EC8 core, #F1A7FF halo)
// ============================================================================

struct G10Colors
{
    // Shell / chrome
    static const juce::Colour shellOuter()      { return juce::Colour (0xFF030305); }
    static const juce::Colour shellInner()      { return juce::Colour (0xFF060609); }
    static const juce::Colour panel()           { return juce::Colour (0xFF0A0A0F); }
    static const juce::Colour panelLine()       { return juce::Colour (0xFF1A1A22); }
    static const juce::Colour textPrimary()     { return juce::Colour (0xFFF2F2F7); }
    static const juce::Colour textSecondary()   { return juce::Colour (0xFF8A8A99); }
    static const juce::Colour textDim()         { return juce::Colour (0xFF5A5A68); }

    // Analyzer trace (maroon only) + passive Violet Galaxy Void chamber.
    static const juce::Colour analyzerCore()    { return juce::Colour (0xFF800000); }
    static const juce::Colour analyzerGlow()    { return juce::Colour (0xFFB03030); }
    static const juce::Colour analyzerDeep()    { return juce::Colour (0xFF4A0000); }
    static const juce::Colour analyzerTrace()   { return juce::Colour (0xFFA52A3A); }
    static const juce::Colour analyzerGrid()    { return juce::Colour (0xFF1E1E28); }
    static const juce::Colour analyzerVoid()    { return juce::Colour (0xFF05040A); }
    static const juce::Colour analyzerViolet()  { return juce::Colour (0xFF28143A); }
    static const juce::Colour analyzerMist()    { return juce::Colour (0xFF330D1B); }
    static const juce::Colour analyzerGuide()   { return juce::Colour (0xFF76627F); }
    static const juce::Colour analyzerRim()     { return juce::Colour (0xFF3A243E); }

    // Band identity (frequency contract)
    static const juce::Colour bandCore (int bandIndex)
    {
        switch (bandIndex)
        {
            case 0:  return juce::Colour (0xFF191235); // 31 Hz  deep violet
            case 1:  return juce::Colour (0xFFFD3F92); // 63 Hz  fuchsia
            case 2:  return juce::Colour (0xFFB34EC8); // 125 Hz plum
            case 3:  return juce::Colour (0xFFFFE27A); // 250 Hz gold
            case 4:  return juce::Colour (0xFFFD3F92); // 500 Hz fuchsia
            case 5:  return juce::Colour (0xFFFFE27A); // 1 kHz  gold
            case 6:  return juce::Colour (0xFF33B5FF); // 2 kHz  blue
            case 7:  return juce::Colour (0xFFFFE27A); // 4 kHz  gold
            case 8:  return juce::Colour (0xFFB34EC8); // 8 kHz  lavender
            case 9:  return juce::Colour (0xFF191235); // 16 kHz deep violet
            default: return juce::Colour (0xFF8A8A99);
        }
    }

    static const juce::Colour bandHalo (int bandIndex)
    {
        switch (bandIndex)
        {
            case 0:  return juce::Colour (0xFF6653B8);
            case 1:  return juce::Colour (0xFFFF8BC0);
            case 2:  return juce::Colour (0xFFF1A7FF);
            case 3:  return juce::Colour (0xFFF17B3B);
            case 4:  return juce::Colour (0xFFFF8BC0);
            case 5:  return juce::Colour (0xFFF17B3B);
            case 6:  return juce::Colour (0xFF005CB9);
            case 7:  return juce::Colour (0xFFF17B3B);
            case 8:  return juce::Colour (0xFFF1A7FF);
            case 9:  return juce::Colour (0xFF6653B8);
            default: return juce::Colour (0xFF8A8A99);
        }
    }

    // Control strip
    static const juce::Colour ctlActive()       { return juce::Colour (0xFFE8E8F0); }
    static const juce::Colour ctlInactive()     { return juce::Colour (0xFF2A2A34); }
    static const juce::Colour ctlTrack()        { return juce::Colour (0xFF14141B); }
    static const juce::Colour ctlAccent()       { return juce::Colour (0xFFFD3F92); }
};

// ============================================================================
// G10Fonts — the approved typography (Inter, the APEX UI font).
// ============================================================================

struct G10Fonts
{
    static juce::Font title()    { return juce::Font ("Inter", 24.0f, juce::Font::FontStyleFlags::bold); }
    static juce::Font subtitle() { return juce::Font ("Inter", 8.5f,  juce::Font::FontStyleFlags::plain); }
    static juce::Font micro()    { return juce::Font ("Inter", 7.5f,  juce::Font::FontStyleFlags::plain); }
    static juce::Font freq()     { return juce::Font ("Inter", 11.0f, juce::Font::FontStyleFlags::bold); }
    static juce::Font bandName() { return juce::Font ("Inter", 8.0f,  juce::Font::FontStyleFlags::bold); }
    static juce::Font db()       { return juce::Font ("Inter", 8.0f,  juce::Font::FontStyleFlags::plain); }
    static juce::Font gain()     { return juce::Font ("Inter", 9.0f,  juce::Font::FontStyleFlags::bold); }
    static juce::Font ctl()      { return juce::Font ("Inter", 10.0f, juce::Font::FontStyleFlags::bold); }
    static juce::Font ctls()     { return juce::Font ("Inter", 8.0f,  juce::Font::FontStyleFlags::plain); }
};

// ============================================================================
// G10Layout — the native editor geometry, scaled 0.625 from the approved
// 1600x980 HTML blueprint to a 1000x612 plugin window. All coordinates below
// are in the 1000x612 space.
// ============================================================================

struct G10Layout
{
    static constexpr int kWidth  = 1000;
    static constexpr int kHeight = 612;

    // Header
    static constexpr int headerTop    = 0;
    static constexpr int headerBottom = 62;

    // Analyzer
    static juce::Rectangle<int> analyzer()
    {
        return { 26, 69, 948, 212 };
    }

    // Fader columns (band centers, HTML 134..1466 scaled by 0.625)
    static constexpr int bandCenterX (int b)
    {
        constexpr int centers[10] = { 84, 176, 269, 361, 454, 546, 639, 731, 824, 916 };
        return centers[b];
    }

    static constexpr int kFaderTop    = 312; // +12 dB
    static constexpr int kFaderZero   = 394; //  0 dB
    static constexpr int kFaderBottom = 475; // -12 dB
    static constexpr int kGainTextY   = 491;

    // Control strip
    static constexpr int kStripTop    = 507;
    static constexpr int kStripBottom = 581;
};

} // namespace G10
} // namespace APEX
