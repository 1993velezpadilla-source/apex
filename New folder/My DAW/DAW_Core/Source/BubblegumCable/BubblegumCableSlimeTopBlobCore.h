#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>
#include <array>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeTopBlobCore
//
// Adds 3-6 organic blob protrusions on the TOP edge of the cable,
// like slime pooling / bubbling upward. Each blob is a soft rounded
// cap drawn OUTSIDE the ribbon on the upper lip.
//
// Reference look: top of slime has irregular rounded hills, not a
// flat line — as if the slime is sagging inward at some points and
// bulging outward at others.
// =====================================================================
class BubblegumCableSlimeTopBlobCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time,
               float                         energy) const
    {
        const float e    = juce::jlimit(0.f, 1.f, energy);
        const float seed = BubblegumCableAnimationCore::cableSeed(in);

        const int count = 3 + (int)(BubblegumCableAnimationCore::stable01(seed + 0.1f) * 3.f);

        for (int i = 0; i < count; ++i)
        {
            const float si   = seed + (float)i * 5.3f;
            const float baseT = juce::jmap((float)(i + 1) / (float)(count + 1), 0.12f, 0.88f);
            const float jitter = (BubblegumCableAnimationCore::stable01(si + 1.1f) - 0.5f) * 0.06f;
            const float t     = juce::jlimit(0.06f, 0.94f, baseT + jitter);

            // Slow organic breathing
            const float phase = BubblegumCableAnimationCore::stable01(si + 2.2f)
                              * juce::MathConstants<float>::twoPi;
            const float breathe = 1.0f + 0.14f * std::sin(time * (0.6f + 0.08f * (float)i) + phase);
            const float sz = BubblegumCableAnimationCore::stable01(si + 3.7f);

            // Sample cable top edge
            const auto s = sampleTopEdge(geo, t);
            const float r = juce::jmax(2.8f, s.halfW * (0.32f + sz * 0.22f + e * 0.08f) * breathe);

            // Blob centre is slightly above the top edge
            const float cx = s.x + s.nx * r * 0.45f;
            const float cy = s.y + s.ny * r * 0.45f;

            // Outer soft halo
            juce::ColourGradient halo(
                kBlobOuter.withAlpha(0.38f + e * 0.06f), cx, cy,
                kBlobOuter.withAlpha(0.00f),             cx, cy - r * 1.8f, true);
            g.setGradientFill(halo);
            g.fillEllipse(cx - r * 1.0f, cy - r * 1.2f, r * 2.0f, r * 2.4f);

            // Solid blob body
            juce::ColourGradient body(
                kBlobLit .withAlpha(0.82f), cx, cy - r * 0.3f,
                kBlobBody.withAlpha(0.86f), cx, cy + r * 0.8f, false);
            body.addColour(0.45, kBlobBody.withAlpha(0.84f));
            g.setGradientFill(body);
            g.fillEllipse(cx - r * 0.82f, cy - r * 0.72f, r * 1.64f, r * 1.55f);

            // Specular cap
            g.setColour(kGloss.withAlpha(0.50f));
            g.fillEllipse(cx - r * 0.28f, cy - r * 0.52f, r * 0.52f, r * 0.34f);
        }
    }

private:
    const juce::Colour kBlobBody  { 0xFFFF6FB0 };
    const juce::Colour kBlobLit   { 0xFFFFCCE0 };
    const juce::Colour kBlobOuter { 0xFFFF75BE };
    const juce::Colour kGloss     { 0xFFFFF0F6 };

    struct EdgeSample { float x, y, nx, ny, halfW; };

    static EdgeSample sampleTopEdge(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float fi = juce::jlimit(0.f, 1.f, t) * (float)BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)std::floor(fi));
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a = fi - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        float nx = juce::jmap(a, p0.nx, p1.nx);
        float ny = juce::jmap(a, p0.ny, p1.ny);
        const float nl = std::sqrt(nx * nx + ny * ny);
        if (nl > 1e-4f) { nx /= nl; ny /= nl; }
        else             { nx = 0.f; ny = -1.f; }
        const float tw = juce::jmap(a, p0.topW, p1.topW);
        EdgeSample s;
        s.x = juce::jmap(a, p0.x, p1.x) + nx * tw;
        s.y = juce::jmap(a, p0.y, p1.y) + ny * tw;
        s.nx = nx; s.ny = ny;
        s.halfW = juce::jmap(a, p0.halfW, p1.halfW);
        return s;
    }
};

} // namespace DAW
