#pragma once
#include "BubblegumCableTypes.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeSkinCore
//
// Draws a translucent outer membrane layer over the entire cable body.
// Like a wet slime surface: thin translucent skin that catches light
// differently from the main body — slightly iridescent, slightly brighter
// near the top edge.
//
// This is drawn LAST after all other layers so it reads as "on top of"
// everything, unifying all the layers underneath into one cohesive
// slime object.
// =====================================================================
class BubblegumCableSlimeSkinCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               float                         time,
               float                         energy) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e = juce::jlimit(0.f, 1.f, energy);

        // ── Outer membrane fill (very low alpha, just tints everything) ──
        {
            juce::Graphics::ScopedSaveState st(g);
            g.reduceClipRegion(geo.ribbon);

            const auto b = getBounds(geo);
            juce::ColourGradient skin(
                kSkinTop.withAlpha(0.18f + e * 0.04f), b.getCentreX(), b.getY(),
                kSkinBot.withAlpha(0.08f + e * 0.02f), b.getCentreX(), b.getBottom(), false);
            skin.addColour(0.22, kSkinTop.withAlpha(0.12f));
            skin.addColour(0.55, kSkinMid.withAlpha(0.07f));
            g.setGradientFill(skin);
            g.fillRect(b);
        }

        // ── Outer contour membrane stroke ────────────────────────────────
        {
            const float pulse = 0.82f + 0.18f * std::sin(time * 1.45f + geo.sx * 0.009f);
            auto ribbon = geo.ribbon;
            g.setColour(kSkinEdge.withAlpha((0.22f + e * 0.06f) * pulse));
            g.strokePath(ribbon, juce::PathStrokeType(1.1f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── Top iridescent sheen ──────────────────────────────────────────
        {
            auto topC = makeSideContour(geo, +1.f);
            const float sheen = 0.80f + 0.20f * std::sin(time * 2.6f + geo.tx * 0.008f);
            g.setColour(kIridescent.withAlpha(0.14f * sheen + e * 0.03f));
            g.strokePath(topC, juce::PathStrokeType(2.8f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

private:
    const juce::Colour kSkinTop    { 0xFFFFE8F4 };
    const juce::Colour kSkinMid    { 0xFFFF9AC6 };
    const juce::Colour kSkinBot    { 0xFF9B2055 };
    const juce::Colour kSkinEdge   { 0xFFFF6FB0 };
    const juce::Colour kIridescent { 0xFFFFCCFF };

    static juce::Rectangle<float> getBounds(const BubblegumCableGeometry& geo)
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
