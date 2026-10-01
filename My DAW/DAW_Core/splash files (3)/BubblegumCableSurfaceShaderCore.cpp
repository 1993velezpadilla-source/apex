#include "BubblegumCableSurfaceShaderCore.h"
#include <cmath>

namespace bubblegum
{
    //=========================================================================
    // paintLiquidStream
    //
    // Pass order (same across both quality modes):
    //
    //   1.    Mist halo                   [toggle: enableMistPass]
    //   2.    Depth shadow offset         [always on]
    //   2.5   Cinematic scatter glow      [Cinematic only + toggle]
    //   3.    Main body gradient          [always on]
    //   3.5   Cinematic depth aura        [Cinematic only + toggle]
    //   3.6   Anti-vector imperfection    [Cinematic only]
    //   4.    Axial fade                  [always on]
    //   5a.   Inner core gradient         [always on]
    //   5b.   Audio-reactive core glow    [always on]
    //   5c.   Inner flow density line     [toggle: enableInnerFlowDensityLine]
    //   6.    Underside shadow line       [toggle: enableUndersideShadowLine]
    //   7.    Specular lump flares        [toggle: enableSpecularFlares]
    //   8.    Top highlight ridge         [always on — identity of the stream]
    //   8.5   Cinematic micro sheen       [Cinematic only + toggle]
    //   9.    Splash drips + droplets     [toggles: enableSplashDrips/Droplets]
    //   10.   Body outline                [toggle: enableOutline]
    //=========================================================================

