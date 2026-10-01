#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeLayerCore — premium multi-layer slime rendering.
//
// Provides 4 independent visual layers drawn ON TOP of the base cable:
//
//   Layer 1 — Inner slime core glow    (thick, dark magenta, pulsed)
//   Layer 2 — Body slime sweep blobs   (3-6 animated pink lobes)
//   Layer 3 — Energy sweep highlights  (fast moving gloss streaks)
//   Layer 4 — Outer aura halo          (soft wide glow around silhouette)
//
// All positions are derived from BubblegumCableAnimationCore values so
// they are INVARIANT to mixer scroll/resize. No world-space state stored.
//
// Caller provides geo + in + current time from AnimationCore.
// =====================================================================
class BubblegumCableSlimeLayerCore
{
public:
    void paintLayers(juce::Graphics& g,
                     const BubblegumCableGeometry& geo,
                     const BubblegumCableInput&    in,
                     float time,
                     float energy) const
    {
        if (geo.ribbon.isEmpty()) return;

        const float e = juce::jlimit(0.f, 1.f, energy);
        const float seed = BubblegumCableAnimationCore::cableSeed(in);

        paintOuterAura    (g, geo, e);
        paintBodyBlobs    (g, geo, in, time, e, seed);
        paintEnergySweeps (g, geo, in, time, e);
        paintInnerGlow    (g, geo, e, time, seed);
    }

    // ── Utility: given a particle laneT + normalBias → world position ────
    static juce::Point<float> resolveParticlePos(
        const BubblegumCableGeometry& geo,
        float laneT, float normalBias) noexcept
    {
        const float clampedT = juce::jlimit(0.0f, 1.0f, laneT);
        const float fIdx = clampedT * (float)BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1,
                                    (int)std::floor(fIdx));
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a = fIdx - (float)i0;

        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];

        const float cx = juce::jmap(a, p0.x,  p1.x);
        const float cy = juce::jmap(a, p0.y,  p1.y);
        float nx = juce::jmap(a, p0.nx, p1.nx);
        float ny = juce::jmap(a, p0.ny, p1.ny);
        const float tw = juce::jmap(a, p0.topW,    p1.topW);
        const float bw = juce::jmap(a, p0.bottomW, p1.bottomW);

        const float nLen = std::sqrt(nx * nx + ny * ny);
        if (nLen > 1e-4f) { nx /= nLen; ny /= nLen; }
        else               { nx = 0.f; ny = -1.f; }

        const float innerR = (tw + bw) * 0.40f;
        const float bias   = juce::jlimit(-0.80f, 0.80f, normalBias);
        return { cx + nx * innerR * bias, cy + ny * innerR * bias };
    }

    // ── Paint a single liquid bead (particle) ────────────────────────────
    static void paintLiquidBead(juce::Graphics& g,
                                 juce::Point<float> pos,
                                 float halfW,
                                 float radius,
                                 float alpha,
                                 float energy) noexcept
    {
        const float e  = juce::jlimit(0.f, 1.f, energy);
        const float r  = juce::jmax(1.2f, radius * juce::jmax(1.0f, halfW * 0.18f));
        const float w  = r * (2.1f + e * 0.3f);
        const float h  = r * (1.45f + e * 0.2f);
        const float x  = pos.x - w * 0.5f;
        const float y  = pos.y - h * 0.5f;

        const auto fill  = juce::Colour(0xFFFF6FB0);
        const auto core  = juce::Colour(0xFFE04A8A);
        const auto edge  = juce::Colour(0xFF9B2055);

        juce::ColourGradient grad(
            core.withAlpha(alpha * (0.78f + e * 0.10f)), pos.x, pos.y - h * 0.22f,
            fill.withAlpha(alpha * (0.92f + e * 0.06f)), pos.x, pos.y + h * 0.44f,
            false);
        grad.addColour(0.55, fill.withAlpha(alpha * (0.85f + e * 0.05f)));
        g.setGradientFill(grad);
        g.fillEllipse(x, y, w, h);

        g.setColour(edge.withAlpha(alpha * 0.42f));
        g.drawEllipse(x, y, w, h, 0.5f);
        g.setColour(core.withAlpha(alpha * 0.28f));
        g.fillEllipse(pos.x - w * 0.16f, pos.y - h * 0.19f, w * 0.22f, h * 0.15f);
    }

