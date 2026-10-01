#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeBodyCore — THICK glossy slime body rendering.
//
// The cable ribbon is the DEAD ZONE (background line). This core draws
// thick, glossy, translucent slime OVER that guideline, producing the
// look from the reference images: rounded glossy top lip, heavy dark
// underside, and strong internal lit-centre.
//
// No world-space state. All positions derived from geo points + time.
// =====================================================================
class BubblegumCableSlimeBodyCore
{
public:
    void paintBody(juce::Graphics& g,
                   const BubblegumCableGeometry& geo,
                   float time,
                   float energy) const
    {
        if (geo.ribbon.isEmpty()) return;

        const float e = juce::jlimit(0.f, 1.f, energy);

        // ── 1. Outer diffuse glow (aura around the body) ─────────────────
        {
            auto topC = makeSideContour(geo, +1.f);
            auto botC = makeSideContour(geo, -1.f);
            g.setColour(kAura.withAlpha(0.09f + e * 0.04f));
            g.strokePath(topC, juce::PathStrokeType(8.f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.strokePath(botC, juce::PathStrokeType(8.f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── 2. Opaque base fill — guaranteed visible ──────────────────────
        g.setColour(kBody);
        g.fillPath(geo.ribbon);

        // ── 3. 2.5D cylindrical gradient (top bright, bottom dark) ────────
        {
            const auto b = getClipBounds(geo);
            juce::ColourGradient grad(
                kLitTop.withAlpha(0.90f), b.getCentreX(), b.getY(),
                kOcclu .withAlpha(0.88f), b.getCentreX(), b.getBottom(),
                false);
            grad.addColour(0.08, kLitTop.withAlpha(0.78f));
            grad.addColour(0.22, kBody  .withAlpha(0.95f));
            grad.addColour(0.52, kBody  .withAlpha(0.92f));
            grad.addColour(0.72, kShadow.withAlpha(0.94f));
            grad.addColour(0.90, kDeepMag.withAlpha(0.96f));
            g.setGradientFill(grad);
            g.fillPath(geo.ribbon);
        }

        // ── 4. Clipped internal bands for 2.5D roundness ─────────────────
        {
            juce::Graphics::ScopedSaveState st(g);
            g.reduceClipRegion(geo.ribbon);
            const auto b = getClipBounds(geo);
            const float x = b.getX(), y = b.getY();
            const float w = b.getWidth(), h = b.getHeight();

            // Upper specular strip — wide soft halo
            juce::ColourGradient topBand(
                kLitTop.withAlpha(0.42f), b.getCentreX(), y,
                kBody.withAlpha(0.0f),   b.getCentreX(), y + h * 0.32f, false);
            topBand.addColour(0.14, kLitTop.withAlpha(0.28f));
            g.setGradientFill(topBand);
            g.fillRect(x, y, w, h * 0.34f);

            // Tight specular line — makes it read as glossy
            juce::ColourGradient hotLine(
                kGloss.withAlpha(0.55f), b.getCentreX(), y + h * 0.04f,
                kGloss.withAlpha(0.0f),  b.getCentreX(), y + h * 0.14f, false);
            g.setGradientFill(hotLine);
            g.fillRect(x, y, w, h * 0.15f);

            // Lower occlusion shadow — heavy dark bottom
            juce::ColourGradient botBand(
                kDeepMag.withAlpha(0.0f),  b.getCentreX(), y + h * 0.48f,
                kOcclu  .withAlpha(0.78f), b.getCentreX(), b.getBottom(), false);
            botBand.addColour(0.42, kShadow.withAlpha(0.22f));
            botBand.addColour(0.78, kDeepMag.withAlpha(0.55f));
            g.setGradientFill(botBand);
            g.fillRect(x, y + h * 0.46f, w, h * 0.54f);
        }

        // ── 5. Animated energy core (inner lit center) ────────────────────
        {
            const float pulse = 0.82f + 0.18f * std::sin(time * 1.8f + geo.sx * 0.012f);
            const auto core = makeCenterPath(geo);
            g.setColour(kDeepMag.withAlpha((0.28f + e * 0.10f) * pulse));
            g.strokePath(core, juce::PathStrokeType(
                juce::jmax(4.0f, geo.thickness * 0.42f),
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(kGlow.withAlpha((0.52f + e * 0.12f) * pulse));
            g.strokePath(core, juce::PathStrokeType(
                juce::jmax(1.6f, geo.thickness * 0.18f),
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── 6. Top specular rim stroke ────────────────────────────────────
        {
            const float rimPulse = 0.80f + 0.20f * std::sin(time * 2.1f + geo.sx * 0.01f);
            auto topC = makeSideContour(geo, +1.f);
            g.setColour(kLitTop.withAlpha(0.26f * rimPulse));
            g.strokePath(topC, juce::PathStrokeType(3.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(kGloss.withAlpha(0.46f * rimPulse));
            g.strokePath(topC, juce::PathStrokeType(0.9f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── 7. Bottom occlusion rim ───────────────────────────────────────
        {
            auto botC = makeSideContour(geo, -1.f);
            g.setColour(kShadow.withAlpha(0.72f));
            g.strokePath(botC, juce::PathStrokeType(2.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(kOcclu.withAlpha(0.62f));
            g.strokePath(botC, juce::PathStrokeType(0.75f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

private:
    // ── Palette ──────────────────────────────────────────────────────────
    const juce::Colour kBody    { 0xFFFF6FB0 };
    const juce::Colour kLitTop  { 0xFFFFCCE4 };
    const juce::Colour kGloss   { 0xFFFFF0F6 };
    const juce::Colour kGlow    { 0xFFFF9AC6 };
    const juce::Colour kAura    { 0xFFFF75BE };
    const juce::Colour kShadow  { 0xFF9B2055 };
    const juce::Colour kDeepMag { 0xFF6A1038 };
    const juce::Colour kOcclu   { 0xFF2E0713 };

    static juce::Rectangle<float> getClipBounds(const BubblegumCableGeometry& geo)
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
        const float w0 = (side > 0.f ? f.topW : f.bottomW);
        path.startNewSubPath(f.x + f.nx * side * w0, f.y + f.ny * side * w0);
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
