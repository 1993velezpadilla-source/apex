#include "BubblegumCableImpactSplashCore.h"
#include <cmath>

namespace bubblegum
{
    void BubblegumCableImpactSplashCore::paintImpactSplash(
        juce::Graphics& g,
        juce::Point<float> anchor,
        float thickness,
        float timeSeconds,
        float audioEnergy01,
        float intensity,
        int   seed,
        const BubblegumCableStyleSettingsCore::Style& style) const
    {
        if (intensity <= 0.0f) return;

        const float T     = thickness;
        const float audio = audioEnergy01;

        // Audio pulse — splash breathes with signal energy
        const float audioPulse = 1.0f + audio * 0.35f;

        // Subtle idle pulse so splashes feel alive even with no audio
        const float idlePulse = 1.0f
            + 0.08f * std::sin(timeSeconds * 3.2f + (float)seed * 0.73f);

        const float scale = intensity * audioPulse * idlePulse;

        // ── Layer 1 · Soft outer halo ─────────────────────────────────────────
        // Radial pink glow that blooms outward from the anchor.
        // Drawn first so rays render cleanly on top of it.
        {
            const float haloR = T * style.impactSplashHaloRadius * scale;
            if (haloR > 0.8f)
            {
                juce::ColourGradient halo(
                    style.splash.withMultipliedAlpha(0.38f * intensity),
                    anchor.x, anchor.y,
                    style.splash.withMultipliedAlpha(0.0f),
                    anchor.x + haloR, anchor.y,
                    true);   // radial
                g.setGradientFill(halo);
                g.fillEllipse(anchor.x - haloR, anchor.y - haloR,
                              haloR * 2.0f, haloR * 2.0f);
            }
        }

        // ── Layer 2 · Radial rays ─────────────────────────────────────────────
        // Tapered teardrops bursting outward. Each ray wobbles in length + angle
        // over time using a phase derived from (i + seed) so source and
        // destination splashes animate independently.
        {
            const int   rayCount  = style.impactSplashRayCount;
            const float baseLen   = T * style.impactSplashRayLength * scale;
            const float baseWidth = T * style.impactSplashRayWidth  * intensity;

            for (int i = 0; i < rayCount; ++i)
            {
                const float angleBase =
                    (float)i / (float)rayCount * juce::MathConstants<float>::twoPi;

                // Length wobble — unique per ray
                const float wobble =
                    0.78f + 0.28f * std::sin(timeSeconds * 2.4f
                                           + (float)(i + seed) * 1.73f);

                // Small angle drift keeps the burst from looking rigid
                const float angleDrift =
                    std::sin(timeSeconds * 1.1f + (float)(i + seed) * 0.61f) * 0.09f;

                const float angle  = angleBase + angleDrift;
                const float rayLen = baseLen * wobble;

                if (rayLen < 0.6f) continue;

                const float dirX  = std::cos(angle);
                const float dirY  = std::sin(angle);
                const float perpX = -dirY;
                const float perpY =  dirX;

                // Tapered teardrop: wide at base, point at tip
                const float halfW = baseWidth * 0.5f;
                const juce::Point<float> baseL{
                    anchor.x + perpX * halfW,
                    anchor.y + perpY * halfW
                };
                const juce::Point<float> baseR{
                    anchor.x - perpX * halfW,
                    anchor.y - perpY * halfW
                };
                const juce::Point<float> tip{
                    anchor.x + dirX * rayLen,
                    anchor.y + dirY * rayLen
                };

                // Curved teardrop using quadratic control points
                juce::Path ray;
                ray.startNewSubPath(baseL);
                ray.quadraticTo(
                    anchor.x + dirX * (rayLen * 0.55f) + perpX * halfW * 0.6f,
                    anchor.y + dirY * (rayLen * 0.55f) + perpY * halfW * 0.6f,
                    tip.x, tip.y);
                ray.quadraticTo(
                    anchor.x + dirX * (rayLen * 0.55f) - perpX * halfW * 0.6f,
                    anchor.y + dirY * (rayLen * 0.55f) - perpY * halfW * 0.6f,
                    baseR.x, baseR.y);
                ray.closeSubPath();

                g.setColour(style.splash.withMultipliedAlpha(intensity * 0.46f));
                g.fillPath(ray);
            }
        }

        // ── Layer 3 · Centre core ─────────────────────────────────────────────
        // Solid pink centre blob where all rays converge.
        {
            const float centreR = T * 0.48f * intensity * audioPulse;
            g.setColour(style.bodyTop.withMultipliedAlpha(0.72f * intensity));
            g.fillEllipse(anchor.x - centreR, anchor.y - centreR,
                          centreR * 2.0f, centreR * 2.0f);
        }

        // ── Layer 4 · Inner sharp highlight ───────────────────────────────────
        // Bright white-pink point at dead centre — the wet specular dot.
        {
            const float hotR = T * 0.22f * intensity;
            g.setColour(style.highlightSharp.withMultipliedAlpha(0.85f * intensity));
            g.fillEllipse(anchor.x - hotR, anchor.y - hotR,
                          hotR * 2.0f, hotR * 2.0f);
        }
    }

} // namespace bubblegum
