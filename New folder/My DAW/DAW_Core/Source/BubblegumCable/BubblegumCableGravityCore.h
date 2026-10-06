#pragma once
#include "BubblegumCableTypes.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableGravityCore
//
// Provides a single unified gravity model for all cable effects.
// In 2D screen space, gravity always points straight DOWN: (0, +1).
//
// Why this matters:
//   - Without gravity, drips and beads use the cable NORMAL as their
//     "down" direction. The cable normal is perpendicular to the
//     centerline, which is diagonal → effects float sideways / outward.
//   - With this core, every effect that falls "down" uses screen-space
//     vertical (0, +1) regardless of cable angle.
//
// Also provides:
//   - constrainToBody(): clamps a world-space x,y so it stays within
//     ±halfW of the cable centerline at a given t.
//   - dripFootX(): the x position a drip falling from t should have
//     (straight down from attachment, not along cable normal).
// =====================================================================
class BubblegumCableGravityCore
{
public:
    // Screen-space gravity direction (always straight down)
    static constexpr float kGravX = 0.f;
    static constexpr float kGravY = 1.f;

    // Returns the world-space attachment point on the cable underside at t.
    static juce::Point<float> undersideAttachment(
        const BubblegumCableGeometry& geo, float t) noexcept
    {
        const auto s = sampleAt(geo, t);
        return { s.cx - s.nx * s.bottomW, s.cy - s.ny * s.bottomW };
    }

    // Returns a point that is `dropDist` pixels STRAIGHT DOWN from the
    // cable underside attachment at t.
    // This is where gravity-aligned drip bodies/beads should be centered.
    static juce::Point<float> gravityDrop(
        const BubblegumCableGeometry& geo, float t, float dropDist) noexcept
    {
        const auto attach = undersideAttachment(geo, t);
        return { attach.x + kGravX * dropDist,
                 attach.y + kGravY * dropDist };
    }

    // Clamp a screen-space x position to stay within ±maxHalfSpread of the
    // cable centerline x at a given t.
    static float clampX(const BubblegumCableGeometry& geo,
                        float t, float worldX, float maxHalfSpread) noexcept
    {
        const auto s = sampleAt(geo, t);
        return juce::jlimit(s.cx - maxHalfSpread, s.cx + maxHalfSpread, worldX);
    }

    // How much horizontal spread is allowed for an effect at t.
    // Returns halfW * spreadFrac, so effects stay inside cable silhouette.
    static float allowedHalfSpread(const BubblegumCableGeometry& geo,
                                   float t, float spreadFrac = 0.75f) noexcept
    {
        const auto s = sampleAt(geo, t);
        return juce::jmax(s.bottomW, s.topW) * spreadFrac;
    }

private:
    struct CableSample { float cx, cy, nx, ny, topW, bottomW; };

    static CableSample sampleAt(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float fi = juce::jlimit(0.f, 1.f, t) * (float)BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)fi);
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg,     i0 + 1);
        const float a = fi - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        float nx = juce::jmap(a, p0.nx, p1.nx);
        float ny = juce::jmap(a, p0.ny, p1.ny);
        const float nl = std::sqrt(nx * nx + ny * ny);
        if (nl > 1e-4f) { nx /= nl; ny /= nl; } else { nx = 0.f; ny = -1.f; }
        return {
            juce::jmap(a, p0.x, p1.x), juce::jmap(a, p0.y, p1.y),
            nx, ny,
            juce::jmap(a, p0.topW, p1.topW),
            juce::jmap(a, p0.bottomW, p1.bottomW)
        };
    }
};

} // namespace DAW
