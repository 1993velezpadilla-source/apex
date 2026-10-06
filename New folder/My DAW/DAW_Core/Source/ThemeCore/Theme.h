#pragma once
#include <JuceHeader.h>

namespace DAW {

class Theme
{
public:
    Theme();
    
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
