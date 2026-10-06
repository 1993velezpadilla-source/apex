#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>
#include <array>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeRippleCore — expanding ripple rings inside the body
//
// Spawns 2-3 concentric oval ripple rings that expand from a point on
// the cable centerline outward, clipped inside the ribbon.
// Like dropping a stone in liquid slime — the wave expands and fades.
//
// Each ripple:
//   - Spawns at a random t position, offset per cable seed
//   - Expands from 0 to maxRadius over its lifetime
//   - Fades in then out
//   - Is oriented along the cable tangent (oval, not circular)
//   - Is clipped to the cable ribbon
//
// DYNAMIC: call every frame.
// =====================================================================
class BubblegumCableSlimeRippleCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time,
               float                         energy) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, energy);
        const float seed = BubblegumCableAnimationCore::cableSeed(in);

        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        const int rippleCount = 2 + (int)(BubblegumCableAnimationCore::stable01(seed + 0.5f) * 2.f);

        for (int i = 0; i < rippleCount; ++i)
        {
            const float si      = seed + (float)i * 11.3f;
            const float period  = 2.4f + BubblegumCableAnimationCore::stable01(si + 0.2f) * 1.6f;
            const float phOff   = BubblegumCableAnimationCore::stable01(si + 1.1f) * period;
            const float life    = BubblegumCableAnimationCore::frac((time + phOff) / period);

            const float tPos = juce::jmap(
                BubblegumCableAnimationCore::stable01(si + 2.2f), 0.18f, 0.82f);
            const auto  s    = sampleAlong(geo, tPos);
            const float ang  = std::atan2(s.ty, s.tx);
            const float maxW = juce::jmax(6.f, s.hw * 1.4f);
            const float maxH = juce::jmax(2.f, s.hw * 0.55f);

            // Expand from 0→1 and fade in 0→0.4, fade out 0.6→1
            const float expandR  = life;
            const float fadeIn   = juce::jlimit(0.f, 1.f, life * 4.0f);
            const float fadeOut  = juce::jlimit(0.f, 1.f, (1.0f - life) * 3.5f);
            const float alpha    = fadeIn * fadeOut * (0.28f + e * 0.08f);
            if (alpha < 0.01f) continue;

            // Draw 2 concentric rings per ripple
            for (int ring = 0; ring < 2; ++ring)
            {
                const float ringR  = expandR * (1.0f - (float)ring * 0.22f);
                const float rw     = maxW * ringR;
                const float rh     = maxH * ringR;
                if (rw < 0.5f) continue;

                juce::Path oval;
                oval.addEllipse(-rw, -rh, rw * 2.0f, rh * 2.0f);
                oval.applyTransform(
                    juce::AffineTransform::rotation(ang).translated(s.x, s.y));

                g.setColour(kRipple.withAlpha(alpha * (1.0f - (float)ring * 0.35f)));
                g.strokePath(oval, juce::PathStrokeType(
                    0.7f - (float)ring * 0.2f,
                    juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded));
            }
        }
    }

private:
    const juce::Colour kRipple { 0xFFFFD4EC };

    struct S { float x, y, nx, ny, tx, ty, hw; };

    static S sampleAlong(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float fi = juce::jlimit(0.f, 1.f, t) * (float)BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)std::floor(fi));
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a = fi - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        float nx = juce::jmap(a, p0.nx, p1.nx), ny = juce::jmap(a, p0.ny, p1.ny);
        const float nl = std::sqrt(nx * nx + ny * ny);
        if (nl > 1e-4f) { nx /= nl; ny /= nl; } else { nx = 0; ny = -1; }
        return { juce::jmap(a, p0.x, p1.x), juce::jmap(a, p0.y, p1.y),
                 nx, ny, -ny, nx,
                 juce::jmap(a, p0.topW, p1.topW) };
    }
};

} // namespace DAW
