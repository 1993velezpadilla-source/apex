#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCable25DLightingCore — 2.5D premium lighting engine
//
// Simulates the following physical lighting phenomena on a 2D cable:
//
//   1. AMBIENT OCCLUSION SHADOW
//      Soft dark shadow cast below the cable onto the "surface", giving
//      the illusion that the cable floats above a surface.
//
//   2. PER-POINT NORMAL LIGHTING
//      Uses each cable point's surface normal to compute diffuse light
//      intensity from a fixed upper-left light source. Points facing the
//      light are brighter. Gives real 3D shading across the body.
//
//   3. SUBSURFACE SCATTERING (fake SSS)
//      Light that appears to pass through the gel from the inside.
//      Centre of cable is lit warmer/brighter than edges, simulating
//      translucent material like real slime / silicone gel.
//
//   4. RIM LIGHT
//      Thin bright line on the upper edge (light source side), as if
//      a studio key light grazes the top surface from upper-left.
//
//   5. SPECULAR HOTSPOT
//      Single animated bright white specular reflection spot that drifts
//      slowly along the cable top surface. Physically: specular lobe of
//      a Phong BRDF approximation.
//
//   6. DEPTH CUEING
//      The sag midpoint is slightly more saturated/darker as if farther
//      from the viewer. Endpoints are slightly brighter (closer).
//
//   7. CONTACT SHADOW LINE
//      Tight dark line on the bottom edge, simulating self-shadow where
//      the cable underside meets its own shadow.
//
// All operations are pure 2D path/gradient — no OpenGL required.
// =====================================================================
class BubblegumCable25DLightingCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               float                         time,
               float                         energy) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e = juce::jlimit(0.f, 1.f, energy);

        paintAOShadow        (g, geo, e);
        paintNormalShading   (g, geo);
        paintSSS             (g, geo, e);
        paintRimLight        (g, geo, time);
        paintSpecularHotspot (g, geo, time, e);
        paintDepthCueing     (g, geo);
        paintContactShadow   (g, geo);
    }

