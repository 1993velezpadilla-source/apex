#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>
#include <array>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeFlowCore — traveling bolus beads source→target loop
//
// Renders 3-5 gel boluses (rounded blobs) that travel continuously from
// the source endpoint along the cable to the target endpoint, then wrap
// back to source. Like fluid pumping through a transparent tube.
//
// Each bolus:
//   - Has a unique phase offset so they're evenly spaced
//   - Scales with halfW at its current t position
//   - Has a glossy cap and inner glow
//   - Moves at a speed proportional to sendEnergy
//   - Trails a subtle wake (darker zone behind it)
//   - Is clipped inside the cable ribbon (stays inside)
//
// IMPORTANT: This is a DYNAMIC layer — paint it every frame, NOT into
// the static cache. The System's dynamic pass calls this.
// =====================================================================
class BubblegumCableSlimeFlowCore
{
public:
    static constexpr int kMaxBoluses = 5;

    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time,
               float                         energy) const
    {
        if (geo.ribbon.isEmpty()) return;

        const float e     = juce::jlimit(0.f, 1.f, energy);
        const float seed  = BubblegumCableAnimationCore::cableSeed(in);
        const float speed = 0.08f + e * 0.14f;
        const int   count = 3 + (int)(BubblegumCableAnimationCore::stable01(seed + 0.1f) * 3.f);

        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        for (int i = 0; i < count; ++i)
        {
            // Each bolus has a different starting phase so they're spread apart
            const float phaseOffset = (float)i / (float)count;
            const float t = BubblegumCableAnimationCore::frac(time * speed + phaseOffset);

            const auto s = sampleAlong(geo, t);

            // Size is proportional to cable thickness at this point
            const float r  = juce::jmax(2.5f, s.hw * (0.55f + e * 0.12f));
            const float rx = r * 1.35f;   // elongated along cable axis
            const float ry = r * 0.75f;
            const float ang = std::atan2(s.ty, s.tx);

            // Centre slightly toward upper normal (inside the body, upper half)
            const float cx = s.x + s.nx * s.hw * 0.10f;
            const float cy = s.y + s.ny * s.hw * 0.10f;

            // ── Wake (darker smear behind the bolus) ──────────────────────
            const float wakeT = BubblegumCableAnimationCore::frac(t - 0.025f);
            const auto  ws    = sampleAlong(geo, wakeT);
            const float wCx   = ws.x + ws.nx * ws.hw * 0.10f;
            const float wCy   = ws.y + ws.ny * ws.hw * 0.10f;
            juce::Path wake;
            wake.addEllipse(-rx * 2.0f, -ry * 0.6f, rx * 4.0f, ry * 1.2f);
            wake.applyTransform(juce::AffineTransform::rotation(ang).translated(wCx, wCy));
            g.setColour(kWake.withAlpha(0.12f + e * 0.04f));
            g.fillPath(wake);

            // ── Bolus body ────────────────────────────────────────────────
            juce::Path blob;
            blob.addEllipse(-rx, -ry, rx * 2.0f, ry * 2.0f);
            blob.applyTransform(juce::AffineTransform::rotation(ang).translated(cx, cy));

            juce::ColourGradient grad(
                kBolusLit.withAlpha(0.78f + e * 0.08f), cx, cy - ry * 0.3f,
                kBolusBot.withAlpha(0.72f + e * 0.06f), cx, cy + ry * 0.8f, false);
            grad.addColour(0.45, kBolusBody.withAlpha(0.75f));
            g.setGradientFill(grad);
            g.fillPath(blob);

            // ── Rim ───────────────────────────────────────────────────────
            g.setColour(kBolusRim.withAlpha(0.30f));
            g.strokePath(blob, juce::PathStrokeType(0.6f));

            // ── Gloss cap (top-left of bolus) ─────────────────────────────
            const float gcx = cx + std::cos(ang - 0.5f) * rx * 0.38f
                                 + s.nx * s.hw * 0.22f;
            const float gcy = cy + std::sin(ang - 0.5f) * ry * 0.38f
                                 + s.ny * s.hw * 0.22f;
            juce::ColourGradient gloss(
                kGloss.withAlpha(0.60f), gcx, gcy,
                kGloss.withAlpha(0.00f), gcx, gcy + ry * 1.6f, true);
            g.setGradientFill(gloss);
            g.fillEllipse(gcx - rx * 0.28f, gcy - ry * 0.28f, rx * 0.56f, ry * 0.56f);
        }
    }

private:
    const juce::Colour kBolusBody { 0xFFFF6FB0 };
    const juce::Colour kBolusLit  { 0xFFFFCCE4 };
    const juce::Colour kBolusBot  { 0xFFCC3380 };
    const juce::Colour kBolusRim  { 0xFF8A1A44 };
    const juce::Colour kGloss     { 0xFFFFF8FC };
    const juce::Colour kWake      { 0xFF9B2055 };

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