private:
    // ── Palette ──────────────────────────────────────────────────────────
    static constexpr juce::uint32 kBodyPink   = 0xFFFF6FB0;
    static constexpr juce::uint32 kNeonPink   = 0xFFFF4FAC;
    static constexpr juce::uint32 kGlowPink   = 0xFFFF9AC6;
    static constexpr juce::uint32 kAura       = 0xFFFF75BE;
    static constexpr juce::uint32 kDeepMag    = 0xFF9B2055;
    static constexpr juce::uint32 kOcclu      = 0xFF420A1E;

    // ── Layer 4: outer aura ───────────────────────────────────────────────
    void paintOuterAura(juce::Graphics& g,
                        const BubblegumCableGeometry& geo,
                        float e) const
    {
        juce::Path expanded = geo.ribbon;
        expanded.applyTransform(juce::AffineTransform::translation(0.f, 1.2f));

        g.setColour(juce::Colour(kAura).withAlpha(0.07f + e * 0.04f));
        juce::PathStrokeType wide(6.5f, juce::PathStrokeType::curved,
                                  juce::PathStrokeType::rounded);
        // stroke the ribbon as an outline aura
        auto topC = makeSideContour(geo, +1.f);
        auto botC = makeSideContour(geo, -1.f);
        g.setColour(juce::Colour(kAura).withAlpha(0.11f + e * 0.05f));
        g.strokePath(topC, juce::PathStrokeType(4.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath(botC, juce::PathStrokeType(4.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── Layer 2: animated body blobs ─────────────────────────────────────
    void paintBodyBlobs(juce::Graphics& g,
                        const BubblegumCableGeometry& geo,
                        const BubblegumCableInput& in,
                        float time, float e, float seed) const
    {
        const int blobCount = 4 + (int)std::floor(
            BubblegumCableAnimationCore::stable01(seed + 1.7f) * 3.f);

        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(geo.ribbon);

        for (int i = 0; i < blobCount; ++i)
        {
            const float laneT = BubblegumCableAnimationCore::blobLaneT(
                time, seed, i, blobCount, 1.6f);
            const auto  s     = sampleAlong(geo, laneT);
            const float off   = (BubblegumCableAnimationCore::stable01(seed + (float)i * 4.3f) - 0.26f)
                              * s.halfW * 0.32f;
            const auto  c     = juce::Point<float>(
                s.pos.x + s.normal.x * off,
                s.pos.y + s.normal.y * off);
            const float ang = std::atan2(s.tangent.y, s.tangent.x);
            const float w   = juce::jmax(7.f, s.halfW * (1.9f +
                              BubblegumCableAnimationCore::stable01(seed + (float)i * 2.2f) * 1.6f));
            const float h   = juce::jmax(2.8f, s.halfW * (0.95f +
                              BubblegumCableAnimationCore::stable01(seed + (float)i * 6.8f) * 1.0f));

            // Pulse amplitude for each blob
            const float pulse = BubblegumCableAnimationCore::pulseSin(
                time, BubblegumCableAnimationCore::stable01(seed + (float)i * 3.3f)
                    * juce::MathConstants<float>::twoPi);
            const float alpha = 0.28f + pulse * 0.10f + e * 0.07f;

            fillSoftBlob(g, c, w, h, ang,
                juce::Colour(kBodyPink).withAlpha(alpha),
                juce::Colour(kDeepMag).withAlpha(alpha * 0.55f));
        }
    }

    // ── Layer 3: energy sweeps (fast gloss streaks) ───────────────────────
    void paintEnergySweeps(juce::Graphics& g,
                           const BubblegumCableGeometry& geo,
                           const BubblegumCableInput& in,
                           float time, float e) const
    {
        // 2 fast sweeps along the cable
        for (int i = 0; i < 2; ++i)
        {
            const float phase  = BubblegumCableAnimationCore::sweepLaneT(
                time, i, 2.2f);
            const auto  s      = sampleAlong(geo, phase);
            const float angle  = std::atan2(s.tangent.y, s.tangent.x);
            const auto  center = juce::Point<float>(
                s.pos.x + s.normal.x * s.halfW * 0.45f,
                s.pos.y + s.normal.y * s.halfW * 0.45f);
            const float width  = juce::jmax(12.f, s.halfW * (5.0f + (float)i * 0.8f));
            const float height = juce::jmax(2.2f, s.halfW * (0.55f + (float)i * 0.09f));
            const float alpha  = (0.14f + e * 0.06f) * (0.90f - (float)i * 0.20f);
            fillSoftBlob(g, center, width, height, angle,
                juce::Colour(kNeonPink).withAlpha(alpha),
                juce::Colour(kNeonPink).withAlpha(0.f));
        }
    }

    // ── Layer 1: inner glow core ──────────────────────────────────────────
    void paintInnerGlow(juce::Graphics& g,
                        const BubblegumCableGeometry& geo,
                        float e, float time, float seed) const
    {
        const float pulse = BubblegumCableAnimationCore::pulseSin(
            time, seed * 0.31f);
        const float glowAlpha = 0.30f + pulse * 0.14f + e * 0.10f;

        const auto core = makeCenterPath(geo);

        // Outer glow
        g.setColour(juce::Colour(kDeepMag).withAlpha(glowAlpha * 0.6f));
        g.strokePath(core, juce::PathStrokeType(
            juce::jmax(3.5f, geo.thickness * 0.40f),
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

        // Inner bright core line
        g.setColour(juce::Colour(kGlowPink).withAlpha(glowAlpha));
        g.strokePath(core, juce::PathStrokeType(
            juce::jmax(1.4f, geo.thickness * 0.16f),
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    // ── Helpers ───────────────────────────────────────────────────────────
    struct SampleResult
    {
        juce::Point<float> pos, tangent, normal;
        float halfW = 0.f;
    };

    static SampleResult sampleAlong(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float ct = juce::jlimit(0.f, 1.f, t);
        const float fi = ct * (float)BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)std::floor(fi));
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a = fi - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];

        SampleResult s;
        s.pos.x    = juce::jmap(a, p0.x,  p1.x);
        s.pos.y    = juce::jmap(a, p0.y,  p1.y);
        s.normal.x = juce::jmap(a, p0.nx, p1.nx);
        s.normal.y = juce::jmap(a, p0.ny, p1.ny);
        s.halfW    = juce::jmap(a, p0.topW, p1.topW);

        const float nLen = std::sqrt(s.normal.x * s.normal.x + s.normal.y * s.normal.y);
        if (nLen > 1e-4f) { s.normal.x /= nLen; s.normal.y /= nLen; }
        else              { s.normal = { 0.f, -1.f }; }
        s.tangent = { -s.normal.y, s.normal.x };
        return s;
    }

    static juce::Path makeCenterPath(const BubblegumCableGeometry& geo)
    {
        juce::Path path;
        path.startNewSubPath(geo.points[0].x, geo.points[0].y);
        for (int i = 1; i <= BubblegumCableGeometry::kSeg; ++i)
            path.lineTo(geo.points[i].x, geo.points[i].y);
        return path;
    }

    static juce::Path makeSideContour(const BubblegumCableGeometry& geo, float side)
    {
        juce::Path path;
        const auto& first = geo.points[0];
        const float w0 = (side > 0.f ? first.topW : first.bottomW);
        path.startNewSubPath(first.x + first.nx * side * w0,
                             first.y + first.ny * side * w0);
        for (int i = 1; i <= BubblegumCableGeometry::kSeg; ++i)
        {
            const auto& pt = geo.points[i];
            const float w  = (side > 0.f ? pt.topW : pt.bottomW);
            path.lineTo(pt.x + pt.nx * side * w,
                        pt.y + pt.ny * side * w);
        }
        return path;
    }

    static void fillSoftBlob(juce::Graphics& g,
                              juce::Point<float> center,
                              float width, float height, float angle,
                              juce::Colour inner, juce::Colour outer)
    {
        juce::Path blob;
        blob.addEllipse(-width * 0.5f, -height * 0.5f, width, height);
        blob.applyTransform(
            juce::AffineTransform::rotation(angle).translated(center.x, center.y));
        juce::ColourGradient grad(
            inner, center.x, center.y,
            outer, center.x + std::cos(angle) * width  * 0.55f,
                   center.y + std::sin(angle) * height * 0.55f,
            true);
        grad.addColour(0.60, inner.withMultipliedAlpha(0.42f));
        grad.addColour(1.00, outer);
        g.setGradientFill(grad);
        g.fillPath(blob);
    }
};

} // namespace DAW
