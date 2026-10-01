#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimePuddleCore  -- slime accumulation pools
//
// Draws flat organic "puddle" shapes at three points on the cable where
// slime would naturally accumulate under gravity:
//
//   1. Source endpoint  (cable hangs from here)
//   2. Target endpoint  (cable hangs from here)
//   3. Sag midpoint     (lowest point -- most slime pools here)
//
// Each puddle is a flattened ellipse wider than the cable, drawn BEFORE
// the cable body and endpoint balls so the tube appears to emerge from
// a pool of slime. This grounds the cable visually -- it doesn't just
// float in space, it's anchored in gel.
//
// Puddle shape is purely geometry/seed derived (stable when dragging).
// Only alpha and color modulations use time.
// =====================================================================
class BubblegumCableSlimePuddleCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float seed = in.source.x * 0.027f + in.target.x * 0.021f;

        // Endpoint puddles
        paintPuddle(g, geo.sx, geo.sy, geo.thickness, seed + 1.1f, e, time);
        paintPuddle(g, geo.tx, geo.ty, geo.thickness, seed + 2.3f, e, time);

        // Midpoint puddle (largest -- most pooling)
        {
            const auto  s   = sampleAlong(geo, 0.5f);
            // Push puddle to cable underside
            const float px  = s.pos.x - s.normal.x * s.halfW;
            const float py  = s.pos.y - s.normal.y * s.halfW;
            paintPuddle(g, px, py, geo.thickness * 1.4f, seed + 3.7f, e, time);
        }
    }

private:
    const juce::Colour kPuddleHigh   { 0xFFFF80C0 };
    const juce::Colour kPuddleMid    { 0xFFCC2070 };
    const juce::Colour kPuddleDark   { 0xFF1A0308 };
    const juce::Colour kPuddleEdge   { 0xFFFF9AC8 };

    void paintPuddle(juce::Graphics& g,
                     float cx, float cy, float thickness,
                     float seed, float e, float time) const
    {
        // Puddle dimensions: wide flat ellipse
        const float rx = juce::jmax(6.f, thickness * (1.8f + BubblegumCableAnimationCore::stable01(seed) * 0.8f)
                                        + e * thickness * 0.3f);
        const float ry = juce::jmax(2.f, thickness * (0.5f + BubblegumCableAnimationCore::stable01(seed + 1.3f) * 0.3f));

        // Slight organic squish along one axis (seed-stable)
        const float squishX = 0.88f + BubblegumCableAnimationCore::stable01(seed + 2.1f) * 0.24f;
        const float prx = rx * squishX;
        const float pry = ry * (2.f - squishX);  // compensate area

        // Alpha pulse (time in alpha only -- shape is stable)
        const float pulse = 0.80f + 0.20f * std::sin(time * 0.7f + seed * 3.1f);

        // Shadow underneath puddle
        g.setColour(juce::Colour(0x33000000));
        g.fillEllipse(cx - prx * 0.9f, cy + pry * 0.3f, prx * 1.8f, pry * 1.0f);

        // Puddle body gradient (radial, centre bright -> edge dark)
        {
            juce::ColourGradient grad(
                kPuddleHigh.withAlpha((0.70f + e * 0.15f) * pulse), cx, cy,
                kPuddleDark.withAlpha((0.30f + e * 0.08f) * pulse),
                cx + prx, cy, true);
            grad.addColour(0.45, kPuddleMid.withAlpha((0.55f + e * 0.10f) * pulse));
            g.setGradientFill(grad);
            g.fillEllipse(cx - prx, cy - pry, prx * 2.f, pry * 2.f);
        }

        // Wet edge ring
        g.setColour(kPuddleEdge.withAlpha((0.40f + e * 0.10f) * pulse));
        g.drawEllipse(cx - prx, cy - pry, prx * 2.f, pry * 2.f, 0.8f);

        // Small specular glint on puddle surface
        {
            const float sr = juce::jmax(0.8f, prx * 0.12f);
            g.setColour(juce::Colour(0xFFFFFFFF).withAlpha(0.55f * pulse));
            g.fillEllipse(cx - prx * 0.22f - sr, cy - pry * 0.30f - sr * 0.5f,
                          sr * 2.f, sr);
        }
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
