#pragma once
#include <JuceHeader.h>

namespace DAW {

class Theme : private juce::DeletedAtShutdown
{
public:
    Theme();
    ~Theme() override = default;
    
    // Color palette
    struct Colors
    {
        juce::Colour background;
        juce::Colour backgroundDark;
        juce::Colour backgroundLight;
        
        juce::Colour surface;
        juce::Colour surfaceHover;
        juce::Colour surfaceActive;
        
        juce::Colour border;
        juce::Colour borderFocus;
        
        juce::Colour text;
        juce::Colour textSecondary;
        juce::Colour textDisabled;
        
        juce::Colour accent;
        juce::Colour accentHover;
        juce::Colour accentActive;
        
        juce::Colour transportPlay;
        juce::Colour transportRecord;
        juce::Colour transportStop;
        
        juce::Colour meterGreen;
        juce::Colour meterYellow;
        juce::Colour meterRed;
        
        juce::Colour waveform;
        juce::Colour waveformBackground;
        
        juce::Colour clipBackground;
        juce::Colour clipBorder;
        juce::Colour clipSelected;
        
        juce::Colour trackHeader;
        juce::Colour trackLane;
        
        juce::Colour mixerChannel;
        juce::Colour mixerChannelHover;
        
        juce::Colour routingCable;
        juce::Colour routingCableSelected;
        juce::Colour sidechainCable;

        // Extended – premium visual system
        juce::Colour titleBarTop;    // title bar gradient top
        juce::Colour titleBarBot;    // title bar gradient bottom
        juce::Colour shadow;         // drop-shadow base colour
        juce::Colour glowAccent;     // accent used for glow halos
        juce::Colour positive;       // success / play green
        juce::Colour warning;        // warning amber
        juce::Colour danger;         // error / record red

        // Semantic control states (4-state model)
        juce::Colour controlIdle;    // default control background
        juce::Colour controlHover;   // mouse-over lift
        juce::Colour controlActive;  // toggled on / selected
        juce::Colour controlPressed; // mouse-down depression

        // Panel depth hierarchy
        juce::Colour panelDepth1;    // deepest (background)
        juce::Colour panelDepth2;    // mid (surface / cards)
        juce::Colour panelDepth3;    // raised (hover / floating)

        // Separator hierarchy
        juce::Colour separatorHard;  // high-contrast structural
        juce::Colour separatorSoft;  // subtle / decorative

        // ── Premium penthouse palette ─────────────────────────────────────────
        // Calibrated blue-blacks (hue 220-245°) — never pure black/white.
        juce::Colour obsidian;   // #0a0a0c  primary app background — deepest layer
        juce::Colour graphite;   // #131316  panel backgrounds
        juce::Colour charcoal;   // #1a1a1e  channel strip surfaces
        juce::Colour smoke;      // #2a2a2f  raised interactive elements
        juce::Colour pewter;     // #3a3a40  borders, dividers
        juce::Colour steel;      // #5a5a62  secondary text, inactive icons
        juce::Colour platinum;   // #9a9aa3  primary text, active icons
        juce::Colour pearl;      // #e5e5ea  hero text, active readouts

        // Metallic accents — max 60% saturation, no neon
        juce::Colour champagne;  // #d4af7f  primary accent — selection, focus
        juce::Colour copper;     // #b87951  secondary accent — hover, activity
        juce::Colour amber;      // #c8923d  warnings — pre-clip, above-unity
        juce::Colour ember;      // #a64a3a  alerts — clip, errors (smoldering red)
        juce::Colour moss;       // #6b8456  safe state — meter in-range
        juce::Colour jade;       // #88a08a  meter mid-range transition

        // Strip translucency
        float stripOpacityNormal   = 0.72f;
        float stripOpacitySelected = 0.48f;
        int   backdropBlurRadius   = 24;
        float grainOpacity         = 0.02f;
    } colors;
    
    // Typography
    struct Fonts
    {
        juce::Font regular;
        juce::Font bold;
        juce::Font mono;
        juce::Font small;
        juce::Font large;
    } fonts;
    
    // Spacing
    struct Spacing
    {
        int xs = 4;
        int sm = 8;
        int md = 16;
        int lg = 24;
        int xl = 32;
    } spacing;
    
    // Sizing
    struct Sizing
    {
        int transportButtonHeight = 40;
        int transportButtonWidth = 40;
        int faderWidth = 40;
        int faderHeight = 200;
        int knobSize = 60;
        int meterWidth = 8;
        int channelStripWidth = 80;
        int trackHeaderWidth = 200;
        int trackLaneHeight = 80;
        int clipMinHeight = 40;
        int timelineRulerHeight = 30;
        int cornerRadius = 4;
        // Premium
        int windowCornerR = 12;  // floating window chrome
        int panelCornerR  = 8;   // panels / cards
        int btnCornerR    = 6;   // buttons
    } sizing;

