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

            // ── APEX Signal Palette — normal sends (subdued) ──────────────────
            // Restrained magentaDeep/violet family: readable, clearly subordinate
            // to the singular bright bubblegum-pink send-to-Master cable.
            // Side-chain cables are handled by the side-chain renderer (cyan/blue).
            juce::Colour bodyTop        = juce::Colour::fromFloatRGBA(0.78f, 0.42f, 0.66f, 0.82f); // dim rose crown
            juce::Colour bodyBottom     = juce::Colour::fromFloatRGBA(0.16f, 0.02f, 0.11f, 0.96f); // dark magenta-violet wine
            juce::Colour coreTop        = juce::Colour::fromFloatRGBA(0.85f, 0.55f, 0.75f, 0.60f);
            juce::Colour coreBottom     = juce::Colour::fromFloatRGBA(0.55f, 0.22f, 0.52f, 0.16f);
            juce::Colour mistTop        = juce::Colour::fromFloatRGBA(0.85f, 0.55f, 0.78f, 0.016f);
            juce::Colour mistBottom     = juce::Colour::fromFloatRGBA(0.38f, 0.08f, 0.26f, 0.08f);
            juce::Colour shadow         = juce::Colour::fromFloatRGBA(0.03f, 0.01f, 0.02f, 0.30f);
            juce::Colour highlightSoft  = juce::Colour::fromFloatRGBA(0.90f, 0.62f, 0.80f, 0.13f);
            juce::Colour highlightSharp = juce::Colour::fromFloatRGBA(0.88f, 0.50f, 0.74f, 0.38f);
            juce::Colour outline        = juce::Colour::fromFloatRGBA(0.62f, 0.28f, 0.60f, 0.13f);
            juce::Colour splash         = juce::Colour::fromFloatRGBA(0.80f, 0.42f, 0.66f, 0.24f);
            juce::Colour droplet        = juce::Colour::fromFloatRGBA(0.82f, 0.48f, 0.70f, 0.30f);

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

// Returns a copy of the current style with the palette swapped to the
        // APEX send-to-Master identity: the only bright bubblegum-pink cable.
        // Slightly more presence than subdued normal sends. All geometry,
        // toggle and audio-reactive settings are preserved.
        static Style makeMasterGoldStyle(const Style& base) noexcept
        {
            Style g = base;
            g.bodyTop        = juce::Colour::fromFloatRGBA(1.00f, 0.82f, 0.92f, 0.92f); // bright rosy crown
            g.bodyBottom     = juce::Colour::fromFloatRGBA(0.55f, 0.04f, 0.26f, 0.98f); // hot pink wine
            g.coreTop        = juce::Colour::fromFloatRGBA(1.00f, 0.90f, 0.96f, 0.70f);
            g.coreBottom     = juce::Colour::fromFloatRGBA(1.00f, 0.24f, 0.56f, 0.20f); // bubblegum core
            g.mistTop        = juce::Colour::fromFloatRGBA(1.00f, 0.85f, 0.94f, 0.024f);
            g.mistBottom     = juce::Colour::fromFloatRGBA(0.72f, 0.08f, 0.38f, 0.10f);
            g.shadow         = juce::Colour::fromFloatRGBA(0.05f, 0.01f, 0.03f, 0.28f);
            g.highlightSoft  = juce::Colour::fromFloatRGBA(1.00f, 0.90f, 0.96f, 0.18f);
            g.highlightSharp = juce::Colour::fromFloatRGBA(1.00f, 0.80f, 0.92f, 0.50f);
            g.outline        = juce::Colour::fromFloatRGBA(1.00f, 0.42f, 0.70f, 0.16f);
            g.splash         = juce::Colour::fromFloatRGBA(1.00f, 0.62f, 0.83f, 0.32f);
            g.droplet        = juce::Colour::fromFloatRGBA(1.00f, 0.70f, 0.87f, 0.38f);
            // Master send carries slightly more presence.
            g.baseThickness  = base.baseThickness * 1.12f;
            return g;
        }

        // Returns a copy of the current style with the palette swapped to the
        // ARC STREAM identity for normal Send/Aux cables.
        //
        // Visual language:
        //   - Predominantly violet electrical plasma
        //   - Restrained magenta near the source socket
        //   - Lavender-white inner electrical core
        //   - Activity indicator pulses in the cable's own violet/lavender
        //   - Thinner than Master cable, thicker than Sidechain
        //
        // ARC STREAM must NOT:
        //   - Use the bright bubblegum-pink reserved for Master
        //   - Use the cyan/blue reserved for Sidechain
        //   - Become predominantly cyan
        static Style makeArcStreamStyle(const Style& base) noexcept
        {
            Style a = base;
            // ARC violet palette: ~50% deep violet, ~25% controlled magenta,
            // ~15% lavender core. Cyan is reserved exclusively for Sidechain.
            a.bodyTop        = juce::Colour::fromFloatRGBA(0.95f, 0.28f, 0.55f, 0.84f); // restrained magenta crown (source)
            a.bodyBottom     = juce::Colour::fromFloatRGBA(0.42f, 0.10f, 0.56f, 0.94f); // purple body (dest)
            a.coreTop        = juce::Colour::fromFloatRGBA(0.90f, 0.75f, 0.95f, 0.62f); // soft lavender core top
            a.coreBottom     = juce::Colour::fromFloatRGBA(0.65f, 0.35f, 0.85f, 0.22f); // purple core bottom
            a.mistTop        = juce::Colour::fromFloatRGBA(0.84f, 0.32f, 0.66f, 0.018f); // subdued violet mist top
            a.mistBottom     = juce::Colour::fromFloatRGBA(0.33f, 0.13f, 0.66f, 0.08f); // deep violet mist bottom
            a.shadow         = juce::Colour::fromFloatRGBA(0.04f, 0.01f, 0.12f, 0.26f); // violet shadow
            a.highlightSoft  = juce::Colour::fromFloatRGBA(0.90f, 0.75f, 0.95f, 0.16f); // soft lavender highlight
            a.highlightSharp = juce::Colour::fromFloatRGBA(0.85f, 0.50f, 0.95f, 0.42f); // electric purple highlight
            a.outline        = juce::Colour::fromFloatRGBA(0.55f, 0.20f, 0.72f, 0.14f); // purple outline
            a.splash         = juce::Colour::fromFloatRGBA(0.65f, 0.20f, 0.78f, 0.22f); // purple splash
            a.droplet        = juce::Colour::fromFloatRGBA(0.68f, 0.25f, 0.75f, 0.28f); // purple droplet
            // ARC STREAM medium thickness: thicker than Sidechain, thinner than Master.
            a.baseThickness  = base.baseThickness * 1.44f;  // ~7.2px base (vs 5px normal, ~5.6px Master)
            a.minThickness   = 4.0f;
            a.maxThickness   = 10.0f;
            return a;
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