private:
    // ── Palette ──────────────────────────────────────────────────────────
    const juce::Colour kAOShadow   { 0x44000000 };
    const juce::Colour kRimLight   { 0xFFFFEEF8 };
    const juce::Colour kSpecular   { 0xFFFFFFFF };
    const juce::Colour kSSS        { 0xFFFF4FA8 };
    const juce::Colour kDepthDark  { 0x229B2055 };
    const juce::Colour kContactSh  { 0xFF2E0713 };

    // ── 1. Ambient occlusion shadow ───────────────────────────────────────
    void paintAOShadow(juce::Graphics& g,
                       const BubblegumCableGeometry& geo,
                       float e) const
    {
        juce::Path shadow = geo.ribbon;
        // Offset downward — shadow on surface below
        shadow.applyTransform(juce::AffineTransform::translation(1.5f, 4.5f));

        juce::Graphics::ScopedSaveState st(g);
        // Soft shadow: several passes with decreasing alpha
        for (int i = 0; i < 4; ++i)
        {
            juce::Path s2 = geo.ribbon;
            const float dy = 3.0f + (float)i * 2.2f;
            s2.applyTransform(juce::AffineTransform::translation(0.8f, dy));
            g.setColour(kAOShadow.withAlpha((0.22f - (float)i * 0.05f) + e * 0.04f));
            g.fillPath(s2);
        }
    }

    // ── 2. Per-point normal shading (diffuse from upper-left light) ───────
    void paintNormalShading(juce::Graphics& g,
                            const BubblegumCableGeometry& geo) const
    {
        // Light direction: upper-left, normalized
        constexpr float Lx = -0.577f, Ly = -0.816f;

        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        // Build path segments, coloured by NdotL
        for (int i = 0; i < BubblegumCableGeometry::kSeg; ++i)
        {
            const auto& p0 = geo.points[(size_t)i];
            const auto& p1 = geo.points[(size_t)(i + 1)];

            const float ndotl = juce::jlimit(0.f, 1.f,
                p0.nx * Lx + p0.ny * Ly);

            // Lit side: lighter pink overlay; shadow side: darker overlay
            const float alpha = (ndotl - 0.5f) * 0.28f;
            if (std::abs(alpha) < 0.005f) continue;

            juce::Path seg;
            seg.startNewSubPath(p0.x + p0.nx * p0.topW, p0.y + p0.ny * p0.topW);
            seg.lineTo         (p1.x + p1.nx * p1.topW, p1.y + p1.ny * p1.topW);
            seg.lineTo         (p1.x - p1.nx * p1.bottomW, p1.y - p1.ny * p1.bottomW);
            seg.lineTo         (p0.x - p0.nx * p0.bottomW, p0.y - p0.ny * p0.bottomW);
            seg.closeSubPath();

            if (alpha > 0.f)
                g.setColour(juce::Colours::white.withAlpha(alpha));
            else
                g.setColour(juce::Colours::black.withAlpha(-alpha));
            g.fillPath(seg);
        }
    }

    // ── 3. Subsurface scattering — warm glow from inside ─────────────────
    void paintSSS(juce::Graphics& g,
                  const BubblegumCableGeometry& geo,
                  float e) const
    {
        const auto core = makeCenterPath(geo);
        g.setColour(kSSS.withAlpha(0.14f + e * 0.06f));
        g.strokePath(core, juce::PathStrokeType(
            juce::jmax(3.0f, geo.thickness * 0.32f),
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── 4. Rim light — bright line on upper edge ──────────────────────────
    void paintRimLight(juce::Graphics& g,
                       const BubblegumCableGeometry& geo,
                       float time) const
    {
        auto topC = makeSideContour(geo, +1.f);
        const float pulse = 0.78f + 0.22f * std::sin(time * 1.6f + geo.sx * 0.01f);
        g.setColour(kRimLight.withAlpha(0.36f * pulse));
        g.strokePath(topC, juce::PathStrokeType(1.4f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Tight inner rim
        g.setColour(juce::Colours::white.withAlpha(0.20f * pulse));
        g.strokePath(topC, juce::PathStrokeType(0.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── 5. Specular hotspot ───────────────────────────────────────────────
    void paintSpecularHotspot(juce::Graphics& g,
                              const BubblegumCableGeometry& geo,
                              float time, float e) const
    {
        // Primary specular travels slowly along the cable
        const float phase1 = BubblegumCableAnimationCore::frac(time * 0.018f + 0.15f);
        const auto  s1     = sampleAlong(geo, phase1);
        const float ang1   = std::atan2(s1.ty, s1.tx);
        const float w1     = juce::jmax(8.f, s1.hw * 3.2f);
        const float h1     = juce::jmax(1.2f, s1.hw * 0.28f);
        const auto  c1     = juce::Point<float>(
            s1.x + s1.nx * s1.hw * 0.55f,
            s1.y + s1.ny * s1.hw * 0.55f);

        fillSpecBlob(g, c1, w1, h1, ang1, kSpecular.withAlpha(0.52f + e * 0.08f));

        // Secondary smaller specular, faster
        const float phase2 = BubblegumCableAnimationCore::frac(time * 0.034f + 0.62f);
        const auto  s2     = sampleAlong(geo, phase2);
        const float ang2   = std::atan2(s2.ty, s2.tx);
        const float w2     = juce::jmax(4.f, s2.hw * 1.8f);
        const float h2     = juce::jmax(0.8f, s2.hw * 0.18f);
        const auto  c2     = juce::Point<float>(
            s2.x + s2.nx * s2.hw * 0.58f,
            s2.y + s2.ny * s2.hw * 0.58f);

        fillSpecBlob(g, c2, w2, h2, ang2, kSpecular.withAlpha(0.34f + e * 0.06f));
    }

    // ── 6. Depth cueing ───────────────────────────────────────────────────
    void paintDepthCueing(juce::Graphics& g,
                          const BubblegumCableGeometry& geo) const
    {
        // Find the lowest point (maximum y = furthest from viewer in 2.5D)
        float maxY = geo.points[0].y;
        for (const auto& p : geo.points) maxY = juce::jmax(maxY, p.y);

        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        const auto b = getBounds(geo);
        // Dark desaturation at bottom of curve (further away)
        juce::ColourGradient depth(
            kDepthDark.withAlpha(0.0f),  b.getCentreX(), b.getY(),
            kDepthDark.withAlpha(0.20f), b.getCentreX(), b.getBottom(), false);
        g.setGradientFill(depth);
        g.fillRect(b);
    }

    // ── 7. Contact shadow line on underside ──────────────────────────────
    void paintContactShadow(juce::Graphics& g,
                            const BubblegumCableGeometry& geo) const
    {
        auto botC = makeSideContour(geo, -1.f);
        g.setColour(kContactSh.withAlpha(0.55f));
        g.strokePath(botC, juce::PathStrokeType(1.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(kContactSh.withAlpha(0.82f));
        g.strokePath(botC, juce::PathStrokeType(0.6f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── Helpers ───────────────────────────────────────────────────────────
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

    static void fillSpecBlob(juce::Graphics& g, juce::Point<float> c,
                              float w, float h, float ang, juce::Colour col)
    {
        juce::Path blob;
        blob.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
        blob.applyTransform(juce::AffineTransform::rotation(ang).translated(c.x, c.y));
        juce::ColourGradient grad(col, c.x, c.y, col.withAlpha(0.f),
            c.x + std::cos(ang) * w * 0.5f,
            c.y + std::sin(ang) * h * 0.5f, true);
        g.setGradientFill(grad);
        g.fillPath(blob);
    }

    static juce::Path makeCenterPath(const BubblegumCableGeometry& geo)
    {
        juce::Path p;
        p.startNewSubPath(geo.points[0].x, geo.points[0].y);
        for (int i = 1; i <= BubblegumCableGeometry::kSeg; ++i)
            p.lineTo(geo.points[i].x, geo.points[i].y);
        return p;
    }

    static juce::Path makeSideContour(const BubblegumCableGeometry& geo, float side)
    {
        juce::Path path;
        const auto& f = geo.points[0];
        const float w0 = side > 0.f ? f.topW : f.bottomW;
        path.startNewSubPath(f.x + f.nx * side * w0, f.y + f.ny * side * w0);
        for (int i = 1; i <= BubblegumCableGeometry::kSeg; ++i)
        {
            const auto& pt = geo.points[i];
            const float w = side > 0.f ? pt.topW : pt.bottomW;
            path.lineTo(pt.x + pt.nx * side * w, pt.y + pt.ny * side * w);
        }
        return path;
    }

    static juce::Rectangle<float> getBounds(const BubblegumCableGeometry& geo)
    {
        float mnX = geo.points[0].x, mxX = mnX, mnY = geo.points[0].y, mxY = mnY;
        for (const auto& p : geo.points)
        {
            const float w = juce::jmax(p.topW, p.bottomW);
            mnX = juce::jmin(mnX, p.x - w); mxX = juce::jmax(mxX, p.x + w);
            mnY = juce::jmin(mnY, p.y - w); mxY = juce::jmax(mxY, p.y + w);
        }
        return { mnX, mnY, juce::jmax(1.f, mxX - mnX), juce::jmax(1.f, mxY - mnY) };
    }
};

} // namespace DAW
