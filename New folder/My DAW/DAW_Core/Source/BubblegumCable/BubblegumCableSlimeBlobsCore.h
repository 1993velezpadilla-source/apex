#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeBlobsCore  -- slime blobs anchored to the cable
//
// Blobs are PART OF the cable -- they are accumulations of the slime
// that coats the wire. Each blob is hard-anchored to the cable bottom
// edge via a solid slime neck so it never looks magnetic or floating.
//
// No independent bob. Position is purely cable-geometry-relative so
// blobs always move exactly with the cable when the mixer is dragged.
//
// Per blob:
//   1. Neck  -- solid tapered rect from cable underside to blob centre
//   2. Shadow blob
//   3. Solid opaque body (gradient: crown -> body -> shadow)
//   4. Specular dot
// =====================================================================
class BubblegumCableSlimeBlobsCore
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

        for (int i = 0; i < count; ++i)
        {
            const float fs = seed + (float)i * 7.3f;

            // Stable cable-relative position (gravity-biased toward sag midpoint)
            const float rawT  = BubblegumCableAnimationCore::stable01(fs + 1.1f);
            const float t     = juce::jlimit(0.08f, 0.92f, juce::jmap(rawT, 0.22f, 0.78f));

            const auto  s  = sampleAlong(geo, t);
            const float hw = s.halfW;

            // Blob size
            const float rScale = 1.3f + BubblegumCableAnimationCore::stable01(fs + 2.9f) * 0.9f;
            const float r      = hw * rScale + e * hw * 0.25f;

            // Neck attachment point: cable bottom edge (no bob, hard-anchored)
            const float nax = s.pos.x - s.normal.x * hw;
            const float nay = s.pos.y - s.normal.y * hw;

            // Blob centre: directly below attachment by r (downward in screen = +Y)
            // "below" the cable means in the direction AWAY from the normal
            const float blobCx = nax - s.normal.x * r;
            const float blobCy = nay - s.normal.y * r;

            // ── 1. Neck (solid, tapered from cable underside to blob edge) ─
            {
                const float neckTopW = juce::jmax(hw * 0.55f, 1.8f);
                const float neckBotW = juce::jmax(hw * 0.30f, 1.2f);
                // tangent for perpendicular offset
                const float tx =  s.normal.y;
                const float ty = -s.normal.x;
                juce::Path neck;
                neck.startNewSubPath(nax + tx * neckTopW, nay + ty * neckTopW);
                neck.lineTo         (nax - tx * neckTopW, nay - ty * neckTopW);
                neck.lineTo         (blobCx - tx * neckBotW, blobCy - ty * neckBotW);
                neck.lineTo         (blobCx + tx * neckBotW, blobCy + ty * neckBotW);
                neck.closeSubPath();
                g.setColour(kNeck);
                g.fillPath(neck);
            }

            // ── 2. Shadow ──────────────────────────────────────────────────
            g.setColour(juce::Colour(0x44000000));
            g.fillEllipse(blobCx - r * 0.88f, blobCy + r * 0.28f, r * 1.76f, r * 0.9f);

            // ── 3. Solid body ──────────────────────────────────────────────
            {
                juce::ColourGradient grad(
                    kBlobHigh,   blobCx, blobCy - r * 0.45f,
                    kBlobDark,   blobCx, blobCy + r, false);
                grad.addColour(0.32, kBlobMid);
                grad.addColour(0.68, kBlobShadow);
                g.setGradientFill(grad);
                g.fillEllipse(blobCx - r, blobCy - r, r * 2.f, r * 2.f);
            }

            // ── 4. Specular dot ────────────────────────────────────────────
            {
                const float sr = juce::jmax(0.8f, r * 0.24f);
                g.setColour(kSpec);
                g.fillEllipse(blobCx - r * 0.33f - sr, blobCy - r * 0.40f - sr,
                              sr * 2.f, sr * 2.f);
            }
        }
    }

private:
    const juce::Colour kSpec      { 0xFFFFFFFF };
    const juce::Colour kBlobHigh  { 0xFFFFD6EC };
    const juce::Colour kBlobMid   { 0xFFEE4FA0 };
    const juce::Colour kBlobShadow{ 0xFF8C1848 };
    const juce::Colour kBlobDark  { 0xFF2A0612 };
    const juce::Colour kNeck      { 0xFFD43A8A };  // slightly darker, slime neck

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