    void BubblegumCableSurfaceShaderCore::paintLiquidStream(
        juce::Graphics& g,
        const CableResolvedFrame& frame,
        const BubblegumCableGeometryCore& geometry,
        const BubblegumDropletCore& dropletCore,
        const BubblegumCableImpactSplashCore& impactSplashCore,
        const BubblegumCableStyleSettingsCore::Style& style,
        int steps) const
    {
        using QM = BubblegumCableStyleSettingsCore::Style::QualityMode;
        const bool cinematic = (style.qualityMode == QM::Cinematic);

        // ── Build all geometry ────────────────────────────────────────────────
        auto samples  = geometry.sampleLiquidStroke      (frame, steps, style);
        auto body     = geometry.buildLiquidBodyPath      (frame, samples, style);
        auto mist     = geometry.buildMistEnvelopePath    (frame, samples, style);
        auto core     = geometry.buildInnerCorePath       (frame, samples, style);
        auto splashes = geometry.buildSecondarySplashPaths(frame, samples, style);
        auto droplets = geometry.buildDropletPoints       (frame, samples, style);

        const float srcX = frame.source.x;
        const float srcY = frame.source.y;
        const float dstX = frame.destination.x;
        const float dstY = frame.destination.y;
        const float cx   = (srcX + dstX) * 0.5f;
        const float cy   = (srcY + dstY) * 0.5f;
        const float T    = frame.thickness;

        // ── PASS 1 · Mist halo ────────────────────────────────────────────────
        if (style.enableMistPass)
        {
            juce::ColourGradient fog(
                style.mistTop,
                cx, cy - T * 3.2f,
                style.mistBottom,
                cx, cy + T * 3.0f,
                false);
            g.setGradientFill(fog);
            g.fillPath(mist);
        }

        // ── PASS 2 · Depth shadow offset ──────────────────────────────────────
        {
            auto shadow = body;
            shadow.applyTransform(juce::AffineTransform::translation(0.0f, T * 0.18f));
            g.setColour(style.shadow);
            g.fillPath(shadow);
        }

        // ── PASS 2.5 · CINEMATIC — Scatter glow ───────────────────────────────
        // Soft outward bloom before the body fills. Adds atmospheric scattering.
        if (cinematic && style.enableCinematicScatterGlow)
        {
            auto scatter = body;
            scatter.applyTransform(juce::AffineTransform::scale(1.01f, 1.04f, cx, cy));

            juce::ColourGradient scatterGrad(
                style.bodyTop.withMultipliedAlpha(0.06f),
                cx, cy - T * 1.6f,
                style.bodyBottom.withMultipliedAlpha(0.08f),
                cx, cy + T * 2.4f,
                false);

            g.setGradientFill(scatterGrad);
            g.fillPath(scatter);
        }

        // ── PASS 3 · Main body — 4-stop cross-body gradient ───────────────────
        {
            juce::ColourGradient bodyGrad(
                style.bodyTop,
                cx, cy - T * 1.4f,
                style.bodyBottom,
                cx, cy + T * 1.9f,
                false);
            bodyGrad.addColour(0.32, style.bodyTop.brighter(0.10f).withMultipliedAlpha(0.95f));
            bodyGrad.addColour(0.68, style.bodyBottom.darker(0.08f).withMultipliedAlpha(0.98f));
            g.setGradientFill(bodyGrad);
            g.fillPath(body);
        }

        // ── PASS 3.5 · CINEMATIC — Depth aura ─────────────────────────────────
        // Faint outward halo in highlight colour. Adds perceived volume.
        if (cinematic && style.enableCinematicDepthAura)
        {
            auto aura = body;
            aura.applyTransform(juce::AffineTransform::scale(1.015f, 1.08f, cx, cy));
            g.setColour(style.highlightSoft.withMultipliedAlpha(0.045f));
            g.fillPath(aura);
        }

        // ── PASS 3.6 · CINEMATIC — Anti-vector imperfection ───────────────────
        // Faint bottom-tinted stroke kills the vector-clean edge feel.
        if (cinematic)
        {
            g.setColour(style.bodyBottom.withMultipliedAlpha(0.03f));
            g.strokePath(body, juce::PathStrokeType(
                0.8f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        // ── PASS 4 · Axial fade ───────────────────────────────────────────────
        {
            juce::ColourGradient axial(
                style.bodyTop.withMultipliedAlpha(0.14f),
                srcX, srcY,
                style.bodyTop.withMultipliedAlpha(0.05f),
                dstX, dstY,
                false);
            g.setGradientFill(axial);
            g.fillPath(body);
        }

        // ── PASS 5a · Inner core gradient ─────────────────────────────────────
        {
            juce::ColourGradient coreGrad(
                style.coreTop,
                cx, cy - T * 0.70f,
                style.coreBottom,
                cx, cy + T * 0.75f,
                false);
            g.setGradientFill(coreGrad);
            g.fillPath(core);
        }

        // ── PASS 5b · Audio-reactive core glow ────────────────────────────────
        {
            g.setColour(style.coreTop.withMultipliedAlpha(frame.audioInternalGlowAlpha));
            g.fillPath(core);
        }

        // ── PASS 5c · Inner flow density line ─────────────────────────────────
        // Moving brightness pulse along the spine. Sells "pressurized liquid".
        if (style.enableInnerFlowDensityLine)
        {
            for (size_t i = 1; i < samples.size(); ++i)
            {
                const auto& a = samples[i - 1];
                const auto& b = samples[i];

                const float flowWave =
                    0.50f + 0.50f * std::sin(
                        a.t * 14.0f
                        - frame.timeSeconds * (3.2f * frame.audioFlowSpeedScale)
                        + a.widthMul * 0.7f);

                const float alpha = frame.audioInternalGlowAlpha * (0.30f + flowWave * 0.42f);

                g.setColour(style.coreTop.withMultipliedAlpha(alpha));
                g.drawLine({ a.point.x, a.point.y, b.point.x, b.point.y }, T * 0.12f);
            }
        }

        // ── PASS 6 · Underside density shadow line ────────────────────────────
        if (style.enableUndersideShadowLine)
        {
            for (size_t i = 1; i < samples.size(); ++i)
            {
                const auto& a = samples[i - 1];
                const auto& b = samples[i];

                const auto p1 = a.point - a.normal * (T * (0.26f + a.lowerSplashBias * 0.22f));
                const auto p2 = b.point - b.normal * (T * (0.26f + b.lowerSplashBias * 0.22f));

                g.setColour(style.shadow.withMultipliedAlpha(0.32f));
                g.drawLine({ p1.x, p1.y, p2.x, p2.y }, 2.2f);
            }
        }

        // ── PASS 7 · Specular lump flares ─────────────────────────────────────
        if (style.enableSpecularFlares)
        {
            for (const auto& s : samples)
            {
                if (s.widthMul < 1.18f) continue;

                const float intensity = juce::jlimit(0.0f, 1.0f,
                    (s.widthMul - 1.18f) / 1.20f);
                const auto apex = s.point + s.normal * (T * 0.40f * s.widthMul);
                const float r   = T * 0.15f * intensity;

                g.setColour(style.highlightSoft.withMultipliedAlpha(intensity * 0.22f));
                g.fillEllipse(apex.x - r * 2.0f, apex.y - r * 2.0f, r * 4.0f, r * 4.0f);

                g.setColour(style.highlightSharp.withMultipliedAlpha(intensity * 0.72f));
                g.fillEllipse(apex.x - r, apex.y - r, r * 2.0f, r * 2.0f);
            }
        }

        // ── PASS 8 · Top highlight ridge ──────────────────────────────────────
        // Always on — this is the identity pass of the stream.
        {
            for (size_t i = 1; i < samples.size(); ++i)
            {
                const auto& a = samples[i - 1];
                const auto& b = samples[i];

                const float waveA =
                    std::sin(a.t * 11.0f + frame.timeSeconds * (2.6f * frame.audioFlowSpeedScale));
                const float waveB =
                    std::sin(a.t * 33.0f - frame.timeSeconds * (4.1f * frame.audioFlowSpeedScale) + 0.8f);

                const float brightness = juce::jlimit(
                    0.0f, 1.0f,
                    0.24f + (waveA * 0.22f + waveB * 0.12f + a.widthMul * 0.16f));

                const auto p1 = a.point + a.normal * (T * (0.30f + a.widthMul * 0.10f));
                const auto p2 = b.point + b.normal * (T * (0.30f + b.widthMul * 0.10f));

                g.setColour(style.highlightSoft.withMultipliedAlpha(
                    brightness * (0.18f + frame.audioHighlightBoost * 0.30f)));
                g.drawLine({ p1.x, p1.y, p2.x, p2.y }, 3.2f);

                g.setColour(style.highlightSharp.withMultipliedAlpha(
                    brightness * (0.28f + frame.audioHighlightBoost * 0.45f)));
                g.drawLine({ p1.x, p1.y, p2.x, p2.y }, 1.1f);
            }
        }

        // ── PASS 8.5 · CINEMATIC — Micro sheen shimmer ────────────────────────
        // High-frequency specular shimmer on the upper third. Premium touch.
        if (cinematic && style.enableCinematicMicroSheen)
        {
            for (size_t i = 1; i < samples.size(); ++i)
            {
                const auto& a = samples[i - 1];
                const auto& b = samples[i];

                const float shimmer =
                    0.5f + 0.5f * std::sin(a.t * 61.0f + frame.timeSeconds * 5.6f);

                const auto p1 = a.point + a.normal * (T * 0.18f * a.widthMul);
                const auto p2 = b.point + b.normal * (T * 0.18f * b.widthMul);

                g.setColour(style.highlightSharp.withMultipliedAlpha(0.06f * shimmer));
                g.drawLine({ p1.x, p1.y, p2.x, p2.y }, 0.7f);
            }
        }

        // ── PASS 9 · Splash drips + micro-droplets ────────────────────────────
        if (style.enableSplashDrips)
            dropletCore.paintSplashPaths(g, splashes, style, cx, cy, T);

        if (style.enableDroplets)
            dropletCore.paintDropletPoints(g, droplets, style, T);

        // ── PASS 9.5 · Endpoint impact splashes ───────────────────────────────
        // Radial burst where the cable meets the anchor strip.
        // Source = liquid leaving the track (smaller, quieter pulse).
        // Destination = liquid LANDING on the receive track (full intensity).
        // Different seeds ensure both splashes animate independently.
        if (style.enableImpactSplash)
        {
            // Inactive sends get a softened splash to match their thinner body
            const float stateScale = (frame.state == SendVisualState::ExistsActive)
                ? 1.0f
                : 0.65f;

            impactSplashCore.paintImpactSplash(
                g, frame.source, T, frame.timeSeconds,
                frame.audioEnergy01,
                style.impactSourceIntensity * stateScale,
                /*seed*/ 0,
                style);

            impactSplashCore.paintImpactSplash(
                g, frame.destination, T, frame.timeSeconds,
                frame.audioEnergy01,
                style.impactDestIntensity * stateScale,
                /*seed*/ 1000,
                style);
        }

        // ── PASS 10 · Body edge outline ───────────────────────────────────────
        if (style.enableOutline)
        {
            g.setColour(style.outline);
            g.strokePath(body, juce::PathStrokeType(
                1.0f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }
    }

} // namespace bubblegum