    /** APEX signal-core visual identity tokens.
     *
     *  Semantic design tokens for the APEX redesign. These extend (not replace)
     *  the legacy slate palette so existing surfaces can migrate incrementally.
     *  Token names express purpose, per the canonical design-token contract:
     *  primitive → semantic → component/state layering. */
    struct ApexTokens
    {
        /** Primitive + semantic colour roles. */
        struct Color
        {
            // Deep space — the creative void everything starts from.
            juce::Colour deepestA;       // #05070C application base
            juce::Colour deepestB;       // #070A10 alt base / shadowed regions

            // Panel depth ramp (monotonically rising luminance).
            juce::Colour panelA;         // #0A0D15 primary panels
            juce::Colour panelB;         // #0E111B raised panels
            juce::Colour panelC;         // #111522 floating surfaces

            // Structural borders — restrained, never neon.
            juce::Colour borderSoftA;    // #1A2233
            juce::Colour borderSoftB;    // #22283A

            // Creative energy — magenta family (selection, play, master identity).
            juce::Colour magenta;        // #FF1678 primary brand accent
            juce::Colour magentaDeep;    // #E91572 pressed / deep accent
            juce::Colour magentaBright;  // #FF2A91 hover / bright accent
            juce::Colour pink;           // #FF3D9F secondary creative highlight

            // Transformation — violet family.
            juce::Colour violet;         // #813CFF
            juce::Colour violetBright;   // #A34CFF

            // Signal / technology — blue + cyan family.
            juce::Colour blue;           // #087BFF
            juce::Colour blueBright;     // #168DFF
            juce::Colour cyan;           // #00C8FF

            // Outer window shell border — electric steel-blue. Distinct from
            // the violet transformation accents and the near-black interior so
            // the top-level window boundary is instantly readable while staying
            // premium (blue = signal/clarity in the APEX language).
            juce::Colour shellBorder;    // #2E6FF2

            // Folder hierarchy — warm timber brown, shared by arrangement
            // and mixer adoption/create affordances.
            juce::Colour folderTimber;       // #8A5A32
            juce::Colour folderTimberBright; // #C58B54

            // Typography roles.
            juce::Colour textPrimary;    // #F2F4FA
            juce::Colour textSecondary;  // #A6ADBC
            juce::Colour textMuted;      // #687083

            // Semantic states.
            juce::Colour activeGreen;    // #12C878 active/on indication
        } color;

        /** Interaction-state strengths (0..1). Glow is semantic:
         *  active/selected controls glow; inactive controls stay dark. */
        struct State
        {
            float hoverStrength       = 0.08f;  // luminance lift on hover
            float pressedStrength     = 0.18f;  // depression tint
            float selectedStrength    = 0.30f;  // accent tint for selection
            float glowOpacityActive   = 0.55f;  // halo for active/selected
            float glowOpacityInactive = 0.12f;  // barely-there idle rim
        } state;

        /** Geometry metrics (logical units; scale with DPI). */
        struct Metric
        {
            float strokeThin    = 1.0f;   // hairline accents
            float strokeNormal  = 1.5f;   // default borders
            float radiusControl = 4.0f;   // buttons / small controls
            float radiusPanel   = 6.0f;   // cards / channel strips
            float radiusWindow  = 10.0f;  // floating panels / windows
            float glowRadius    = 10.0f;  // semantic glow falloff
        } metric;

        /** Decorative texture budget. Splatter communicates creativity
         *  escaping structure — it must never sit behind critical text. */
        struct Decor
        {
            float splatterOpacity    = 0.22f;  // max opacity of splatter layers
            float sigilOpacity       = 0.28f;  // signal sigil in unused space
            float eyeMotifOpacity    = 0.30f;  // master-track eye motif
            int   splatterSeed       = 0xA9E1; // deterministic procedural seed
        } decor;

        /** Motion timings (ms). Expert actions stay instant;
         *  animation communicates state, never delays it. */
        struct Motion
        {
            int hoverMs = 90;
            int pressMs = 70;
            int panelMs = 180;
        } motion;

        /** Typography role scale (logical points). */
        struct FontRoles
        {
            float displaySize      = 20.0f;  // hero values (tempo, position)
            float panelTitleSize   = 14.0f;  // MIXER / FX CHAIN titles
            float controlLabelSize = 12.0f;  // control labels
            float numericSize      = 13.0f;  // tabular numerics (dB, BPM)
            float metadataSize     = 10.5f;  // secondary metadata
        } font;
    } apex;

    /** Interface rendering depth.
     *  Standard    — elegant, restrained, low eye-fatigue default.
     *  Immersive3D — stronger shell depth, richer highlights, more tactile premium feel.
     *  Both share the same design language; Immersive is an enhanced depth layer, not
     *  a different art direction. */
    enum class InterfaceDepthMode { Standard, Immersive3D };
    InterfaceDepthMode depthMode = InterfaceDepthMode::Standard;

    void toggleDepthMode()
    {
        depthMode = (depthMode == InterfaceDepthMode::Standard)
                  ? InterfaceDepthMode::Immersive3D
                  : InterfaceDepthMode::Standard;
    }
    bool isImmersive() const { return depthMode == InterfaceDepthMode::Immersive3D; }

    // Get singleton instance
    static Theme& getInstance();
    
private:
    void initializeDefaultTheme();
};

} // namespace DAW
