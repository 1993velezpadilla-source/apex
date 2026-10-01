#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"
#include "BubblegumCableAnimationCoreV2.h"
#include <cmath>

namespace DAW::BgV2 {

// Endpoint splashes: the cable stream dissolves into animated water impact points.
// Ripple rings expand outward, crown droplets arc and fall, bright white caustic at center.
class BubblegumCableWaterSplashCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const CableInput& in, const MaterialPalette& pal,
               float time) const {
        paintSplash(g, geo.sx, geo.sy, in.sendEnergy, geo.thickness, pal, time, geo.seed);
        paintSplash(g, geo.tx, geo.ty, in.sendEnergy, geo.thickness, pal, time, geo.seed + 17.3f);
    }

private:
    void paintSplash(juce::Graphics& g,
                     float cx, float cy,
                     float energy, float thickness,
                     const MaterialPalette& pal,
                     float time, float seed) const {
        const float e     = juce::jlimit(0.f, 1.f, energy);
        const float baseR = juce::jmax(10.f, thickness * 2.2f);

        // Layer 1 — Outer diffuse water pool (flat ellipse)
        {
            const float poolW = baseR * 3.2f;
            const float poolH = baseR * 0.55f;
            juce::ColourGradient pool(
                pal.albedo.withAlpha(0.18f + e * 0.12f), cx, cy,
                juce::Colours::transparentBlack,          cx + poolW, cy, true);
            g.setGradientFill(pool);
            g.fillEllipse(cx - poolW, cy - poolH * 0.5f, poolW * 2.f, poolH);
        }

        // Layer 2 — Expanding ripple rings (3 rings, phase-offset)
        for (int ring = 0; ring < 3; ++ring) {
            const float ringPhase = BubblegumCableAnimationCoreV2::loopPhase(
                time, 0.55f, seed + (float)ring * 0.33f);
            const float s     = BubblegumCableAnimationCoreV2::smoothstep(ringPhase);
            const float ringR = baseR * (0.4f + s * 2.2f);
            const float ringH = ringR * 0.32f;
            const float alphaScale = ring == 0 ? 1.0f : ring == 1 ? 0.65f : 0.40f;
            const float alpha = (1.f - s) * (0.55f + e * 0.20f) * alphaScale;
            if (alpha < 0.02f) continue;
            g.setColour(pal.litTop.withAlpha(alpha));
            g.drawEllipse(cx - ringR, cy - ringH,
                          ringR * 2.f, ringH * 2.f,
                          juce::jmax(0.3f, 1.2f - s * 0.8f));
        }

        // Layer 3 — Splash crown: arc of droplets radiating outward
        constexpr int kDrops = 6;
        for (int i = 0; i < kDrops; ++i) {
            const float baseSeed  = seed + (float)i * 5.71f;
            const float freq      = 0.40f + BubblegumCableAnimationCoreV2::rand01(baseSeed) * 0.25f;
            const float phaseOff  = BubblegumCableAnimationCoreV2::rand01(baseSeed + 1.f);
            const float dropPhase = BubblegumCableAnimationCoreV2::loopPhase(time, freq, phaseOff);
            const float s         = BubblegumCableAnimationCoreV2::smoothstep(dropPhase);
            const float arcY      = -baseR * 1.6f * s * (1.f - s) * 4.f;
            const float angle     = juce::MathConstants<float>::twoPi
                                    * ((float)i / (float)kDrops)
                                    + BubblegumCableAnimationCoreV2::rand01(baseSeed + 2.f) * 0.5f;
            const float dist  = baseR * (0.6f + s * 1.4f);
            const float dx    = std::cos(angle) * dist;
            const float dy    = std::sin(angle) * dist * 0.4f + arcY;
            const float alpha = (1.f - s) * (0.70f + e * 0.20f);
            const float r     = (1.2f + BubblegumCableAnimationCoreV2::rand01(baseSeed + 3.f))
                                * (1.f - s * 0.7f);
            if (alpha < 0.02f || r < 0.2f) continue;

            juce::ColourGradient dg(
                pal.specPeak.withAlpha(alpha),       cx + dx - r * 0.3f, cy + dy - r * 0.4f,
                pal.albedo.withAlpha(alpha * 0.6f),  cx + dx + r,         cy + dy + r, false);
            g.setGradientFill(dg);
            g.fillEllipse(cx + dx - r, cy + dy - r, r * 2.f, r * 2.f);
        }

        // Layer 4 — Central impact: bright radial gradient, stream dissolves here
        {
            const float pulse   = 0.80f + 0.20f * std::sin(time * 3.1f + seed);
            const float iR      = baseR * 0.55f * pulse;
            juce::ColourGradient impact(
                juce::Colours::white.withAlpha(0.85f + e * 0.10f), cx, cy,
                pal.albedo.withAlpha(0.f),                          cx + iR, cy, true);
            g.setGradientFill(impact);
            g.fillEllipse(cx - iR, cy - iR * 0.5f, iR * 2.f, iR);
        }

        // Layer 5 — Thin inner bright ring at impact
        g.setColour(pal.specPeak.withAlpha(0.60f + e * 0.20f));
        g.drawEllipse(cx - baseR * 0.35f, cy - baseR * 0.12f,
                      baseR * 0.70f, baseR * 0.24f, 0.8f);
    }
};

} // namespace DAW::BgV2
