#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeGlossCore
//
// Renders 4-6 independent animated gloss streaks that slide along the
// cable surface. Each streak is a narrow oriented ellipse on the upper
// portion of the cable body, like light reflecting off wet slime.
//
// Reference look: multiple small bright gloss spots + one long gloss
// band travelling through the slime body.
// =====================================================================
class BubblegumCableSlimeGlossCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time,
               float                         energy) const
    {
        const float e    = juce::jlimit(0.f, 1.f, energy);
        const float seed = BubblegumCableAnimationCore::cableSeed(in);

        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        // ── Long primary gloss band ────────────────────────────────────
        {
            const float t = BubblegumCableAnimationCore::sweepLaneT(time, 0, 1.4f);
            paintGlossStreak(g, geo, t, seed, 0.78f, 0.0f, 3.0f, 0.26f + e * 0.06f);
        }

        // ── Secondary gloss band ───────────────────────────────────────
        {
            const float t = BubblegumCableAnimationCore::sweepLaneT(time + 2.3f, 1, 1.1f);
            paintGlossStreak(g, geo, t, seed + 1.0f, 0.55f, 0.0f, 2.0f, 0.18f + e * 0.04f);
        }

        // ── 3-4 small static gloss spots ──────────────────────────────
        const int spotCount = 3 + (int)(BubblegumCableAnimationCore::stable01(seed + 7.1f) * 2.f);
        for (int i = 0; i < spotCount; ++i)
        {
            const float si    = seed + (float)i * 4.9f;
            const float baseT = juce::jmap((float)(i + 1) / (float)(spotCount + 1), 0.14f, 0.86f);
            const float pulse = 0.72f + 0.28f * std::sin(
                time * (0.9f + 0.1f * (float)i)
                + BubblegumCableAnimationCore::stable01(si) * juce::MathConstants<float>::twoPi);
            paintGlossSpot(g, geo, baseT, seed + (float)i * 2.1f, 0.16f * pulse + e * 0.04f);
        }
    }

private:
    const juce::Colour kGloss   { 0xFFFFF8FC };
    const juce::Colour kLitPink { 0xFFFFE0F0 };

    void paintGlossStreak(juce::Graphics& g,
                          const BubblegumCableGeometry& geo,
                          float t, float seed,
                          float normalFrac,
                          float /*unused*/,
                          float widthMul,
                          float alpha) const
    {
        const auto s = sampleAlong(geo, t);
        const float ang   = std::atan2(s.ty, s.tx);
        const float w     = juce::jmax(12.f, s.halfW * (4.5f * widthMul));
        const float h     = juce::jmax(1.4f, s.halfW * 0.38f);
        const float cx    = s.x + s.nx * s.halfW * normalFrac;
        const float cy    = s.y + s.ny * s.halfW * normalFrac;

        juce::Path blob;
        blob.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
        blob.applyTransform(juce::AffineTransform::rotation(ang).translated(cx, cy));

        juce::ColourGradient grad(
            kGloss.withAlpha(alpha), cx, cy,
            kGloss.withAlpha(0.f),
            cx + std::cos(ang) * w * 0.52f,
            cy + std::sin(ang) * h * 0.52f, true);
        g.setGradientFill(grad);
        g.fillPath(blob);
    }

    void paintGlossSpot(juce::Graphics& g,
                        const BubblegumCableGeometry& geo,
                        float t, float seed,
                        float alpha) const
    {
        const auto s = sampleAlong(geo, t);
        const float r  = juce::jmax(2.2f, s.halfW * 0.32f);
        const float cx = s.x + s.nx * s.halfW * 0.60f;
        const float cy = s.y + s.ny * s.halfW * 0.60f;

        juce::ColourGradient grad(
            kGloss.withAlpha(alpha), cx, cy,
            kGloss.withAlpha(0.f),   cx, cy + r * 2.2f, true);
        g.setGradientFill(grad);
        g.fillEllipse(cx - r, cy - r * 0.6f, r * 2.0f, r * 1.2f);
    }

    struct S { float x, y, nx, ny, tx, ty, halfW; };

    static S sampleAlong(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float fi = juce::jlimit(0.f, 1.f, t) * (float)BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)std::floor(fi));
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a = fi - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        float nx = juce::jmap(a, p0.nx, p1.nx);
        float ny = juce::jmap(a, p0.ny, p1.ny);
        const float nl = std::sqrt(nx * nx + ny * ny);
        if (nl > 1e-4f) { nx /= nl; ny /= nl; } else { nx = 0; ny = -1; }
        S s;
        s.x  = juce::jmap(a, p0.x, p1.x);
        s.y  = juce::jmap(a, p0.y, p1.y);
        s.nx = nx; s.ny = ny;
        s.tx = -ny; s.ty = nx;
        s.halfW = juce::jmap(a, p0.topW, p1.topW);
        return s;
    }
};

} // namespace DAW
