#pragma once
#include "BubblegumCableTypes.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableRenderCore -- fully opaque 2.5D cylindrical tube
//
// Zero transparency on the cable body. The cylindrical illusion is
// achieved by a single fully-opaque multi-stop gradient fill so the
// tube is always 100% solid -- never see-through.
//
//   1. Drop shadow  (semi-transparent, cast below -- outside body, ok)
//   2. Opaque cylindrical gradient fill  (one pass, no holes)
//   3. Top specular strip  (solid bright pink/white line)
//   4. Solid traveling specular ellipse  (no fade-to-transparent)
//   5. Bottom occlusion rim  (solid dark)
//   6. Thin outline  (solid)
//   7. Glossy endpoint balls  (solid)
// =====================================================================
class BubblegumCableRenderCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput& in,
               float) const
    {
        if (geo.ribbon.isEmpty()) return;
        if (!in.shouldDraw()) return;  // graph says: no route exists

        const float graphAlpha = juce::jlimit(0.f, 1.f, in.alphaMult);
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float tNow = (float)(juce::Time::getMillisecondCounterHiRes() * 0.001);
        const auto  b    = getRibbonBounds(geo);
        const float cx   = b.getCentreX();

        // ── 1. Drop shadow (outside body -- semi-transparent is fine here) ──
        {
            juce::Path sh = geo.ribbon;
            sh.applyTransform(juce::AffineTransform::translation(0.f, 6.f));
            g.setColour(juce::Colour(0x66000000).withMultipliedAlpha(graphAlpha));
            g.fillPath(sh);
            sh.applyTransform(juce::AffineTransform::translation(0.f, 4.f));
            g.setColour(juce::Colour(0x33000000).withMultipliedAlpha(graphAlpha));
            g.fillPath(sh);
        }

        // ── 2. Fully-opaque cylindrical gradient (one pass, no transparent stops) ──
        {
            // For inactive cables, reduce opacity of the entire body via a
            // post-fill alpha rect rather than touching each gradient stop.
            juce::ColourGradient cyl(
                kSpecular,   cx, b.getY(),
                kOcclude,    cx, b.getBottom(), false);
            cyl.addColour(0.08, kHighlight);
            cyl.addColour(0.20, kLit);
            cyl.addColour(0.42, kBody);
            cyl.addColour(0.68, kShadow);
            cyl.addColour(0.86, kOcclude);
            g.setGradientFill(cyl);
            g.fillPath(geo.ribbon);

            // Inactive-cable dimming: overlay a black rect at reduced alpha
            // so the graph state is visible without a separate render path.
            if (graphAlpha < 1.0f)
            {
                juce::Graphics::ScopedSaveState dimSt(g);
                g.reduceClipRegion(geo.ribbon);
                g.setColour(juce::Colours::black.withAlpha(1.0f - graphAlpha));
                g.fillRect(b);
            }

            // Inner lit band -- still clipped, but uses a SOLID light colour
            // blended over the already-opaque fill via a second clip pass
            juce::Graphics::ScopedSaveState st(g);
            g.reduceClipRegion(geo.ribbon);
            juce::ColourGradient litBand(
                kSpecular,   cx, b.getY(),
                kLit,        cx, b.getY() + b.getHeight() * 0.18f, false);
            g.setGradientFill(litBand);
            g.fillRect(b.getX(), b.getY(), b.getWidth(), b.getHeight() * 0.19f);
        }

        // ── 3. Top specular strip -- solid, no fade ────────────────────────
        {
            auto topC = makeSideContour(geo, +1.f);
            const float pulse = 0.85f + 0.15f * std::sin(tNow * 1.8f + geo.sx * 0.011f);
            // Wide soft halo (semi -- outside the silhouette, cast outward)
            g.setColour(kLit.withAlpha(0.35f * pulse));
            g.strokePath(topC, juce::PathStrokeType(4.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Solid bright line on the crown
            g.setColour(kSpecular);
            g.strokePath(topC, juce::PathStrokeType(1.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── 4. Solid traveling specular ellipse ───────────────────────────
        {
            const float phase = std::fmod(tNow * 0.22f, 1.0f);
            const auto  s     = sampleAlong(geo, phase);
            const float ang   = std::atan2(s.tangent.y, s.tangent.x);
            const auto  ctr   = juce::Point<float>(
                s.pos.x + s.normal.x * s.halfW * 0.52f,
                s.pos.y + s.normal.y * s.halfW * 0.52f);
            const float w = juce::jmax(10.f, s.halfW * 3.8f);
            const float h = juce::jmax(2.5f, s.halfW * 0.70f);

            // Solid bright ellipse clipped to the ribbon
            juce::Graphics::ScopedSaveState st(g);
            g.reduceClipRegion(geo.ribbon);
            juce::Path blob;
            blob.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
            blob.applyTransform(
                juce::AffineTransform::rotation(ang).translated(ctr.x, ctr.y));
            // Solid gradient: kSpecular -> kHighlight (both fully opaque)
            juce::ColourGradient sg(
                kSpecular,   ctr.x, ctr.y,
                kHighlight,  ctr.x + w * 0.5f, ctr.y, true);
            g.setGradientFill(sg);
            g.fillPath(blob);
        }

        // ── 5. Bottom occlusion rim -- solid ──────────────────────────────
        {
            auto botC = makeSideContour(geo, -1.f);
            g.setColour(kShadow);
            g.strokePath(botC, juce::PathStrokeType(3.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(kOcclude);
            g.strokePath(botC, juce::PathStrokeType(1.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── 6. Outline -- solid ───────────────────────────────────────────
        g.setColour(kOcclude);
        g.strokePath(geo.ribbon, juce::PathStrokeType(1.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ── 7. Endpoint balls ─────────────────────────────────────────────
        paintEndpointBall(g, geo.sx, geo.sy, geo.thickness, e, tNow);
        paintEndpointBall(g, geo.tx, geo.ty, geo.thickness, e, tNow);
    }

private:
    // Fully-opaque palette -- no withAlpha() on body colors
    const juce::Colour kSpecular  { 0xFFFFEEF8 };  // near-white crown
    const juce::Colour kHighlight { 0xFFFFD6EC };  // lit top band
    const juce::Colour kLit       { 0xFFFF9AC8 };  // mid-lit face
    const juce::Colour kBody      { 0xFFEE4FA0 };  // saturated hot pink
    const juce::Colour kShadow    { 0xFF8C1848 };  // dark underside
    const juce::Colour kOcclude   { 0xFF2A0612 };  // deep contact shadow

    static juce::Rectangle<float> getRibbonBounds(const BubblegumCableGeometry& geo)
    {
        float mnX = geo.points[0].x, mxX = mnX;
        float mnY = geo.points[0].y, mxY = mnY;
        for (const auto& p : geo.points)
        {
            const float w = juce::jmax(p.topW, p.bottomW);
            mnX = juce::jmin(mnX, p.x - std::abs(p.nx) * w);
            mxX = juce::jmax(mxX, p.x + std::abs(p.nx) * w);
            mnY = juce::jmin(mnY, p.y - std::abs(p.ny) * w);
            mxY = juce::jmax(mxY, p.y + std::abs(p.ny) * w);
        }
        return { mnX, mnY, juce::jmax(1.f, mxX - mnX), juce::jmax(1.f, mxY - mnY) };
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

    struct Sample { juce::Point<float> pos, tangent, normal; float halfW = 0.f; };

    static Sample sampleAlong(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float ft = juce::jlimit(0.f, 1.f, t) * (float)BubblegumCableGeometry::kSeg;
        const int   i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)ft);
        const int   i1 = juce::jmin(BubblegumCableGeometry::kSeg, i0 + 1);
        const float a  = ft - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        Sample s;
        s.pos    = { juce::jmap(a, p0.x,  p1.x),  juce::jmap(a, p0.y,  p1.y) };
        s.normal = { juce::jmap(a, p0.nx, p1.nx),  juce::jmap(a, p0.ny, p1.ny) };
        s.halfW  = juce::jmap(a, p0.topW, p1.topW);
        const float nl = std::sqrt(s.normal.x * s.normal.x + s.normal.y * s.normal.y);
        if (nl > 1e-4f) { s.normal.x /= nl; s.normal.y /= nl; }
        else            { s.normal = { 0.f, -1.f }; }
        s.tangent = { -s.normal.y, s.normal.x };
        return s;
    }

    void paintEndpointBall(juce::Graphics& g,
                           float cx, float cy,
                           float thickness, float e, float tNow) const
    {
        const float r = juce::jmax(7.f, thickness * 0.78f);

        // Shadow (outside ball silhouette -- semi-transparent ok)
        g.setColour(juce::Colour(0x55000000));
        g.fillEllipse(cx - r, cy - r * 0.8f + 5.f, r * 2.f, r * 1.6f);

        // Fully-opaque cylindrical ball body
        {
            juce::ColourGradient grad(
                kSpecular,  cx, cy - r * 0.55f,
                kOcclude,   cx, cy + r, false);
            grad.addColour(0.28, kHighlight);
            grad.addColour(0.50, kBody);
            grad.addColour(0.74, kShadow);
            g.setGradientFill(grad);
            g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
        }

        // Solid specular glint (no transparency)
        const float pulse = 0.82f + 0.18f * std::sin(tNow * 2.4f + cx * 0.02f);
        const float glintW = r * (0.52f + e * 0.08f) * pulse;
        const float glintH = r * (0.30f + e * 0.04f) * pulse;
        juce::ColourGradient glint(
            kSpecular,   cx - glintW * 0.2f, cy - r * 0.58f,
            kHighlight,  cx + glintW * 0.5f, cy - r * 0.25f, false);
        g.setGradientFill(glint);
        g.fillEllipse(cx - glintW * 0.5f, cy - r * 0.65f, glintW, glintH);

        // Solid outline
        g.setColour(kOcclude);
        g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.2f);
    }
};

} // namespace DAW
