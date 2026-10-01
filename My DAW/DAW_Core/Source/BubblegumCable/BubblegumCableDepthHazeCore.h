#pragma once
#include "BubblegumCableTypes.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableDepthHazeCore  -- 2.5D atmospheric depth
//
// In a 2.5D scene, objects further from the viewer are slightly darker
// and more desaturated. For a hanging cable, the sag midpoint is the
// "deepest" part (furthest from the viewer) and the endpoints are the
// "nearest" (attached to the front surface of the mixer).
//
// This nucleus adds a subtle dark gradient overlay that:
//   - Is brightest (alpha=0) at both endpoints
//   - Is darkest at the sag midpoint (most depth)
//   - Uses a dark desaturated overlay so the midpoint reads as further
//
// Additionally adds a very subtle secondary specular at the endpoints
// (near surfaces catch more specular light) to reinforce the depth.
//
// All shape is purely geometry-based. Time used only for alpha pulse.
// =====================================================================
class BubblegumCableDepthHazeCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);

        // ── Depth gradient overlay (clipped to ribbon) ─────────────────────
        {
            juce::Graphics::ScopedSaveState st(g);
            g.reduceClipRegion(geo.ribbon);

            const auto b = getRibbonBounds(geo);
            // Horizontal gradient: bright at left endpoint, dark in mid, bright at right
            // Approximate: left=sx side, right=tx side
            const float leftX  = juce::jmin(geo.sx, geo.tx);
            const float rightX = juce::jmax(geo.sx, geo.tx);
            const float midX   = (leftX + rightX) * 0.5f;

            // Left half: bright -> dark
            {
                juce::ColourGradient grad(
                    kHaze.withAlpha(0.f),    leftX, b.getCentreY(),
                    kHaze.withAlpha(0.18f),  midX,  b.getCentreY(), false);
                g.setGradientFill(grad);
                g.fillRect(b.getX(), b.getY(),
                           midX - b.getX(), b.getHeight());
            }
            // Right half: dark -> bright
            {
                juce::ColourGradient grad(
                    kHaze.withAlpha(0.18f),  midX,   b.getCentreY(),
                    kHaze.withAlpha(0.f),    rightX, b.getCentreY(), false);
                g.setGradientFill(grad);
                g.fillRect(midX, b.getY(),
                           b.getRight() - midX, b.getHeight());
            }

            juce::ignoreUnused(e);
        }

        // ── Near-endpoint brightening (depth-near specular catch) ──────────
        {
            const float pulse = 0.85f + 0.15f * std::sin(time * 1.1f + geo.sx * 0.008f);
            paintNearBright(g, geo.sx, geo.sy, geo.thickness, pulse);
            paintNearBright(g, geo.tx, geo.ty, geo.thickness, pulse);
        }
    }

private:
    const juce::Colour kHaze  { 0xFF1A0308 };   // deep dark desaturated overlay
    const juce::Colour kNear  { 0xFFFFEEF8 };   // near-viewer brightening

    void paintNearBright(juce::Graphics& g,
                         float cx, float cy, float thickness,
                         float pulse) const
    {
        const float r = juce::jmax(5.f, thickness * 0.65f);
        juce::ColourGradient grad(
            kNear.withAlpha(0.22f * pulse), cx, cy,
            kNear.withAlpha(0.0f),          cx + r * 1.5f, cy, true);
        g.setGradientFill(grad);
        g.fillEllipse(cx - r * 1.5f, cy - r * 1.5f, r * 3.f, r * 3.f);
    }

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
};

} // namespace DAW
