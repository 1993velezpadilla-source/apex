#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeFillCore — animated slime interior fill
//
// Fills the cable interior with layered animated slime texture:
//
//   1. SLOW HEAVE — large slow gradient blobs that drift through the body,
//      simulating viscous internal flow / gel movement.
//
//   2. VEIN LINES — thin darker lines that run roughly parallel to the
//      cable axis, like internal cell walls or slime texture veining.
//
//   3. INTERNAL BRIGHTNESS PULSE — a periodic brightness wave that
//      travels along the cable, like a bioluminescent pulse.
//
// DYNAMIC: call every frame, not into static cache.
// =====================================================================
class BubblegumCableSlimeFillCore
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

        paintSlowHeave        (g, geo, time, e, seed);
        paintVeinLines        (g, geo, time, e, seed);
        paintBrightnessPulse  (g, geo, time, e);
    }

private:
    const juce::Colour kHeave  { 0xFFFF6FB0 };
    const juce::Colour kVein   { 0xFF9B2055 };
    const juce::Colour kPulse  { 0xFFFFD4EC };

    void paintSlowHeave(juce::Graphics& g,
                        const BubblegumCableGeometry& geo,
                        float time, float e, float seed) const
    {
        for (int i = 0; i < 3; ++i)
        {
            const float si     = seed + (float)i * 7.3f;
            const float phOff  = BubblegumCableAnimationCore::stable01(si)
                               * juce::MathConstants<float>::twoPi;
            const float t      = BubblegumCableAnimationCore::frac(
                time * (0.015f + 0.004f * (float)i) + (float)i * 0.28f);
            const auto  s      = sampleAlong(geo, t);
            const float ang    = std::atan2(s.ty, s.tx);
            const float w      = juce::jmax(18.f, s.hw * (5.0f + (float)i * 1.2f));
            const float h      = juce::jmax(3.5f, s.hw * (1.1f + (float)i * 0.2f));
            const float breathe = 1.f + 0.12f * std::sin(time * 0.8f + phOff);
            const float alpha  = (0.08f + e * 0.04f) * breathe;

            juce::Path blob;
            blob.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
            blob.applyTransform(juce::AffineTransform::rotation(ang)
                .translated(s.x, s.y));

            juce::ColourGradient grad(
                kHeave.withAlpha(alpha), s.x, s.y,
                kHeave.withAlpha(0.f),
                s.x + std::cos(ang) * w * 0.52f,
                s.y + std::sin(ang) * h * 0.52f, true);
            g.setGradientFill(grad);
            g.fillPath(blob);
        }
    }

    void paintVeinLines(juce::Graphics& g,
                        const BubblegumCableGeometry& geo,
                        float time, float e, float seed) const
    {
        const int lineCount = 2;
        for (int li = 0; li < lineCount; ++li)
        {
            const float normalOffset = (li == 0 ? 0.22f : -0.18f)
                + 0.06f * std::sin(time * 0.4f + seed * 3.1f + (float)li);

            juce::Path vein;
            bool started = false;
            for (int i = 0; i <= BubblegumCableGeometry::kSeg; ++i)
            {
                const auto& p = geo.points[(size_t)i];
                const float hw = juce::jmax(p.topW, p.bottomW);
                const float px = p.x + p.nx * hw * normalOffset;
                const float py = p.y + p.ny * hw * normalOffset;
                if (!started) { vein.startNewSubPath(px, py); started = true; }
                else            vein.lineTo(px, py);
            }
            g.setColour(kVein.withAlpha(0.10f + e * 0.03f));
            g.strokePath(vein, juce::PathStrokeType(0.6f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void paintBrightnessPulse(juce::Graphics& g,
                              const BubblegumCableGeometry& geo,
                              float time, float e) const
    {
        const float t   = BubblegumCableAnimationCore::frac(time * 0.06f);
        const auto  s   = sampleAlong(geo, t);
        const float ang = std::atan2(s.ty, s.tx);
        const float w   = juce::jmax(22.f, s.hw * 6.5f);
        const float h   = juce::jmax(4.0f, s.hw * 1.4f);

        juce::Path pulse;
        pulse.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
        pulse.applyTransform(juce::AffineTransform::rotation(ang).translated(s.x, s.y));

        juce::ColourGradient grad(
            kPulse.withAlpha(0.18f + e * 0.06f), s.x, s.y,
            kPulse.withAlpha(0.f),
            s.x + std::cos(ang) * w * 0.5f,
            s.y + std::sin(ang) * h * 0.5f, true);
        g.setGradientFill(grad);
        g.fillPath(pulse);
    }

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
