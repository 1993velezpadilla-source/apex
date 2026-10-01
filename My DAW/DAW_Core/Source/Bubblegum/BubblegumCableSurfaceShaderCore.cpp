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
    //   9.5   Endpoint impact splashes    [toggle: enableImpactSplash]
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
        // ── Cable-type dispatch ────────────────────────────────────────────
        // ARC STREAM: normal Send/Aux → violet electrical plasma
        // Master:     Send→Master → bright bubblegum-pink liquid (unchanged)
        // Sidechain:  separate cyan chain renderer, never reaches here
        if (frame.cableType == CableType::NormalSend && !frame.isMasterTarget)
        {
            paintArcStreamNormalSend(g, frame, geometry, style, steps, 0.0f);
            return;
        }

        using QM = BubblegumCableStyleSettingsCore::Style::QualityMode;

        // ── MINIMAL MODE — Simple static cable (max performance) ──────────────
        // No animations, no effects, just a basic bezier curve with gradient.
        // Perfect for huge projects with 100+ tracks where you need Reaper-like speed.
        if (style.qualityMode == QM::Minimal)
        {
            auto samples = geometry.sampleLiquidStroke(frame, steps, style);
            auto body    = geometry.buildLiquidBodyPath(frame, samples, style);

            const float srcX = frame.source.x;
            const float srcY = frame.source.y;
            const float dstX = frame.destination.x;
            const float dstY = frame.destination.y;
            const float cx   = (srcX + dstX) * 0.5f;
            const float cy   = (srcY + dstY) * 0.5f;
            const float T    = frame.thickness;

            // Simple 2-stop gradient (top = lighter, bottom = darker)
            juce::ColourGradient simpleGrad(
                style.bodyTop,
                cx, cy - T * 1.4f,
                style.bodyBottom,
                cx, cy + T * 1.9f,
                false);
            g.setGradientFill(simpleGrad);
            g.fillPath(body);

            // Optional thin outline for clarity
            if (style.enableOutline)
            {
                g.setColour(style.outline);
                g.strokePath(body, juce::PathStrokeType(0.5f));
            }

            return;  // Skip all effects/animations
        }

        // ── PERFORMANCE / CINEMATIC MODES — Full liquid rendering ─────────────
        const bool cinematic = (style.qualityMode == QM::Cinematic);

        // ── Build all geometry ─────────────────────────────────────────────
        auto samples = geometry.sampleLiquidStroke(frame, steps, style);
        auto body    = geometry.buildLiquidBodyPath(frame, samples, style);
        auto core    = geometry.buildInnerCorePath(frame, samples, style);

        juce::Path mist;
        if (cinematic && style.enableMistPass)
            mist = geometry.buildMistEnvelopePath(frame, samples, style);

        juce::Path destEndCap;
        if (style.enableImpactSplash)
            destEndCap = geometry.buildDestinationEndCapPath(frame, samples, style);

        const float srcX = frame.source.x;
        const float srcY = frame.source.y;
        const float dstX = frame.destination.x;
        const float dstY = frame.destination.y;
        const float cx   = (srcX + dstX) * 0.5f;
        const float cy   = (srcY + dstY) * 0.5f;
        const float T    = frame.thickness;

        // Quantized values used ONLY for gradient endpoint construction.
        // JUCE's Direct2D backend caches gradient brush handles keyed by
        // ColourGradient equality; snapping inputs to a coarse grid lets the
        // cache hit across frames without changing the live cable silhouette.
        auto q2  = [](float v) noexcept { return std::round(v * 0.5f) * 2.0f; };
        auto q4  = [](float v) noexcept { return std::round(v * 0.25f) * 4.0f; };
        auto qT  = [](float v) noexcept { return std::round(v * 2.0f) * 0.5f; };

        const float qcx    = q2(cx);
        const float qcy    = q2(cy);
        const float qThick = qT(T);
        const float qSrcX  = q4(srcX);
        const float qSrcY  = q4(srcY);
        const float qDstX  = q4(dstX);
        const float qDstY  = q4(dstY);

        // ── PASS 1 · Mist halo ────────────────────────────────────────────────
        if (cinematic && style.enableMistPass)
        {
            juce::ColourGradient fog(
                style.mistTop,
                qcx, qcy - qThick * 3.2f,
                style.mistBottom,
                qcx, qcy + qThick * 3.0f,
                false);
            g.setGradientFill(fog);
            g.fillPath(mist);

            if (cinematic)
            {
                juce::ColourGradient noirFog(
                    style.shadow.withMultipliedAlpha(0.08f),
                    qcx, qcy + qThick * 0.20f,
                    juce::Colours::transparentBlack,
                    qcx, qcy + qThick * 3.4f,
                    false);
                g.setGradientFill(noirFog);
                g.fillPath(mist);
            }
        }

        // ── PASS 2 · Depth shadow offset ──────────────────────────────────────
        {
            auto shadow = body;
            shadow.applyTransform(juce::AffineTransform::translation(0.0f, T * 0.18f));
            g.setColour(style.shadow);
            g.fillPath(shadow);

            if (cinematic)
            {
                auto softShadow = body;
                softShadow.applyTransform(juce::AffineTransform::translation(0.0f, T * 0.32f));
                g.setColour(style.shadow.withMultipliedAlpha(0.42f));
                g.fillPath(softShadow);
            }
        }

        // ── PASS 2.5 · CINEMATIC — Scatter glow ───────────────────────────────
        // Soft outward bloom before the body fills. Adds atmospheric scattering.
        if (cinematic && style.enableCinematicScatterGlow)
        {
            auto scatter = body;
            scatter.applyTransform(juce::AffineTransform::scale(1.006f, 1.025f, cx, cy));

            juce::ColourGradient scatterGrad(
                style.bodyTop.withMultipliedAlpha(0.035f),
                qcx, qcy - qThick * 1.6f,
                style.bodyBottom.withMultipliedAlpha(0.045f),
                qcx, qcy + qThick * 2.4f,
                false);

            g.setGradientFill(scatterGrad);
            g.fillPath(scatter);

            auto halo = body;
            halo.applyTransform(juce::AffineTransform::scale(1.012f, 1.05f, cx, cy));
            juce::ColourGradient haloGrad(
                style.highlightSharp.withMultipliedAlpha(0.016f),
                qcx, qcy - qThick * 1.2f,
                style.bodyBottom.withMultipliedAlpha(0.025f),
                qcx, qcy + qThick * 2.8f,
                false);
            g.setGradientFill(haloGrad);
            g.fillPath(halo);
        }

        // ── PASS 3 · Main body — 4-stop cross-body gradient ───────────────────
        {
            juce::ColourGradient bodyGrad(
                style.bodyTop,
                qcx, qcy - qThick * 1.55f,
                style.bodyBottom,
                qcx, qcy + qThick * 2.05f,
                false);
            bodyGrad.addColour(0.08, style.highlightSharp.withMultipliedAlpha(0.14f));
            bodyGrad.addColour(0.18, style.highlightSoft.withMultipliedAlpha(0.28f));
            bodyGrad.addColour(0.30, style.bodyTop.interpolatedWith(style.highlightSoft, 0.32f)
                                       .withMultipliedAlpha(0.98f));
            bodyGrad.addColour(0.48, style.bodyTop.withMultipliedAlpha(0.92f));
            bodyGrad.addColour(0.66, style.bodyBottom.interpolatedWith(style.shadow, 0.14f)
                                       .withMultipliedAlpha(0.99f));
            bodyGrad.addColour(0.84, style.shadow.withMultipliedAlpha(0.52f));
            g.setGradientFill(bodyGrad);
            g.fillPath(body);

            juce::Graphics::ScopedSaveState bodyClip(g);
            g.reduceClipRegion(body);

            juce::ColourGradient glassRefraction(
                style.highlightSoft.withMultipliedAlpha(0.14f),
                qcx - qThick * 0.6f, qcy - qThick * 0.9f,
                juce::Colours::transparentWhite,
                qcx + qThick * 1.4f, qcy + qThick * 0.9f,
                false);
            glassRefraction.addColour(0.24, style.highlightSharp.withMultipliedAlpha(0.10f));
            glassRefraction.addColour(0.46, style.bodyTop.withMultipliedAlpha(0.08f));
            glassRefraction.addColour(0.78, style.bodyBottom.withMultipliedAlpha(0.06f));
            g.setGradientFill(glassRefraction);
            g.fillRect(body.getBounds().expanded((int)std::ceil(T * 1.4f), (int)std::ceil(T * 1.6f)));

            juce::ColourGradient undersideReflect(
                style.bodyBottom.brighter(0.10f).withMultipliedAlpha(0.025f),
                qcx, qcy + qThick * 0.10f,
                style.shadow.withMultipliedAlpha(0.10f),
                qcx, qcy + qThick * 1.45f,
                false);
            undersideReflect.addColour(0.42, style.bodyBottom.withMultipliedAlpha(0.05f));
            g.setGradientFill(undersideReflect);
            g.fillRect(body.getBounds().expanded((int)std::ceil(T * 0.9f), (int)std::ceil(T * 1.2f)));

            juce::Path champagneBand;
            if (!samples.empty())
            {
                const auto& s0 = samples.front();
                champagneBand.startNewSubPath(s0.point + s0.normal * (T * 0.10f));
                for (size_t i = 1; i < samples.size(); ++i)
                {
                    const auto& s = samples[i];
                    champagneBand.lineTo(s.point + s.normal * (T * 0.12f * (0.92f + s.widthMul * 0.08f)));
                }
            }

            g.setColour(style.highlightSharp.withMultipliedAlpha(0.08f));
            g.strokePath(champagneBand, juce::PathStrokeType(
                juce::jmax(1.4f, T * 0.22f),
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));

            juce::Path crownThread;
            if (!samples.empty())
            {
                const auto& s0 = samples.front();
                crownThread.startNewSubPath(s0.point + s0.normal * (T * 0.20f));
                for (size_t i = 1; i < samples.size(); ++i)
                {
                    const auto& s = samples[i];
                    crownThread.lineTo(s.point + s.normal * (T * (0.18f + s.widthMul * 0.04f)));
                }
            }

            g.setColour(style.highlightSharp.withMultipliedAlpha(0.06f));
            g.strokePath(crownThread, juce::PathStrokeType(
                juce::jmax(0.8f, T * 0.10f),
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));

            if (cinematic)
            {
                juce::Path obsidianBand;
                if (!samples.empty())
                {
                    const auto& s0 = samples.front();
                    obsidianBand.startNewSubPath(s0.point - s0.normal * (T * 0.18f));
                    for (size_t i = 1; i < samples.size(); ++i)
                    {
                        const auto& s = samples[i];
                        obsidianBand.lineTo(s.point - s.normal * (T * (0.16f + s.lowerSplashBias * 0.04f)));
                    }
                }

                g.setColour(style.shadow.withMultipliedAlpha(0.18f));
                g.strokePath(obsidianBand, juce::PathStrokeType(
                    juce::jmax(1.0f, T * 0.18f),
                    juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded));
            }
        }

        
        // -- Destination end-cap (Option B) --
        // The cable swells into its landing shape at the destination.
        // Drawn outside the Pass 3 clip region so it is not masked by body.
        // Shaded with the same bodyGrad so it reads as one continuous liquid.
        if (style.enableImpactSplash && !destEndCap.isEmpty())
        {
            juce::ColourGradient endCapGrad(
                style.bodyTop,
                qcx, qcy - qThick * 1.55f,
                style.bodyBottom,
                qcx, qcy + qThick * 2.05f,
                false);
            endCapGrad.addColour(0.08, style.highlightSharp.withMultipliedAlpha(0.14f));
            endCapGrad.addColour(0.18, style.highlightSoft.withMultipliedAlpha(0.28f));
            endCapGrad.addColour(0.30, style.bodyTop.interpolatedWith(style.highlightSoft, 0.32f)
                                       .withMultipliedAlpha(0.98f));
            endCapGrad.addColour(0.48, style.bodyTop.withMultipliedAlpha(0.92f));
            endCapGrad.addColour(0.66, style.bodyBottom.interpolatedWith(style.shadow, 0.14f)
                                       .withMultipliedAlpha(0.99f));
            endCapGrad.addColour(0.84, style.shadow.withMultipliedAlpha(0.52f));
            g.setGradientFill(endCapGrad);
            g.fillPath(destEndCap);

            if (style.enableOutline)
            {
                g.setColour(style.outline);
                g.strokePath(destEndCap, juce::PathStrokeType(1.0f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
      // ── PASS 3.5 · CINEMATIC — Depth aura ─────────────────────────────────
        // Faint outward halo in highlight colour. Adds perceived volume.
        if (cinematic && style.enableCinematicDepthAura)
        {
            auto aura = body;
            aura.applyTransform(juce::AffineTransform::scale(1.015f, 1.08f, cx, cy));
            g.setColour(style.highlightSoft.withMultipliedAlpha(0.026f));
            g.fillPath(aura);

            auto lowerAura = body;
            lowerAura.applyTransform(juce::AffineTransform::translation(0.0f, T * 0.10f));
            g.setColour(style.shadow.withMultipliedAlpha(0.05f));
            g.fillPath(lowerAura);
        }

        // ── PASS 3.6 · CINEMATIC — Anti-vector imperfection ───────────────────
        // Faint bottom-tinted stroke kills the vector-clean edge feel.
        if (cinematic)
        {
            g.setColour(style.bodyBottom.withMultipliedAlpha(0.02f));
            g.strokePath(body, juce::PathStrokeType(
                0.8f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        // ── PASS 4 · Axial fade ───────────────────────────────────────────────
        if (cinematic)
        {
            juce::ColourGradient axial(
                style.bodyTop.withMultipliedAlpha(0.12f),
                qSrcX, qSrcY,
                style.highlightSoft.withMultipliedAlpha(0.035f),
                qDstX, qDstY,
                false);
            axial.addColour(0.48, style.highlightSharp.withMultipliedAlpha(0.03f));
            g.setGradientFill(axial);
            g.fillPath(body);
        }

        // ── PASS 5a · Inner core gradient ─────────────────────────────────────
        {
            juce::ColourGradient coreGrad(
                style.coreTop,
                qcx, qcy - qThick * 0.70f,
                style.coreBottom,
                qcx, qcy + qThick * 0.75f,
                false);
            g.setGradientFill(coreGrad);
            g.fillPath(core);
        }

        // ── PASS 5b · Audio-reactive core glow ────────────────────────────────
        {
            g.setColour(style.coreTop.withMultipliedAlpha(frame.audioInternalGlowAlpha * 0.82f));
            g.fillPath(core);
        }

        // ── PASS 5c · Inner flow density line ─────────────────────────────────
        // Skipped in Performance mode — subtle detail not worth per-segment draw calls.
        if (cinematic && style.enableInnerFlowDensityLine)
        {
            juce::Path flowPath;
            constexpr float flowPhase = 0.0f;
            constexpr float flowSpd = 1.0f;
            const float avgWave = 0.50f + 0.50f * std::sin(
                0.5f * 14.0f - flowPhase * flowSpd);
            const float flowAlpha = frame.audioInternalGlowAlpha * (0.30f + avgWave * 0.42f);
            if (!samples.empty())
            {
                flowPath.startNewSubPath(samples[0].point);
                for (size_t i = 1; i < samples.size(); ++i)
                    flowPath.lineTo(samples[i].point);
            }
            g.setColour(style.coreTop.withMultipliedAlpha(flowAlpha));
            g.strokePath(flowPath, juce::PathStrokeType(T * 0.12f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── PASS 6 · Underside density shadow line ────────────────────────────
        if (cinematic && style.enableUndersideShadowLine)
        {
            juce::Path shadowPath;
            if (!samples.empty())
            {
                const auto& s0 = samples[0];
                auto p0 = s0.point - s0.normal * (T * (0.26f + s0.lowerSplashBias * 0.22f));
                shadowPath.startNewSubPath(p0);
                for (size_t i = 1; i < samples.size(); ++i)
                {
                    const auto& s = samples[i];
                    shadowPath.lineTo(s.point - s.normal * (T * (0.26f + s0.lowerSplashBias * 0.22f)));
                }
            }
            g.setColour(style.shadow.withMultipliedAlpha(0.24f));
            g.strokePath(shadowPath, juce::PathStrokeType(2.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── PASS 7 · Specular lump flares ─────────────────────────────────────
        if (cinematic && style.enableSpecularFlares)
        {
            for (const auto& s : samples)
            {
                if (s.widthMul < 1.18f) continue;

                const float intensity = juce::jlimit(0.0f, 1.0f,
                    (s.widthMul - 1.18f) / 1.20f);
                const auto apex = s.point + s.normal * (T * 0.40f * s.widthMul);
                const float r   = T * 0.15f * intensity;

                g.setColour(style.highlightSoft.withMultipliedAlpha(intensity * 0.15f));
                g.fillEllipse(apex.x - r * 2.0f, apex.y - r * 2.0f, r * 2.0f, r * 4.0f);

                g.setColour(style.highlightSharp.withMultipliedAlpha(intensity * 0.50f));
                g.fillEllipse(apex.x - r, apex.y - r, r * 2.0f, r * 2.0f);
            }
        }

        // ── PASS 8 · Top highlight ridge ───────────────────────────────────
        // Batched into a single path per alpha tier — eliminates per-segment draw calls.
        {
            constexpr float shaderPhase = 0.0f;
            constexpr float shaderSpeed = 1.0f;

            // Compute a representative brightness using mid-cable values
            const float midWaveA = std::sin(0.5f * 11.0f + shaderPhase * (2.6f * shaderSpeed));
            const float midWaveB = std::sin(0.5f * 33.0f - shaderPhase * (4.1f * shaderSpeed) + 0.8f);
            const float brightness = juce::jlimit(0.0f, 1.0f,
                0.24f + (midWaveA * 0.22f + midWaveB * 0.12f + 0.16f));

            juce::Path ridgePath;
            if (!samples.empty())
            {
                const auto& s0 = samples[0];
                ridgePath.startNewSubPath(s0.point + s0.normal * (T * (0.30f + s0.widthMul * 0.10f)));
                for (size_t i = 1; i < samples.size(); ++i)
                {
                    const auto& s = samples[i];
                    ridgePath.lineTo(s.point + s.normal * (T * (0.30f + s0.widthMul * 0.10f)));
                }
            }

            g.setColour(style.highlightSoft.withMultipliedAlpha(
                brightness * (0.16f + frame.audioHighlightBoost * 0.24f)));
            g.strokePath(ridgePath, juce::PathStrokeType(4.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(style.bodyTop.withMultipliedAlpha(
                brightness * (0.07f + frame.audioHighlightBoost * 0.12f)));
            g.strokePath(ridgePath, juce::PathStrokeType(2.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(style.highlightSharp.withMultipliedAlpha(
                brightness * (0.18f + frame.audioHighlightBoost * 0.30f)));
            g.strokePath(ridgePath, juce::PathStrokeType(0.95f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (cinematic)
            {
                g.setColour(style.highlightSharp.withMultipliedAlpha(
                    brightness * (0.08f + frame.audioHighlightBoost * 0.12f)));
                g.strokePath(ridgePath, juce::PathStrokeType(0.55f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // ── PASS 8.5 · CINEMATIC — Micro sheen shimmer ────────────────────────
        // High-frequency specular shimmer on the upper third. Premium touch.
        if (cinematic && style.enableCinematicMicroSheen)
        {
            constexpr float sheenPhase = 0.0f;
            constexpr float sheenSpeed = 1.0f;

            for (size_t i = 1; i < samples.size(); ++i)
            {
                const auto& a = samples[i - 1];
                const auto& b = samples[i];

                const float shimmer =
                    0.5f + 0.5f * std::sin(a.t * 61.0f + sheenPhase * sheenSpeed);

                const auto p1 = a.point + a.normal * (T * 0.18f * a.widthMul);
                const auto p2 = b.point + b.normal * (T * 0.18f * b.widthMul);

                g.setColour(style.highlightSharp.withMultipliedAlpha(0.035f * shimmer));
                g.drawLine({ p1.x, p1.y, p2.x, p2.y }, 0.7f);
            }

            juce::Path whisperRidge;
            if (!samples.empty())
            {
                const auto& s0 = samples.front();
                whisperRidge.startNewSubPath(s0.point + s0.normal * (T * 0.24f * s0.widthMul));
                for (size_t i = 1; i < samples.size(); ++i)
                {
                    const auto& s = samples[i];
                    whisperRidge.lineTo(s.point + s.normal * (T * 0.24f * s0.widthMul));
                }
            }

            g.setColour(style.highlightSharp.withMultipliedAlpha(0.025f));
            g.strokePath(whisperRidge, juce::PathStrokeType(0.45f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── PASS 9 · Static cable-body droplets ────────────────────────────
        if (style.enableDroplets && !samples.empty())
        {
            constexpr int kCableBodyDropletCount = 18;
            constexpr float tValues[kCableBodyDropletCount] =
                { 0.055f, 0.095f, 0.145f, 0.205f, 0.265f, 0.325f,
                  0.385f, 0.445f, 0.505f, 0.565f, 0.625f, 0.685f,
                  0.735f, 0.785f, 0.835f, 0.885f, 0.925f, 0.955f };
            constexpr float hangMul[kCableBodyDropletCount] =
                { 0.48f, 0.76f, 0.38f, 0.92f, 0.56f, 0.84f,
                  0.44f, 1.06f, 0.62f, 0.88f, 0.50f, 1.12f,
                  0.58f, 0.96f, 0.42f, 0.82f, 0.52f, 0.72f };
            constexpr float sizeMul[kCableBodyDropletCount] =
                { 0.20f, 0.28f, 0.17f, 0.31f, 0.23f, 0.29f,
                  0.19f, 0.34f, 0.25f, 0.30f, 0.21f, 0.35f,
                  0.24f, 0.32f, 0.18f, 0.27f, 0.22f, 0.26f };
            constexpr float sideMul[kCableBodyDropletCount] =
                { -0.32f, 0.20f, 0.36f, -0.18f, 0.28f, -0.34f,
                  0.14f, -0.26f, 0.34f, -0.12f, 0.22f, -0.38f,
                  0.30f, -0.24f, 0.16f, -0.30f, 0.26f, -0.16f };

            std::vector<DropletPoint> staticDroplets;
            staticDroplets.reserve(kCableBodyDropletCount);

            for (int i = 0; i < kCableBodyDropletCount; ++i)
            {
                const float targetT = tValues[i];
                const CableStrokeSample* best = &samples.front();
                float bestDist = std::abs(best->t - targetT);
                for (const auto& s : samples)
                {
                    const float dist = std::abs(s.t - targetT);
                    if (dist < bestDist)
                    {
                        best = &s;
                        bestDist = dist;
                    }
                }

                juce::Point<float> dropDir{ best->normal.x * 0.18f, juce::jmax(0.65f, best->normal.y) };
                const float dirLen = dropDir.getDistanceFromOrigin();
                if (dirLen > 0.001f)
                    dropDir = dropDir / dirLen;

                const float surfaceOff = T * 0.62f * best->widthMul;
                const float lateral = T * sideMul[i];
                const float hang = T * hangMul[i];

                DropletPoint dp;
                dp.attachPoint = best->point + dropDir * surfaceOff + best->tangent * (lateral * 0.35f);
                dp.position = dp.attachPoint + dropDir * hang + best->tangent * (lateral * 0.18f);
                dp.tailLength = hang;
                dp.radius = juce::jmax(1.15f, T * sizeMul[i] * best->widthMul);
                dp.opacity = 0.68f;
                staticDroplets.push_back(dp);
            }

            dropletCore.paintDropletPoints(g, staticDroplets, style, T);
        }

        // ── PASS 9.5 · Endpoint impact splashes ───────────────────────────────
        // Source radial burst where the cable exits the track (smaller, quieter pulse).
        // Destination is now handled by the geometry end-cap (buildDestinationEndCap)
        // which merges into the cable body rather than being a separate blob.
        if (style.enableImpactSplash)
        {
            // Performance mode: half the drip tail count. Cinematic: full.
            const_cast<BubblegumCableImpactSplashCore&>(impactSplashCore)
                .setDripTailDivisor(cinematic ? 1 : 2);

            // Inactive sends get a softened splash to match their thinner body
            const float stateScale = (frame.state == SendVisualState::ExistsActive)
                ? 1.0f
                : 0.65f;

            impactSplashCore.paintImpactSplash(
                g, frame.source, T, 0.0f,
                0.0f,
                frame.sendLevel01,
                style.impactSourceIntensity * stateScale,
                /*seed*/ 0,
                /*isDestination*/ false,
                style);
        }

        // ── PASS 10 · Body edge outline ───────────────────────────────────────
        if (style.enableOutline)
        {
            g.setColour(style.outline);
            g.strokePath(body, juce::PathStrokeType(
                1.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (cinematic)
            {
                g.setColour(style.shadow.withMultipliedAlpha(0.16f));
                g.strokePath(body, juce::PathStrokeType(
                    1.8f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }

    //=========================================================================
    // paintArcStreamNormalSend
    //
    // ARC STREAM violet electrical plasma for normal Send/Aux cables.
    // Violet core with restrained magenta source, lavender-white inner core,
    // moving threads, energy packets, and broken-ring electrical sockets.
    // Uses cached geometry from the frame, no allocation in hot path.
    //=========================================================================
    void BubblegumCableSurfaceShaderCore::paintArcStreamNormalSend(
        juce::Graphics& g,
        const CableResolvedFrame& frame,
        const BubblegumCableGeometryCore& geometry,
        const BubblegumCableStyleSettingsCore::Style& style,
        int steps,
        float timeSeconds) const
    {
        using QM = BubblegumCableStyleSettingsCore::Style::QualityMode;

        const bool cinematic = (style.qualityMode == QM::Cinematic);
        const bool active    = (frame.state == SendVisualState::ExistsActive);
        const float send     = juce::jlimit(0.0f, 1.0f, frame.sendLevel01);

        // ── Build cached geometry ────────────────────────────────────────
        auto samples = geometry.sampleLiquidStroke(frame, steps, style);
        auto body    = geometry.buildLiquidBodyPath(frame, samples, style);
        auto core    = geometry.buildInnerCorePath(frame, samples, style);

        const float srcX = frame.source.x;
        const float srcY = frame.source.y;
        const float dstX = frame.destination.x;
        const float dstY = frame.destination.y;
        const float T    = frame.thickness;

        const float pulse = 0.5f + 0.5f * std::sin(timeSeconds * 2.0f + (active ? 0.0f : 3.0f));
        const float baseAlpha = 0.3f + 0.6f * send; // 0.3 to 0.9 based on send level
        const float finalAlpha = active ? baseAlpha * pulse : baseAlpha * 0.4f;

        // ── PASS 1 · Outer violet plasma aura (soft glow) ─────────────────────
        {
            // Outer aura - very soft, large radius
            const float auraSize = T * (3.0f + send * 2.0f); // 3-5x thickness
            const float auraAlpha = 0.08f + send * 0.12f; // 0.08-0.20
            
            juce::Path auraPath = body;
            auraPath.applyTransform(juce::AffineTransform::scale(1.0f + auraSize / T, 1.0f + auraSize / T, 
                                                           (srcX + dstX) * 0.5f, (srcY + dstY) * 0.5f));
        
            g.setColour(style.mistTop.withMultipliedAlpha(auraAlpha));
            g.strokePath(auraPath, juce::PathStrokeType(1.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── PASS 2 · Depth shadow ────────────────────────────────────────
        {
            auto shadow = body;
            shadow.applyTransform(juce::AffineTransform::translation(0.0f, T * 0.15f));
            g.setColour(style.shadow.withMultipliedAlpha(0.2f + send * 0.3f)); // 0.2-0.5
            g.fillPath(shadow);
        }

        // ── PASS 3 · Main body - violet plasma core ───────────────────────
        {
            // Main plasma body - violet gradient with electrical core
            juce::ColourGradient plasmaGrad(
                // Outer: deep violet
                juce::Colour::fromFloatRGBA(0.4f, 0.2f, 0.8f, 0.8f),
                0.0f, 0.0f,
                // Inner: electric violet/white
                juce::Colour::fromFloatRGBA(0.8f, 0.7f, 1.0f, 0.9f),
                1.0f, 0.0f,
                false);
                
            // Make it follow the cable path by using the body path as a clip
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(body);
            
            // Fill with radial gradient from center outward
            const float centerX = (srcX + dstX) * 0.5f;
            const float centerY = (srcY + dstY) * 0.5f;
            const float radius  = T * 2.0f;
            
            juce::ColourGradient radialGrad(
                juce::Colour::fromFloatRGBA(0.9f, 0.8f, 1.0f, 0.9f), // bright center
                juce::Point<float>(centerX, centerY),
                juce::Colour::fromFloatRGBA(0.4f, 0.2f, 0.8f, 0.6f), // dimmer edge
                juce::Point<float>(centerX + radius, centerY + radius),
                false);
                
            g.setGradientFill(radialGrad);
            g.fillRect(juce::Rectangle<float>(centerX - radius, centerY - radius, 
                                              radius * 2.0f, radius * 2.0f));
            
            // Restore clip and draw the actual plasma flow along the path
            g.setColour(juce::Colour::fromFloatRGBA(0.6f, 0.4f, 0.9f, finalAlpha * 0.8f));
            g.strokePath(body, juce::PathStrokeType(T * 1.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                
            // Bright core line following the path
            g.setColour(juce::Colour::fromFloatRGBA(0.9f, 0.8f, 1.0f, finalAlpha));
            g.strokePath(core, juce::PathStrokeType(T * 0.3f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── PASS 4 · Subtle animation indicators ────────────────────────
        if (active && !std::isnan(timeSeconds))
        {
            // Add a few moving "energy particles" along the path
            const int particleCount = 3;
            const float speed = 0.5f + send * 1.0f; // 0.5-1.5 units per second
            
            for (int i = 0; i < particleCount; ++i)
            {
                // Staggered positions along the path
                const float phase = (float)i / (float)particleCount;
                const float tPos = std::fmod(timeSeconds * speed + phase, 1.0f);
                
                if (tPos >= 0.0f && tPos <= 1.0f)
                {
                    // Point on the curve
                    const juce::Point<float> pos = geometry.pointOnCurve(frame, tPos);
                    const juce::Point<float> tan = geometry.tangentOnCurve(frame, tPos);
                    
                    // Particle size based on send level
                    const float particleSize = 2.0f + send * 3.0f; // 2-5px
                    
                    // Draw particle with motion blur effect
                    juce::Path particle;
                    particle.addEllipse(-particleSize * 0.5f, -particleSize * 0.5f, 
                                      particleSize, particleSize);
                                  
                    // Stretch in direction of motion for speed effect
                    const float stretch = 1.0f + send * 1.0f; // 1.0-2.0x stretch
                    particle.applyTransform(juce::AffineTransform::scale(
                        stretch, 1.0f, 0.0f, 0.0f));
                    // Rotate to match tangent direction
                    const float angle = std::atan2(tan.y, tan.x);
                    particle.applyTransform(juce::AffineTransform::rotation(angle, 
                                                                          pos.x, pos.y));
                                                                  
                    // Position on path
                    particle.applyTransform(juce::AffineTransform::translation(
                                                                          pos.x, pos.y));
                                                                      
                    // Color: bright electric cyan-white
                    const float pulseMod = 0.5f + 0.5f * std::sin(timeSeconds * 10.0f + i);
                    const juce::Colour particleColor = juce::Colour::fromFloatRGBA(
                        0.33f,  // R - cyan
                        0.82f,  // G
                        1.00f,  // B - bright white/blue
                        0.7f * pulseMod); // Alpha
                    
                    g.setColour(particleColor);
                    g.fillPath(particle);
                }
            }
        }

        // ── PASS 5 · Simplified sockets (placeholder for proper broken-ring design) ──
        // For now, draw simple colored circles to indicate socket positions
        // TODO: Replace with proper broken-ring electrical sockets in future implementation
        if (true) // Always show sockets for basic visibility
        {
            const float socketRadius = 6.0f + send * 4.0f; // 6 to 10 pixels
            
            // Source socket - magenta-toned
            {
                // Outer glow
                g.setColour(style.bodyTop.withMultipliedAlpha(0.2f + send * 0.3f)); // Magenta-based
                g.drawEllipse(
                    srcX - socketRadius * 1.5f,
                    srcY - socketRadius * 1.5f,
                    socketRadius * 3.0f,
                    socketRadius * 3.0f,
                    1.0f);
                
                // Main socket body
                g.setColour(style.bodyTop); // Magenta from style
                g.fillEllipse(
                    srcX - socketRadius,
                    srcY - socketRadius,
                    socketRadius * 2.0f,
                    socketRadius * 2.0f);
                
                // Inner core
                g.setColour(style.coreTop); // Lavender from style
                g.fillEllipse(
                    srcX - socketRadius * 0.5f,
                    srcY - socketRadius * 0.5f,
                    socketRadius,
                    socketRadius);
            }
            
            // Destination socket - violet-toned with violet activity indicator
            {
                // Outer glow
                g.setColour(style.bodyBottom.withMultipliedAlpha(0.2f + send * 0.3f)); // Violet-based
                g.drawEllipse(
                    dstX - socketRadius * 1.5f,
                    dstY - socketRadius * 1.5f,
                    socketRadius * 3.0f,
                    socketRadius * 3.0f,
                    1.0f);
                
                // Main socket body
                g.setColour(style.bodyBottom); // Violet from style
                g.fillEllipse(
                    dstX - socketRadius,
                    dstY - socketRadius,
                    socketRadius * 2.0f,
                    socketRadius * 2.0f);
                
                // Inner core
                g.setColour(style.coreTop); // Lavender from style
                g.fillEllipse(
                    dstX - socketRadius * 0.5f,
                    dstY - socketRadius * 0.5f,
                    socketRadius,
                    socketRadius);
                    
                // Activity indicator - pulses when active.
                // Uses the cable's own violet/lavender energy (highlightSharp /
                // coreTop) instead of cyan so the purple send never flashes
                // blue/cyan "droplets" when activated.
                if (active)
                {
                    const float pulseSize = 1.0f + send * 2.0f; // 1-3px
                    const float pulseAlpha = 0.5f + 0.5f * std::sin(timeSeconds * 4.0f); // 0-1 pulsing

                    // Inner electric-purple pulse
                    g.setColour(style.highlightSharp.withMultipliedAlpha(pulseAlpha * 0.85f));
                    g.fillEllipse(
                        dstX - pulseSize * 0.5f,
                        dstY - pulseSize * 0.5f,
                        pulseSize,
                        pulseSize);

                    // Outer lavender glow pulse
                    g.setColour(style.coreTop.withMultipliedAlpha(pulseAlpha * 0.35f));
                    g.drawEllipse(
                        dstX - pulseSize * 1.5f,
                        dstY - pulseSize * 1.5f,
                        pulseSize * 3.0f,
                        pulseSize * 3.0f,
                        1.0f);
                }
            }
        }
    }

} // namespace bubblegum