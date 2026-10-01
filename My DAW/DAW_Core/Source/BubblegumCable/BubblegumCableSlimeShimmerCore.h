#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeShimmerCore  -- iridescent wet surface shimmer
//
// Drawn last (after all slime layers), adds a wet-surface reflective
// sheen that travels slowly along the cable top face, simulating the
// way light catches moving slime / gel under a light source.
//
// Two effects:
//   1. Slow color-shifting streak along top contour
//      (pink -> lilac -> white -> pink, animated over ~4s cycle)
//   2. Bright static specular "puddle" highlight near midpoint
//      (brightest where cable sags = most slime pooled)
// =====================================================================
class BubblegumCableSlimeShimmerCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);

        // ── 1. Color-shifting streak on top contour ────────────────────────
        {
            auto topC = makeSideContour(geo, +1.f);

            // Cycle through pink → lilac → white → pink over 5s
            const float cycle = std::fmod(time * 0.20f, 1.0f);
            const float hue   = 300.f + cycle * 60.f;  // 300=magenta, 360=red-pink, via lilac

            // Map cycle to a shimmer colour
            juce::Colour shimmer;
            if (cycle < 0.33f)
            {
                // pink → lilac
                const float f = cycle / 0.33f;
                shimmer = juce::Colour(0xFFFF9AC8).interpolatedWith(juce::Colour(0xFFE0A0FF), f);
            }
            else if (cycle < 0.66f)
            {
                // lilac → white
                const float f = (cycle - 0.33f) / 0.33f;
                shimmer = juce::Colour(0xFFE0A0FF).interpolatedWith(juce::Colour(0xFFFFFFFF), f);
            }
            else
            {
                // white → pink
                const float f = (cycle - 0.66f) / 0.34f;
                shimmer = juce::Colour(0xFFFFFFFF).interpolatedWith(juce::Colour(0xFFFF9AC8), f);
            }

            juce::ignoreUnused(hue);

            const float pulse = 0.75f + 0.25f * std::sin(time * 1.9f + geo.sx * 0.012f);
            // Wide soft halo
            g.setColour(shimmer.withAlpha(0.18f * pulse + e * 0.06f));
            g.strokePath(topC, juce::PathStrokeType(3.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Tight bright line
            g.setColour(shimmer.withAlpha(0.55f * pulse + e * 0.10f));
            g.strokePath(topC, juce::PathStrokeType(0.8f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── 2. Specular puddle at sag midpoint ────────────────────────────
        {
            const auto  s   = sampleAlong(geo, 0.5f);  // midpoint = most slime
            const float hw  = s.halfW;
            const float r   = hw * (3.5f + e * 1.5f);
            const float cy  = s.pos.y + s.normal.y * hw * 0.4f;
            const float cx  = s.pos.x + s.normal.x * hw * 0.4f;

            const float pulse2 = 0.80f + 0.20f * std::sin(time * 0.9f + geo.tx * 0.008f);
            juce::ColourGradient pool(
                juce::Colour(0xFFFFFFFF).withAlpha(0.35f * pulse2 + e * 0.08f),
                cx, cy,
                juce::Colour(0xFFFFB8DC).withAlpha(0.0f),
                cx + r, cy, true);
            g.setGradientFill(pool);
            g.fillEllipse(cx - r, cy - r * 0.4f, r * 2.f, r * 0.8f);
        }
    }

private:
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

    struct Sample { juce::Point<float> pos, normal; float halfW = 0.f; };

    static Sample sampleAlong(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float ft = juce::jlimit(0.f, 1.f, t) * (float)BubblegumCableGeometry::kSeg;
        const int   i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)ft);
        const int   i1 = juce::jmin(BubblegumCableGeometry::kSeg, i0 + 1);
        const float a  = ft - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        Sample s;
        s.pos    = { juce::jmap(a, p0.x, p1.x), juce::jmap(a, p0.y, p1.y) };
        s.normal = { juce::jmap(a, p0.nx, p1.nx), juce::jmap(a, p0.ny, p1.ny) };
        s.halfW  = juce::jmap(a, p0.topW, p1.topW);
        const float nl = std::sqrt(s.normal.x * s.normal.x + s.normal.y * s.normal.y);
        if (nl > 1e-4f) { s.normal.x /= nl; s.normal.y /= nl; }
        else            { s.normal = { 0.f, -1.f }; }
        return s;
    }
};

} // namespace DAW
