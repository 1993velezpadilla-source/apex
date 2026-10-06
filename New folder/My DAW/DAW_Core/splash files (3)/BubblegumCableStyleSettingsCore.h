#pragma once
#include <JuceHeader.h>

namespace bubblegum
{
    //=========================================================================
    // BubblegumCableStyleSettingsCore
    //
    // Single source of truth for every tunable number in the visual system.
    // Nothing is hardcoded anywhere else — all constants come from here.
    //
    // Palette is Bubblegum pink. The stream is always pink.
    // White/blue water palette is FORBIDDEN.
    //=========================================================================
    class BubblegumCableStyleSettingsCore
    {
    public:
        struct Style
        {
            // ── Thickness ─────────────────────────────────────────────────────
            // 5px base keeps multiple parallel sends from one track visually
            // distinct. Bulges peak at ~9px * 1.40 widthMul = ~13px — still
            // readable as a liquid mass, never a fat ribbon.
            float baseThickness            = 5.0f;
            float inactiveThicknessScale   = 0.78f;  // ExistsInactive is thinner
            float minThickness             = 3.0f;
            float maxThickness             = 9.0f;

            // ── Width variation clamps ─────────────────────────────────────────
            float widthMulMin = 0.55f;
            float widthMulMax = 1.40f;   // tightened for parallel cable separation

            // ── Path geometry scales ───────────────────────────────────────────
            float mistTopScale    = 1.35f;  // tightened mist top spread
            float mistBottomScale = 1.85f;  // tightened mist bottom spread
            float coreScale       = 0.20f;  // inner core half-width as fraction of thickness

            // ── Short cable safety ─────────────────────────────────────────────
            // Below shortCableStart: bulges/noise/drips scale down to avoid chaos
            float shortCableStart = 90.0f;
            float shortCableEnd   = 240.0f;

            // ── Asymmetry bias ────────────────────────────────────────────────
            // lowerBreakupBias > topBreakupBias = heavy bottom, calm top
            float lowerBreakupBias = 1.25f;  // raised: more underside chaos
            float topBreakupBias   = 0.22f;  // lowered: cleaner top tension

            // ── Hanging / Sag ─────────────────────────────────────────────────
            float baseSag            = 14.0f;
            float distanceSagScale   = 0.11f;
            float verticalSagScale   = 0.10f;
            float minSag             = 10.0f;
            float maxSag             = 76.0f;
            float shortCableSagScale = 0.72f;

            // ── Motion stability ──────────────────────────────────────────────
            float smoothingHz          = 18.0f;  // exponential smoothing rate
            float snapDistance         = 0.35f;  // pixel distance to snap to target
            float maxVelocityPxPerSec  = 1800.0f;

            // ── Droplets ──────────────────────────────────────────────────────
            float dropletDensity      = 0.32f;  // lowered: body is hero, droplets secondary
            int   maxDropletsPerSample = 2;      // hard cap per sample point

            // ── Impact splash at endpoints ────────────────────────────────────
            // The burst of pink liquid where the cable meets the anchor strip.
            // Source splash = liquid squeezing out (smaller).
            // Destination splash = liquid landing (larger).
            float impactSplashHaloRadius = 2.60f;  // halo radius as multiple of thickness
            int   impactSplashRayCount   = 9;      // number of radial tendrils
            float impactSplashRayLength  = 2.20f;  // ray length as multiple of thickness
            float impactSplashRayWidth   = 0.42f;  // ray base width as multiple of thickness
            float impactSourceIntensity  = 0.62f;  // splash strength at source anchor
            float impactDestIntensity    = 1.00f;  // splash strength at destination (landing)

            // ── Audio reactive ────────────────────────────────────────────────
            float audioThicknessScaleMax  = 0.10f;
            float audioFlowSpeedScaleMax  = 0.18f;
            float audioInternalGlowBase   = 0.18f;
            float audioInternalGlowMaxAdd = 0.35f;
            float audioHighlightBase      = 0.08f;
            float audioHighlightMaxAdd    = 0.22f;
            float audioDropletChanceMaxAdd= 0.20f;

            // ── Bubblegum Pink Palette ────────────────────────────────────────
            // Every colour is pink. Not white. Not blue. Pink.
            juce::Colour bodyTop        = juce::Colour::fromFloatRGBA(1.00f, 0.86f, 0.93f, 0.88f);
            juce::Colour bodyBottom     = juce::Colour::fromFloatRGBA(0.95f, 0.44f, 0.66f, 0.94f);
            juce::Colour coreTop        = juce::Colour::fromFloatRGBA(1.00f, 0.96f, 0.98f, 0.66f);
            juce::Colour coreBottom     = juce::Colour::fromFloatRGBA(1.00f, 0.80f, 0.90f, 0.28f);
            juce::Colour mistTop        = juce::Colour::fromFloatRGBA(1.00f, 0.92f, 0.97f, 0.03f);
            juce::Colour mistBottom     = juce::Colour::fromFloatRGBA(1.00f, 0.72f, 0.86f, 0.10f);
            juce::Colour shadow         = juce::Colour::fromFloatRGBA(0.38f, 0.03f, 0.18f, 0.16f);
            juce::Colour highlightSoft  = juce::Colour::fromFloatRGBA(1.00f, 1.00f, 1.00f, 0.18f);
            juce::Colour highlightSharp = juce::Colour::fromFloatRGBA(1.00f, 0.97f, 0.99f, 0.48f);
            juce::Colour outline        = juce::Colour::fromFloatRGBA(1.00f, 0.84f, 0.92f, 0.12f);
            juce::Colour splash         = juce::Colour::fromFloatRGBA(1.00f, 0.88f, 0.94f, 0.34f);
            juce::Colour droplet        = juce::Colour::fromFloatRGBA(1.00f, 0.93f, 0.97f, 0.42f);

            // ── Quality Mode ──────────────────────────────────────────────────
            // One renderer, two quality modes. Performance protects responsiveness.
            // Cinematic sells the signature look. Both preserve architecture,
            // routing truth, and the Bubblegum pink identity.
            enum class QualityMode
            {
                Performance = 0,
                Cinematic
            };

            QualityMode qualityMode = QualityMode::Cinematic;

            // ── Step counts per mode ──────────────────────────────────────────
            // 48 steps is fast; 96 is premium. Renderer picks one based on mode.
            int performanceSteps = 48;
            int cinematicSteps   = 96;

            // ── Feature toggles (shared between modes) ────────────────────────
            bool enableMistPass             = true;
            bool enableSpecularFlares       = true;
            bool enableInnerFlowDensityLine = true;
            bool enableUndersideShadowLine  = true;
            bool enableDroplets             = true;
            bool enableSplashDrips          = true;
            bool enableImpactSplash         = true;   // endpoint anchor bursts
            bool enableOutline              = true;

            // ── Cinematic-only extras ─────────────────────────────────────────
            // These passes only activate when qualityMode == Cinematic AND their
            // individual flag is true. Ignored entirely in Performance mode.
            bool enableCinematicScatterGlow = true;
            bool enableCinematicDepthAura   = true;
            bool enableCinematicMicroSheen  = true;
        };

        const Style& getStyle()  const noexcept { return style; }
        void         setStyle(const Style& s)   { style = s; }

        // Returns step count based on active quality mode.
        // Used by the render core — never hardcode step counts elsewhere.
        int getRecommendedSteps() const noexcept
        {
            return style.qualityMode == Style::QualityMode::Cinematic
                ? style.cinematicSteps
                : style.performanceSteps;
        }

    private:
        Style style;
    };

} // namespace bubblegum
