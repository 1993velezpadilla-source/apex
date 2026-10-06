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
            // Deep U-arc: cables droop heavily below the scrollbar strip,
            // forming the belly shape shown in the hand drawing.
            float baseSag            = 28.0f;   // was 14 — much deeper idle droop
            float distanceSagScale   = 0.20f;   // was 0.11 — longer cable = deeper belly
            float verticalSagScale   = 0.10f;
            float minSag             = 18.0f;   // was 10 — even short cables sag visibly
            float maxSag             = 140.0f;  // was 76 — allows the full U
            float shortCableSagScale = 0.72f;

            // ── Motion stability ──────────────────────────────────────────────
            float smoothingHz          = 18.0f;  // exponential smoothing rate
            float snapDistance         = 0.35f;  // pixel distance to snap to target
            float maxVelocityPxPerSec  = 1800.0f;

            // ── Droplets ──────────────────────────────────────────────────────
            float dropletDensity      = 0.58f;  // raised: more drips along the whole cable
            int   maxDropletsPerSample = 4;      // raised cap per sample point

            // ── Impact splash at endpoints ────────────────────────────────────
            // The burst of pink liquid where the cable meets the anchor strip.
            // Source splash = liquid squeezing out (smaller).
            // Destination splash = liquid landing (larger).
            // Splash touches the scrollbar underside — compact burst, not a starburst.
            // Rays are biased downward so the splash reads as liquid dripping OFF
            // the strip, not exploding outward in all directions.
            float impactSplashHaloRadius = 3.20f;  // tighter halo
            int   impactSplashRayCount   = 10;     // enough tendrils for texture
            float impactSplashRayLength  = 3.00f;  // shorter = stays near the strip
            float impactSplashRayWidth   = 0.50f;  // moderate width
            float impactSourceIntensity  = 0.55f;  // source: subtle exit
            float impactDestIntensity    = 0.85f;  // destination: clear landing, not blinding

            // ── Audio reactive ────────────────────────────────────────────────
            float audioThicknessScaleMax  = 0.10f;
            float audioFlowSpeedScaleMax  = 0.18f;
            float audioInternalGlowBase   = 0.18f;
            float audioInternalGlowMaxAdd = 0.35f;
            float audioHighlightBase      = 0.08f;
            float audioHighlightMaxAdd    = 0.22f;
            float audioDropletChanceMaxAdd= 0.45f;  // raised: hot sends spawn visibly more drips

            // ── Bubblegum Pink Palette ────────────────────────────────────────
            // Every colour is pink. Not white. Not blue. Pink.
            juce::Colour bodyTop        = juce::Colour::fromFloatRGBA(0.98f, 0.95f, 0.94f, 0.88f);
            juce::Colour bodyBottom     = juce::Colour::fromFloatRGBA(0.21f, 0.05f, 0.10f, 0.98f);
            juce::Colour coreTop        = juce::Colour::fromFloatRGBA(1.00f, 0.98f, 0.96f, 0.68f);
            juce::Colour coreBottom     = juce::Colour::fromFloatRGBA(0.88f, 0.66f, 0.72f, 0.18f);
            juce::Colour mistTop        = juce::Colour::fromFloatRGBA(1.00f, 0.98f, 0.96f, 0.018f);
            juce::Colour mistBottom     = juce::Colour::fromFloatRGBA(0.40f, 0.12f, 0.17f, 0.09f);
            juce::Colour shadow         = juce::Colour::fromFloatRGBA(0.02f, 0.01f, 0.02f, 0.30f);
            juce::Colour highlightSoft  = juce::Colour::fromFloatRGBA(1.00f, 0.97f, 0.92f, 0.15f);
            juce::Colour highlightSharp = juce::Colour::fromFloatRGBA(1.00f, 0.95f, 0.86f, 0.46f);
            juce::Colour outline        = juce::Colour::fromFloatRGBA(0.96f, 0.86f, 0.82f, 0.12f);
            juce::Colour splash         = juce::Colour::fromFloatRGBA(1.00f, 0.90f, 0.87f, 0.28f);
            juce::Colour droplet        = juce::Colour::fromFloatRGBA(1.00f, 0.96f, 0.93f, 0.36f);

            // ── Quality Mode ──────────────────────────────────────────────────
            // Three quality modes to scale from massive Reaper-like projects
            // down to beautiful cinematic routing visuals:
            //   Minimal     = simple static cables, all effects OFF, max performance
            //   Performance = liquid cables with core features, balanced for large projects
            //   Cinematic   = full signature look with all effects, for mixing/demos
            enum class QualityMode
            {
                Minimal = 0,
                Performance,
                Cinematic
            };

            QualityMode qualityMode = QualityMode::Performance;

            // ── Step counts per mode ──────────────────────────────────────────
            // Fewer steps = faster render. Minimal uses bare minimum for static cables.
            int minimalSteps     = 6;   // simple bezier, no animation needed
            int performanceSteps = 16;  // smooth liquid, fast
            int cinematicSteps   = 48;  // premium quality

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

        // Returns a copy of the given style with the palette swapped to master-bus gold.
        // All other geometry / toggle / audio-reactive settings are preserved.
        static Style makeMasterGoldStyle(const Style& base) noexcept
        {
            Style g = base;
            g.bodyTop        = juce::Colour::fromFloatRGBA(1.00f, 0.97f, 0.86f, 0.88f);
            g.bodyBottom     = juce::Colour::fromFloatRGBA(0.35f, 0.23f, 0.04f, 0.98f);
            g.coreTop        = juce::Colour::fromFloatRGBA(1.00f, 0.99f, 0.92f, 0.64f);
            g.coreBottom     = juce::Colour::fromFloatRGBA(0.88f, 0.72f, 0.24f, 0.18f);
            g.mistTop        = juce::Colour::fromFloatRGBA(1.00f, 0.99f, 0.90f, 0.020f);
            g.mistBottom     = juce::Colour::fromFloatRGBA(0.58f, 0.44f, 0.10f, 0.09f);
            g.shadow         = juce::Colour::fromFloatRGBA(0.05f, 0.03f, 0.00f, 0.28f);
            g.highlightSoft  = juce::Colour::fromFloatRGBA(1.00f, 0.98f, 0.94f, 0.16f);
            g.highlightSharp = juce::Colour::fromFloatRGBA(1.00f, 0.97f, 0.82f, 0.44f);
            g.outline        = juce::Colour::fromFloatRGBA(0.97f, 0.88f, 0.64f, 0.14f);
            g.splash         = juce::Colour::fromFloatRGBA(1.00f, 0.91f, 0.70f, 0.30f);
            g.droplet        = juce::Colour::fromFloatRGBA(1.00f, 0.95f, 0.78f, 0.34f);
            return g;
        }

        // Returns step count based on active quality mode.
        // Used by the render core — never hardcode step counts elsewhere.
        int getRecommendedSteps() const noexcept
        {
            switch (style.qualityMode)
            {
                case Style::QualityMode::Minimal:     return style.minimalSteps;
                case Style::QualityMode::Cinematic:   return style.cinematicSteps;
                case Style::QualityMode::Performance:
                default:                              return style.performanceSteps;
            }
        }

        // Returns true if the given feature should render based on quality mode.
        // Minimal mode forces all effects OFF regardless of toggle state.
        // Performance/Cinematic respect individual toggles.
        bool shouldRenderFeature(bool featureToggle) const noexcept
        {
            if (style.qualityMode == Style::QualityMode::Minimal)
                return false;  // All effects OFF in Minimal mode
            return featureToggle;  // Respect toggle in other modes
        }

        // Returns true if cinematic-only features should render.
        bool shouldRenderCinematicFeature(bool featureToggle) const noexcept
        {
            if (style.qualityMode != Style::QualityMode::Cinematic)
                return false;  // Cinematic features only in Cinematic mode
            return featureToggle;  // Respect toggle
        }

    private:
        Style style;
    };

} // namespace bubblegum
