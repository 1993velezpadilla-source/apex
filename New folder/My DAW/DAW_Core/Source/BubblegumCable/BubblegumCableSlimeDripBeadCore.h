#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeDripBeadCore  -- smooth animated drip teardrops
//
// All shape dimensions are SMOOTH CONTINUOUS functions of phase.
// There are NO discrete if/else branches that could cause visual pops
// between frames -- every property is a smooth lerp or smoothstep so
// the animation looks like fluid, not a flipbook.
//
// Phase lifecycle (0 -> 1, continuous loop):
//   0.00 - 0.55  GROW   : bead radius swells 0 -> maxR
//   0.55 - 0.82  STRETCH: drop descends, neck elongates + thins
//   0.82 - 1.00  FALL   : bead detaches and falls, alpha fades
//
// All transitions are smoothstep-interpolated so there are zero hard
// edges at boundary values.
// =====================================================================
class BubblegumCableSlimeDripBeadCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float seed = in.source.x * 0.041f + in.target.x * 0.023f + 99.1f;

        const int count = 2 + (int)(BubblegumCableAnimationCore::stable01(seed) * 2.f);

        for (int i = 0; i < count; ++i)
        {
            const float fs = seed + (float)i * 11.7f;

            // Stable anchor along cable
            const float t = juce::jmap(
                BubblegumCableAnimationCore::stable01(fs + 3.3f), 0.20f, 0.80f);

            const float cycleSpeed = 0.22f
                + BubblegumCableAnimationCore::stable01(fs + 5.1f) * 0.18f
                + e * 0.10f;
            const float phase = BubblegumCableAnimationCore::frac(
                time * cycleSpeed + BubblegumCableAnimationCore::stable01(fs + 7.7f));

            const auto  s  = sampleAlong(geo, t);
            const float hw = s.halfW;

            // Lifecycle blend values (all smoothstepped, no hard edges)
            constexpr float kGrowEnd    = 0.55f;
            constexpr float kStretchEnd = 0.82f;

            const float growBlend    = smoothstep(0.f,         kGrowEnd,    phase);
            const float stretchBlend = smoothstep(kGrowEnd,    kStretchEnd, phase);
            const float fallBlend    = smoothstep(kStretchEnd, 1.0f,        phase);

            const float maxR = hw * (1.3f + e * 0.35f);

            // ── Continuous shape dimensions ────────────────────────────────
            // Bead radius: grows in, shrinks slightly as it stretches down
            const float r = maxR * growBlend * (1.f - stretchBlend * 0.12f)
                                             * (1.f - fallBlend    * 0.08f);
            if (r < 0.4f) continue;  // nothing to draw yet

            // Drop distance below anchor (smoothly increases)
            const float dropDist = r + stretchBlend * maxR * 1.5f
                                     + fallBlend    * maxR * 3.8f;

            // Neck height (appears during grow, elongates during stretch, vanishes on fall)
            const float neckH = maxR * growBlend
                              * (0.4f + stretchBlend * 1.0f)
                              * (1.f - fallBlend);

            // Neck width tapers as stretch increases
            const float neckW = juce::jmax(hw * 0.15f,
                                           hw * 0.50f * (1.f - stretchBlend * 0.70f));

            // Overall alpha: full during grow/stretch, fades fast on fall
            const float alpha = (1.f - fallBlend * fallBlend);
            if (alpha < 0.02f) continue;

            // Anchor at cable bottom edge
            const float ax = s.pos.x - s.normal.x * hw;
            const float ay = s.pos.y - s.normal.y * hw;

            // Drop center
            const float dc = dropDist;
            const float bcx = ax - s.normal.x * dc;
            const float bcy = ay - s.normal.y * dc;

            const float ex = r * (1.f - stretchBlend * 0.10f);
            const float ey = r * (1.f + stretchBlend * 0.18f);
            const float inkAlpha = alpha * (0.62f + e * 0.12f);

            // ── 1. Neck: bezier tapered path ────────────────────────────────
            if (neckH > 0.5f && alpha > 0.05f)
            {
                const float tx =  s.normal.y;  // tangent perpendicular to normal
                const float ty = -s.normal.x;

                // Four bezier control points for organic neck shape
                const float x0l = ax  - tx * neckW;
                const float y0l = ay;
                const float x0r = ax  + tx * neckW;
                const float y0r = ay;
                const float x1l = bcx - tx * neckW * 0.5f;
                const float y1l = ay  - s.normal.y * neckH;
                const float x1r = bcx + tx * neckW * 0.5f;
                const float y1r = ay  - s.normal.y * neckH;

                juce::Path neck;
                neck.startNewSubPath(x0l, y0l);
                neck.quadraticTo(x1l, y1l, bcx - tx * neckW * 0.25f,
                                           bcy + s.normal.y * r * 0.85f);
                neck.lineTo       (bcx + tx * neckW * 0.25f,
                                   bcy + s.normal.y * r * 0.85f);
                neck.quadraticTo(x1r, y1r, x0r, y0r);
                neck.closeSubPath();

                juce::ignoreUnused(x0l, y0l, x0r, y0r, x1l, y1l, x1r, y1r);

                g.setColour(kNeck.withAlpha(alpha * 0.90f));
                g.fillPath(neck);

                g.setColour(kToonLine.withAlpha(inkAlpha));
                g.strokePath(neck, juce::PathStrokeType(0.80f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // ── 2. Shadow ──────────────────────────────────────────────────
            g.setColour(juce::Colour(0x33000000).withAlpha(0.25f * alpha));
            g.fillEllipse(bcx - r * 0.85f, bcy + r * 0.30f, r * 1.7f, r * 0.80f);

            // ── 3. Solid body ──────────────────────────────────────────────
            {
                juce::ColourGradient grad(
                    kDripHigh,   bcx, bcy - r * 0.45f,
                    kDripDark,   bcx, bcy + r, false);
                grad.addColour(0.28, kDripMid);
                grad.addColour(0.66, kDripShadow);
                g.setGradientFill(grad);
                g.fillEllipse(bcx - ex, bcy - ey, ex * 2.f, ey * 2.f);
            }

            g.setColour(kToonLine.withAlpha(inkAlpha));
            g.drawEllipse(bcx - ex, bcy - ey, ex * 2.f, ey * 2.f, 0.95f);

            // ── 4. Specular ────────────────────────────────────────────────
            {
                const float sr = juce::jmax(0.5f, r * 0.22f);
                g.setColour(kSpec.withAlpha(alpha));
                g.fillEllipse(bcx - r * 0.30f - sr, bcy - r * 0.38f - sr,
                              sr * 2.f, sr * 2.f);
            }
        }
    }

private:
    const juce::Colour kSpec      { 0xFFFFFFFF };
    const juce::Colour kDripHigh  { 0xFFFFD6EC };
    const juce::Colour kDripMid   { 0xFFEE4FA0 };
    const juce::Colour kDripShadow{ 0xFF8C1848 };
    const juce::Colour kDripDark  { 0xFF2A0612 };
    const juce::Colour kNeck      { 0xFFD43A8A };
    const juce::Colour kToonLine  { 0xFF050505 };

    // Smooth Hermite interpolation (no hard edges at 0 or 1)
    static float smoothstep(float edge0, float edge1, float x) noexcept
    {
        const float t = juce::jlimit(0.f, 1.f, (x - edge0) / (edge1 - edge0));
        return t * t * (3.f - 2.f * t);
    }

    struct Sample { juce::Point<float> pos, normal; float halfW = 0.f; };

    static Sample sampleAlong(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float ft = juce::jlimit(0.f, 1.f, t) * (float)BubblegumCableGeometry::kSeg;
        const int   i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)ft);
        const int   i1 = juce::jmin(BubblegumCableGeometry::kSeg, i0 + 1);
        const float a  = ft - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        Sample s;
        s.pos    = { juce::jmap(a, p0.x,  p1.x),  juce::jmap(a, p0.y,  p1.y)  };
        s.normal = { juce::jmap(a, p0.nx, p1.nx),  juce::jmap(a, p0.ny, p1.ny) };
        s.halfW  = juce::jmap(a, p0.topW, p1.topW);
        const float nl = std::sqrt(s.normal.x * s.normal.x + s.normal.y * s.normal.y);
        if (nl > 1e-4f) { s.normal.x /= nl; s.normal.y /= nl; }
        else            { s.normal = { 0.f, -1.f }; }
        return s;
    }
};

} // namespace DAW
