#include "Theme.h"

namespace DAW {

Theme::Theme()
{
    initializeDefaultTheme();
}

void Theme::initializeDefaultTheme()
{
    // ── Slate Studio ──────────────────────────────────────────────────────────
    // Neutral-warm charcoal foundation. Near-black never reads as tinted.
    // Single violet accent (#7c3aed). All surfaces step in 8-10 luma units.
    // Inspired by high-end studio hardware: nothing competes with the content.

    colors.background      = juce::Colour(0xff0d0d0f);   // deep base
    colors.backgroundDark  = juce::Colour(0xff080809);   // menu / deepest chrome
    colors.backgroundLight = juce::Colour(0xff131317);   // track lane alt

    colors.surface         = juce::Colour(0xff1a1a1e);   // panels, cards
    colors.surfaceHover    = juce::Colour(0xff212126);   // hover lift
    colors.surfaceActive   = juce::Colour(0xff28282f);   // pressed / selected

    colors.border          = juce::Colour(0xff252529);   // subtle structural lines
    colors.borderFocus     = juce::Colour(0xff7c3aed);   // accent focus ring

    colors.text            = juce::Colour(0xffe4e4e8);   // primary labels
    colors.textSecondary   = juce::Colour(0xff64646e);   // secondary / dims
    colors.textDisabled    = juce::Colour(0xff38383e);   // greyed-out

    colors.accent          = juce::Colour(0xff7c3aed);   // brand violet
    colors.accentHover     = juce::Colour(0xff9158f4);   // lighter on hover
    colors.accentActive    = juce::Colour(0xff6025cc);   // pressed

    colors.transportPlay   = juce::Colour(0xff14b87a);   // green — play
    colors.transportRecord = juce::Colour(0xffe83535);   // red — record
    colors.transportStop   = juce::Colour(0xff404050);   // neutral stop

    colors.meterGreen      = juce::Colour(0xff14b87a);
    colors.meterYellow     = juce::Colour(0xfffbbf24);
    colors.meterRed        = juce::Colour(0xffe83535);

    colors.waveform            = juce::Colour(0xff38b2f8);   // sky blue
    colors.waveformBackground  = juce::Colour(0xff080809);

    colors.clipBackground  = juce::Colour(0xff1c1c22);
    colors.clipBorder      = juce::Colour(0xff303040);
    colors.clipSelected    = juce::Colour(0xff7c3aed);

    colors.trackHeader     = juce::Colour(0xff111115);
    colors.trackLane       = juce::Colour(0xff0d0d0f);

    colors.mixerChannel      = juce::Colour(0xff131318);
    colors.mixerChannelHover = juce::Colour(0xff1a1a20);

    colors.routingCable         = juce::Colour(0xff38b2f8);
    colors.routingCableSelected = juce::Colour(0xfffbbf24);
    colors.sidechainCable       = juce::Colour(0xffe83535);

    colors.titleBarTop = juce::Colour(0xff303038);   // noticeably lighter than surface
    colors.titleBarBot = juce::Colour(0xff222228);   // gradient bottom, still above background
    colors.shadow      = juce::Colour(0xff000000);
    colors.glowAccent  = juce::Colour(0xff7c3aed);
    colors.positive    = juce::Colour(0xff14b87a);
    colors.warning     = juce::Colour(0xfffbbf24);
    colors.danger      = juce::Colour(0xffe83535);

    // ── Semantic control states ────────────────────────────────────────────────
    colors.controlIdle    = juce::Colour(0xff1e1e24);   // slightly above surface
    colors.controlHover   = juce::Colour(0xff262630);   // visible lift
    colors.controlActive  = juce::Colour(0xff7c3aed).withAlpha(0.22f); // accent tint
    colors.controlPressed = juce::Colour(0xff7c3aed).withAlpha(0.40f); // deeper tint

    // ── Panel depth hierarchy ──────────────────────────────────────────────────
    colors.panelDepth1    = juce::Colour(0xff0d0d0f);   // = background
    colors.panelDepth2    = juce::Colour(0xff1a1a1e);   // = surface
    colors.panelDepth3    = juce::Colour(0xff212126);   // = surfaceHover

    // ── Separators ────────────────────────────────────────────────────────────
    colors.separatorHard  = juce::Colour(0xff000000).withAlpha(0.6f);
    colors.separatorSoft  = juce::Colour(0xff252529).withAlpha(0.3f);

    // ── Premium penthouse palette ─────────────────────────────────────────────
    // Blue-black calibrated (hue 220-245°), never pure black or pure white
    colors.obsidian   = juce::Colour(0xff0a0a0c);
    colors.graphite   = juce::Colour(0xff131316);
    colors.charcoal   = juce::Colour(0xff1a1a1e);
    colors.smoke      = juce::Colour(0xff2a2a2f);
    colors.pewter     = juce::Colour(0xff3a3a40);
    colors.steel      = juce::Colour(0xff5a5a62);
    colors.platinum   = juce::Colour(0xff9a9aa3);
    colors.pearl      = juce::Colour(0xffe5e5ea);

    // Metallic accents — max 60% saturation
    colors.champagne  = juce::Colour(0xffd4af7f);
    colors.copper     = juce::Colour(0xffb87951);
    colors.amber      = juce::Colour(0xffc8923d);
    colors.ember      = juce::Colour(0xffa64a3a);
    colors.moss       = juce::Colour(0xff6b8456);
    colors.jade       = juce::Colour(0xff88a08a);

    // ── Typography ────────────────────────────────────────────────────────────
    // Clear scale: 11 → 12 → 13 → 15 → 18
    fonts.small   = juce::Font(11.0f);
    fonts.regular = juce::Font(13.0f);
    fonts.bold    = juce::Font(13.0f, juce::Font::bold);
    fonts.mono    = juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);
    fonts.large   = juce::Font(15.0f, juce::Font::bold);

    // ── Sizing / Geometry ─────────────────────────────────────────────────────
    sizing.windowCornerR = 10;   // floating window chrome
    sizing.panelCornerR  = 6;    // panels / cards
    sizing.btnCornerR    = 5;    // buttons
}

Theme& Theme::getInstance()
{
    static Theme instance;
    return instance;
}

} // namespace DAW
