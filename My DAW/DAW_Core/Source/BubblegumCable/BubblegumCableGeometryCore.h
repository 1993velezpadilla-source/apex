#pragma once
// ╔══════════════════════════════════════════════════════════════╗
// ║  LEGACY / GHOST — NOT the active overlay geometry           ║
// ║  BubblegumCableOverlayComponent does NOT use this file.     ║
// ║  Active geometry: Source/Bubblegum/BubblegumCableGeometryCore.h ║
// ║  Do NOT edit sag/catenary/curve constants here.             ║
// ╚══════════════════════════════════════════════════════════════╝
#include "BubblegumCableTypes.h"
#include "BubblegumCableSlimeObjProfileCore.h"

namespace DAW {

class BubblegumCableGeometryCore
{
public:
    BubblegumCableGeometry buildGeometry(const BubblegumCableInput& in,
                                         float laneBottomY,
                                         float pulseAlpha) const
    {
        BubblegumCableGeometry geo;
        const float e = juce::jlimit(0.f, 1.f, in.sendEnergy);

        const float sx = in.source.x;
        const float sy = in.source.y;
        const float tx = in.target.x;
        const float ty = in.target.y;
        geo.sx = sx; geo.sy = sy; geo.tx = tx; geo.ty = ty;

        const float dx      = tx - sx;
        const float dy      = ty - sy;
        const float span    = juce::jmax(1.f, std::abs(dx));
        const float levelK  = 1.f - juce::jlimit(0.f, 1.f, std::abs(dy) / juce::jmax(40.f, span));
        const float tension = juce::jlimit(0.f, 1.f, in.dragTension);

        const float sagBase = juce::jlimit(34.f, 190.f, span * 0.46f + e * 18.f);
        const float rawSag  = sagBase * (0.70f + 0.30f * levelK) * (1.f - tension * 0.28f);
        const float midLineY = 0.5f * (sy + ty);
        const float sag = laneBottomY > 0.f
            ? juce::jmin(rawSag, juce::jmax(18.f, laneBottomY - midLineY - 4.f))
            : rawSag;

        constexpr int N = BubblegumCableGeometry::kSeg;

        for (int i = 0; i <= N; ++i)
        {
            const float t  = (float) i / (float) N;
            auto& p        = geo.points[i];
            p.t = t;
            p.x = sx + dx * t;
            const float lineY = sy + dy * t;
            p.y = lineY + sag * catenaryDrop01(t);
        }

        for (int i = 0; i <= N; ++i)
        {
            const int   a   = juce::jmax(0, i - 1);
            const int   b   = juce::jmin(N, i + 1);
            const float tdx = geo.points[b].x - geo.points[a].x;
            const float tdy = geo.points[b].y - geo.points[a].y;
            const float len = std::sqrt(tdx * tdx + tdy * tdy);
            if (len < 1e-4f) { geo.points[i].nx = 0.f;        geo.points[i].ny = -1.f; }
            else             { geo.points[i].nx = -tdy / len; geo.points[i].ny =  tdx / len; }
        }

        // Target total cable diameter: ~6px (topW + bottomW ≈ 6).
        // base is the half-width scale; topW = base * 0.5, bottomW = base * 0.5.
        constexpr float kBase = 3.0f;   // half-width → total diameter ≈ 6px

        const float base = kBase + e * 0.5f;  // tiny energy swell, stays thin
        geo.thickness    = base * 2.f;         // expose full diameter for ball sizing

        const float seed = in.source.x * 0.017f + in.target.x * 0.011f;
        juce::ignoreUnused(seed);

        for (int i = 0; i <= N; ++i)
        {
            auto& p = geo.points[i];
            // Symmetric clean tube: both sides equal
            const float hw = base;
            p.topW    = hw;
            p.bottomW = hw;
            p.halfW   = hw;
        }

        {
            const auto& p0 = geo.points[0];
            geo.ribbon.startNewSubPath(p0.x + p0.nx * p0.topW,
                                       p0.y + p0.ny * p0.topW);
            for (int i = 1; i <= N; ++i)
            {
                const auto& p = geo.points[i];
                geo.ribbon.lineTo(p.x + p.nx * p.topW,
                                  p.y + p.ny * p.topW);
            }
            for (int i = N; i >= 0; --i)
            {
                const auto& p = geo.points[i];
                geo.ribbon.lineTo(p.x - p.nx * p.bottomW,
                                  p.y - p.ny * p.bottomW);
            }
            geo.ribbon.closeSubPath();
        }

        juce::ignoreUnused(pulseAlpha);
        return geo;
    }

    static float halfWidthAt(float t, float thickness) noexcept
    {
        const float arch   = std::sin(t * juce::MathConstants<float>::pi);
        const float shaped = std::pow(arch, 1.1f);
        return thickness * 0.5f * (1.f + shaped * 0.30f);
    }

    static juce::Point<float> undersideAt(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float clampedT = juce::jlimit(0.0f, 1.0f, t);
        const float fIndex   = clampedT * (float) BubblegumCableGeometry::kSeg;
        const int   i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int) std::floor(fIndex));
        const int   i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a  = fIndex - (float) i0;
        const auto& p0 = geo.points[(size_t) i0];
        const auto& p1 = geo.points[(size_t) i1];
        const float cx = juce::jmap(a, p0.x,       p1.x);
        const float cy = juce::jmap(a, p0.y,       p1.y);
        const float nx = juce::jmap(a, p0.nx,      p1.nx);
        const float ny = juce::jmap(a, p0.ny,      p1.ny);
        const float bw = juce::jmap(a, p0.bottomW, p1.bottomW);
        return { cx - nx * bw, cy - ny * bw };
    }

private:
    // TRUE catenary profile via cosh(), normalized to [0,1].
    // Endpoints = 0, center = 1, curvature continuous throughout.
    static float catenaryDrop01(float t) noexcept
    {
        constexpr float kHalfWidth = 1.90f;
        const float x        = (2.f * t - 1.f) * kHalfWidth;
        const float coshX    = std::cosh(x);
        const float coshMax  = std::cosh(kHalfWidth);
        const float inverted = coshMax - coshX;
        const float norm     = inverted / juce::jmax(1e-4f, coshMax - 1.f);
        return juce::jlimit(0.f, 1.f, norm);
    }

    static float dripLobes01(float t, float seed) noexcept
    {
        auto gauss = [](float x, float mu, float sigma) -> float
        {
            const float d = (x - mu) / sigma;
            return std::exp(-0.5f * d * d);
        };

        const float j0 = 0.015f * std::sin(seed * 3.1f);
        const float j1 = 0.015f * std::sin(seed * 5.7f + 1.3f);
        const float j2 = 0.015f * std::sin(seed * 2.3f + 2.7f);

        const float v = 0.24f * gauss(t, 0.30f + j0, 0.12f)
                      + 0.46f * gauss(t, 0.50f + j1, 0.13f)
                      + 0.28f * gauss(t, 0.70f + j2, 0.12f);

        return juce::jlimit(0.f, 1.0f, v);
    }
};

} // namespace DAW
