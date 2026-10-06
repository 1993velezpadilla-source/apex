#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeCoatCore  -- organic wet slime coating on cable
//
// Drawn BEFORE the main cable body so the clean tube always sits on top.
// Builds a slightly wider irregular ribbon by perturbing each cable
// point's normal offset with low-frequency sinusoidal lumps, then
// fills and strokes it to give the cable an organic, gooey silhouette.
//
// Effect: the cable looks coated in viscous gel -- edges are lumpy and
// wet rather than perfectly smooth.
//
// Two passes:
//   1. Wide organic fill  (same pink family, slightly darker/deeper)
//   2. Outer contour stroke  (slightly brighter, wet-surface edge)
// =====================================================================
class BubblegumCableSlimeCoatCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float seed = in.source.x * 0.023f + in.target.x * 0.017f;

        // Build an organically-perturbed wider ribbon
        constexpr int N  = BubblegumCableGeometry::kSeg;
        const float extra = 1.8f + e * 0.8f;   // coat thickness past cable edge

        // Top contour (perturbed outward from cable top edge)
        auto buildContour = [&](float side) -> juce::Path
        {
            juce::Path path;
            for (int i = 0; i <= N; ++i)
            {
                const auto& p = geo.points[(size_t)i];
                const float baseW = (side > 0.f ? p.topW : p.bottomW);

                // Organic lump: purely seed+geometry -- NO time so shape is stable
                const float lump =
                    0.55f * std::sin(p.t * 6.3f  + seed) +
                    0.30f * std::sin(p.t * 13.1f + seed * 1.7f) +
                    0.15f * std::sin(p.t * 22.7f + seed * 2.3f);

                const float w = baseW + extra * (1.f + lump * 0.35f);
                const float px = p.x + p.nx * side * w;
                const float py = p.y + p.ny * side * w;

                if (i == 0) path.startNewSubPath(px, py);
                else        path.lineTo(px, py);
            }
            return path;
        };

        juce::Path topC = buildContour(+1.f);
        juce::Path botC = buildContour(-1.f);

        // Close into a filled shape: top forward, bottom reversed
        juce::Path coat = topC;
        for (int i = N; i >= 0; --i)
        {
            const auto& p  = geo.points[(size_t)i];
            const float lump =
                0.55f * std::sin(p.t * 6.3f  + seed) +
                0.30f * std::sin(p.t * 13.1f + seed * 1.7f) +
                0.15f * std::sin(p.t * 22.7f + seed * 2.3f);
            const float w  = p.bottomW + extra * (1.f + lump * 0.35f);
            coat.lineTo(p.x - p.nx * w, p.y - p.ny * w);
        }
        coat.closeSubPath();

        // ── 1. Organic coat fill ──────────────────────────────────────────
        {
            const auto b = getCoatBounds(geo, extra);
            juce::ColourGradient grad(
                kCoatHigh, b.getCentreX(), b.getY(),
                kCoatDark, b.getCentreX(), b.getBottom(), false);
            grad.addColour(0.35, kCoatMid);
            grad.addColour(0.70, kCoatShadow);
            g.setGradientFill(grad);
            g.fillPath(coat);
        }

        // ── 2. Wet outer edge stroke ──────────────────────────────────────
        {
            const float pulse = 0.80f + 0.20f * std::sin(time * 1.3f + geo.sx * 0.01f);
            g.setColour(kCoatEdge.withAlpha(0.55f * pulse));
            g.strokePath(coat, juce::PathStrokeType(0.8f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

private:
    const juce::Colour kCoatHigh   { 0xFFFF9AC8 };
    const juce::Colour kCoatMid    { 0xFFD43A8A };
    const juce::Colour kCoatShadow { 0xFF7A1240 };
    const juce::Colour kCoatDark   { 0xFF1E0409 };
    const juce::Colour kCoatEdge   { 0xFFFFB8DC };

    static juce::Rectangle<float> getCoatBounds(const BubblegumCableGeometry& geo,
                                                 float extra)
    {
        float mnX = geo.points[0].x, mxX = mnX;
        float mnY = geo.points[0].y, mxY = mnY;
        for (const auto& p : geo.points)
        {
            const float w = juce::jmax(p.topW, p.bottomW) + extra * 1.35f;
            mnX = juce::jmin(mnX, p.x - std::abs(p.nx) * w);
            mxX = juce::jmax(mxX, p.x + std::abs(p.nx) * w);
            mnY = juce::jmin(mnY, p.y - std::abs(p.ny) * w);
            mxY = juce::jmax(mxY, p.y + std::abs(p.ny) * w);
        }
        return { mnX, mnY, juce::jmax(1.f, mxX - mnX), juce::jmax(1.f, mxY - mnY) };
    }
};

} // namespace DAW
