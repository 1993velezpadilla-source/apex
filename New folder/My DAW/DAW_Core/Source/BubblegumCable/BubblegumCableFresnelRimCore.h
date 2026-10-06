#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableFresnelRimCore  -- translucent gel Fresnel rim lighting
//
// In real 3D rendering, translucent gel/slime materials produce a strong
// rim glow at grazing angles -- the surface brightens dramatically where
// the viewer's angle is shallow relative to the surface normal.
//
// This nucleus approximates that effect in 2D using each cable point's
// normal vector: points where the normal faces mostly sideways (|nx| is
// large = the tube face is nearly edge-on to the viewer) get a bright
// rim contribution. Points facing directly up (|ny| large) get less.
//
// Result: the cable edges glow brightly like a neon gel tube, while
// the top face stays clean. This is the signature look of premium
// silicone / slime materials under studio lighting.
//
// Drawn AFTER the body and overlays, BEFORE shimmer.
// =====================================================================
class BubblegumCableFresnelRimCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float pulse = 0.82f + 0.18f * std::sin(time * 1.4f + geo.sx * 0.009f);

        // ── Top rim: Fresnel glow on upper edge ────────────────────────────
        // Normal facing up = crown of tube = specular Fresnel catch
        {
            auto topC = makeSideContour(geo, +1.f);

            // Wide diffuse Fresnel halo
            g.setColour(kFresnelTop.withAlpha((0.20f + e * 0.08f) * pulse));
            g.strokePath(topC, juce::PathStrokeType(4.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Tight hot line
            g.setColour(kFresnelHot.withAlpha((0.60f + e * 0.12f) * pulse));
            g.strokePath(topC, juce::PathStrokeType(0.7f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── Per-segment lateral rim (grazing angle = |nx| contribution) ───
        // Build two sub-paths: left-facing and right-facing segments.
        // For each segment, compute Fresnel intensity = |nx| (how side-on it is).
        // Only paint segments above a threshold to avoid flat-top noise.
        {
            juce::Graphics::ScopedSaveState st(g);

            constexpr int N = BubblegumCableGeometry::kSeg;
            for (int i = 0; i < N; ++i)
            {
                const auto& p0 = geo.points[(size_t)i];
                const auto& p1 = geo.points[(size_t)(i + 1)];

                // Fresnel intensity: how much the normal faces sideways
                const float fresnel = std::abs(p0.nx);
                if (fresnel < 0.08f) continue;  // skip near-vertical normals

                const float alpha = fresnel * fresnel * (0.28f + e * 0.10f) * pulse;

                // Top edge of this segment
                const float tx0 = p0.x + p0.nx * p0.topW;
                const float ty0 = p0.y + p0.ny * p0.topW;
                const float tx1 = p1.x + p1.nx * p1.topW;
                const float ty1 = p1.y + p1.ny * p1.topW;
                // Bottom edge
                const float bx0 = p0.x - p0.nx * p0.bottomW;
                const float by0 = p0.y - p0.ny * p0.bottomW;
                const float bx1 = p1.x - p1.nx * p1.bottomW;
                const float by1 = p1.y - p1.ny * p1.bottomW;

                // Left rim (left side of tube in screen space)
                juce::Path leftSeg;
                leftSeg.startNewSubPath(tx0, ty0);
                leftSeg.lineTo(tx1, ty1);
                g.setColour(kFresnelSide.withAlpha(alpha));
                g.strokePath(leftSeg, juce::PathStrokeType(1.4f));

                // Right / bottom rim
                juce::Path rightSeg;
                rightSeg.startNewSubPath(bx0, by0);
                rightSeg.lineTo(bx1, by1);
                g.strokePath(rightSeg, juce::PathStrokeType(1.0f));
            }
        }

        // ── Endpoint ball Fresnel ring ─────────────────────────────────────
        paintBallFresnel(g, geo.sx, geo.sy, geo.thickness, e, pulse);
        paintBallFresnel(g, geo.tx, geo.ty, geo.thickness, e, pulse);
    }

private:
    const juce::Colour kFresnelTop  { 0xFFFFEEF8 };  // near-white crown
    const juce::Colour kFresnelHot  { 0xFFFFFFFF };  // pure white tight line
    const juce::Colour kFresnelSide { 0xFFFFB8DC };  // pink side rim

    void paintBallFresnel(juce::Graphics& g,
                          float cx, float cy, float thickness,
                          float e, float pulse) const
    {
        const float r = juce::jmax(7.f, thickness * 0.78f);
        // Bright ring around ball edge (Fresnel at ball surface)
        g.setColour(kFresnelSide.withAlpha((0.35f + e * 0.12f) * pulse));
        g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.5f);
        g.setColour(kFresnelHot.withAlpha((0.20f + e * 0.06f) * pulse));
        g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 0.5f);
    }

    static juce::Path makeSideContour(const BubblegumCableGeometry& geo, float side)
    {
        juce::Path path;
        const auto& f = geo.points[0];
        path.startNewSubPath(f.x + f.nx * side * (side > 0.f ? f.topW : f.bottomW),
                             f.y + f.ny * side * (side > 0.f ? f.topW : f.bottomW));
        for (int i = 1; i <= BubblegumCableGeometry::kSeg; ++i)
        {
            const auto& pt = geo.points[i];
            const float w  = (side > 0.f ? pt.topW : pt.bottomW);
            path.lineTo(pt.x + pt.nx * side * w, pt.y + pt.ny * side * w);
        }
        return path;
    }
};

} // namespace DAW
