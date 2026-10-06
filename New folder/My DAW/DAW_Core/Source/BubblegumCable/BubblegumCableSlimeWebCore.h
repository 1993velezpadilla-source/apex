#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeWebCore  -- connecting filaments between blobs
//
// Draws ultra-thin slime filaments between adjacent blob anchor points
// along the cable. These are the "connective tissue" of the slime --
// the micro-threads you see stretching between gel droplets.
//
// Uses the SAME blob positions as SlimeBlobsCore (same seed formula)
// so filaments always connect to the exact blob locations.
//
// Each filament:
//   - Thin quadratic bezier (0.4-0.7px stroke)
//   - Sags slightly between anchor points (mid ctrl pt pulled down)
//   - Tapers: wider at both ends (attached to blobs), narrow at mid
//   - Color: slightly darker than body, semi-transparent is OK here
//     because filaments are ON the cable surface (not the body itself)
//
// Shape is purely seed+geometry derived -- no time in positions.
// =====================================================================
class BubblegumCableSlimeWebCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float seed = in.source.x * 0.031f + in.target.x * 0.019f;

        const int count = 3 + (int)(BubblegumCableAnimationCore::stable01(seed) * 4.f);

        // Rebuild the same blob anchor positions as SlimeBlobsCore
        struct BlobAnchor { float ax, ay, t; };
        std::array<BlobAnchor, 8> anchors;

        for (int i = 0; i < count; ++i)
        {
            const float fs    = seed + (float)i * 7.3f;
            const float rawT  = BubblegumCableAnimationCore::stable01(fs + 1.1f);
            const float t     = juce::jlimit(0.08f, 0.92f, juce::jmap(rawT, 0.22f, 0.78f));
            const auto  s     = sampleAlong(geo, t);
            const float hw    = s.halfW;
            // Cable bottom edge (same as SlimeBlobsCore anchor)
            anchors[(size_t)i] = { s.pos.x - s.normal.x * hw,
                                   s.pos.y - s.normal.y * hw,
                                   t };
        }

        // Draw filament between each adjacent pair
        const float pulse = 0.75f + 0.25f * std::sin(time * 0.9f + geo.sx * 0.01f);

        for (int i = 0; i < count - 1; ++i)
        {
            const auto& a0 = anchors[(size_t)i];
            const auto& a1 = anchors[(size_t)(i + 1)];

            // Mid control point: midway, pulled down (sag) + slight normal offset
            const float midT  = (a0.t + a1.t) * 0.5f;
            const auto  sm    = sampleAlong(geo, midT);
            const float sagAmt = juce::jmax(2.f, sm.halfW * 1.2f);

            const float mcx = (a0.ax + a1.ax) * 0.5f - sm.normal.x * sagAmt;
            const float mcy = (a0.ay + a1.ay) * 0.5f - sm.normal.y * sagAmt;

            // Draw as a series of segments, tapering toward middle
            const int   segs  = 6;
            const float maxW  = juce::jmax(0.6f, sm.halfW * 0.25f);

            for (int si = 0; si < segs; ++si)
            {
                const float t0 = (float)si       / (float)segs;
                const float t1 = (float)(si + 1) / (float)segs;

                // Taper: fat at ends (t≈0 or t≈1), thin at mid (t≈0.5)
                const float midness = 1.f - std::abs(juce::jmap((t0 + t1) * 0.5f, 0.f, 1.f,
                                                                  -1.f, 1.f));
                const float strokeW = juce::jmap(midness, maxW, maxW * 0.35f);

                auto bezier = [&](float bt) -> juce::Point<float>
                {
                    const float ia = 1.f - bt;
                    return { ia * ia * a0.ax + 2.f * ia * bt * mcx + bt * bt * a1.ax,
                             ia * ia * a0.ay + 2.f * ia * bt * mcy + bt * bt * a1.ay };
                };

                const auto p0 = bezier(t0);
                const auto p1 = bezier(t1);

                // Alpha: slightly dimmer at mid-span
                const float spanAlpha = (0.55f + e * 0.10f)
                                      * juce::jmap(midness, 0.70f, 1.0f)
                                      * pulse;

                g.setColour(kWeb.withAlpha(spanAlpha));
                juce::Path seg;
                seg.startNewSubPath(p0.x, p0.y);
                seg.lineTo(p1.x, p1.y);
                g.strokePath(seg, juce::PathStrokeType(strokeW));
            }
        }
    }

private:
    const juce::Colour kWeb { 0xFFBB2878 };  // deeper magenta thread

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
        s.pos    = { juce::jmap(a, p0.x,  p1.x),  juce::jmap(a, p0.y,  p1.y)  };
        s.normal = { juce::jmap(a, p0.nx, p1.nx),  juce::jmap(a, p0.ny, p1.ny) };
        s.halfW  = juce::jmap(a, p0.topW, p1.topW);
        const float nl = std::sqrt(s.normal.x * s.normal.x + s.normal.y * s.normal.y);
        if (nl > 1e-4f) { s.normal.x /= nl; s.normal.y /= nl; }
        else            { s.normal = { 0.f, -1.f }; }
        return s;
    }
};

} // namespace DAW
