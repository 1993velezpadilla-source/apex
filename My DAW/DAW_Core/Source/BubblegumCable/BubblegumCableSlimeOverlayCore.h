#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeOverlayCore  -- organic lump overlay over tube body
//
// Drawn AFTER render_ so it sits on top of the clean tube, breaking
// its perfect cylindrical silhouette and making it read as a blobby
// slime mass rather than a rubber tube.
//
// Places 5-9 overlapping organic blobs along the cable that:
//   - Are anchored to the cable centerline (no independent movement)
//   - Bulge outward in both top and bottom directions
//   - Are fully opaque, same color family as cable
//   - Vary in size to create irregular lumpy texture
//   - Blend into each other creating a continuous slime skin
// =====================================================================
class BubblegumCableSlimeOverlayCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float seed = in.source.x * 0.041f + in.target.x * 0.027f + 5.5f;

        const int count = 5 + (int)(BubblegumCableAnimationCore::stable01(seed) * 4.f);

        for (int i = 0; i < count; ++i)
        {
            const float fs = seed + (float)i * 6.1f;

            // Position along cable (distributed, gravity-biased toward sag)
            const float rawT = (float)(i + 1) / (float)(count + 1);
            const float biasT = juce::jmap(rawT, 0.12f, 0.88f);
            const float t = juce::jlimit(0.05f, 0.95f, biasT);

            const auto  s  = sampleAlong(geo, t);
            const float hw = s.halfW;

            // Blob dimensions: extends beyond cable half-width on both sides
            const float topExt  = hw * (0.7f + BubblegumCableAnimationCore::stable01(fs + 1.1f) * 0.9f)
                                 + e * hw * 0.2f;
            const float botExt  = hw * (0.5f + BubblegumCableAnimationCore::stable01(fs + 2.3f) * 0.7f)
                                 + e * hw * 0.15f;
            const float blobW   = hw * (2.5f + BubblegumCableAnimationCore::stable01(fs + 3.7f) * 2.0f);

            // Blob centre on the cable centerline
            const float cx = s.pos.x;
            const float cy = s.pos.y;

            // Top extent (in normal direction)
            const float topX = cx + s.normal.x * (hw + topExt);
            const float topY = cy + s.normal.y * (hw + topExt);
            // Bot extent (against normal direction)
            const float botX = cx - s.normal.x * (hw + botExt);
            const float botY = cy - s.normal.y * (hw + botExt);

            // Total height in normal direction
            const float totalH = (hw + topExt) + (hw + botExt);
            // Centroid
            const float bcx = (topX + botX) * 0.5f;
            const float bcy = (topY + botY) * 0.5f;

            // Slow gentle pulsing alpha (makes slime feel alive)
            const float pulse = 0.88f + 0.12f * std::sin(
                time * (0.4f + 0.05f * (float)i) + fs * 1.3f);

            // ── Blob fill ──────────────────────────────────────────────────
            {
                const float halfH = totalH * 0.5f;
                juce::ColourGradient grad(
                    kLumpHigh,   bcx, bcy - halfH * 0.5f,
                    kLumpDark,   bcx, bcy + halfH, false);
                grad.addColour(0.25, kLumpMid);
                grad.addColour(0.55, kLumpBody);
                grad.addColour(0.80, kLumpShadow);
                g.setGradientFill(grad);

                // Build a slightly irregular ellipse via path with 4 bezier arcs
                juce::Path blob;
                buildOrganicBlob(blob, bcx, bcy,
                                 blobW * 0.5f, halfH,
                                 s.tangent,
                                 BubblegumCableAnimationCore::stable01(fs + 9.1f));
                g.fillPath(blob);
            }

            // ── Wet edge stroke ────────────────────────────────────────────
            {
                juce::Path blob;
                buildOrganicBlob(blob, bcx, bcy,
                                 blobW * 0.5f,
                                 totalH * 0.5f,
                                 s.tangent,
                                 BubblegumCableAnimationCore::stable01(fs + 9.1f));
                g.setColour(kLumpEdge.withAlpha(0.45f * pulse));
                g.strokePath(blob, juce::PathStrokeType(0.7f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // ── Specular glint on top of each lump ────────────────────────
            {
                const float sr = juce::jmax(0.6f, hw * 0.18f);
                g.setColour(kSpec);
                g.fillEllipse(topX - s.normal.x * hw * 0.3f - sr,
                              topY - s.normal.y * hw * 0.3f - sr,
                              sr * 2.f, sr * 2.f);
            }

            juce::ignoreUnused(pulse, e);
        }
    }

private:
    const juce::Colour kSpec       { 0xFFFFFFFF };
    const juce::Colour kLumpHigh   { 0xFFFFCCE8 };
    const juce::Colour kLumpMid    { 0xFFFF80C0 };
    const juce::Colour kLumpBody   { 0xFFD83890 };
    const juce::Colour kLumpShadow { 0xFF7A1040 };
    const juce::Colour kLumpDark   { 0xFF1E0408 };
    const juce::Colour kLumpEdge   { 0xFFFFB0D8 };

    // Build an organic (slightly squished) ellipse aligned to the cable tangent
    static void buildOrganicBlob(juce::Path& p,
                                  float cx, float cy,
                                  float rx, float ry,
                                  juce::Point<float> tangent,
                                  float squish)
    {
        // Rotate ellipse to align with cable direction
        const float ang = std::atan2(tangent.y, tangent.x);
        // Slight squish on one side to make it organic
        const float rx2 = rx * (0.82f + squish * 0.36f);
        p.addEllipse(-rx2, -ry, rx2 * 2.f, ry * 2.f);
        p.applyTransform(juce::AffineTransform::rotation(ang).translated(cx, cy));
    }

    struct Sample { juce::Point<float> pos, normal, tangent; float halfW = 0.f; };

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
        s.tangent = { -s.normal.y, s.normal.x };
        return s;
    }
};

} // namespace DAW
