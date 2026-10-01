#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableTravelingPulseCore  -- energy orb traveling source->target
//
// A bright solid orb of light that rides along the cable centerline,
// looping continuously from source endpoint to target.
//
// At zero energy: single dim orb, slow travel.
// At full energy:  two bright orbs, fast travel, larger radius.
//
// The orb is drawn AFTER the body so it appears to sit ON the surface
// of the tube like a bead of light rolling through a neon tube.
//
// Each orb is a solid ellipse (aligned with the cable tangent) with:
//   - An outer glow ellipse  (soft, slightly larger)
//   - A solid bright core    (opaque, no fade-to-transparent)
//   - A tiny specular dot    (pure white, off-center)
// =====================================================================
class BubblegumCableTravelingPulseCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e     = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float speed = 0.14f + e * 0.28f;

        // Primary orb
        {
            const float phase = BubblegumCableAnimationCore::frac(time * speed);
            paintOrb(g, geo, phase, e, 1.0f);
        }

        // Secondary orb at half energy (offset by 0.5)
        if (e > 0.15f)
        {
            const float phase2 = BubblegumCableAnimationCore::frac(time * speed + 0.5f);
            paintOrb(g, geo, phase2, e * 0.70f, 0.72f);
        }
    }

private:
    const juce::Colour kOrbCore    { 0xFFFFEEF8 };  // near-white solid core
    const juce::Colour kOrbMid     { 0xFFFFB8DC };  // mid pink
    const juce::Colour kOrbGlow    { 0xFFFF6FB0 };  // outer glow color
    const juce::Colour kOrbSpecular{ 0xFFFFFFFF };  // pure white glint

    void paintOrb(juce::Graphics& g,
                  const BubblegumCableGeometry& geo,
                  float phase, float e, float scale) const
    {
        const auto s    = sampleAlong(geo, phase);
        const float ang = std::atan2(s.tangent.y, s.tangent.x);
        const float hw  = s.halfW * scale;

        const float orbW = juce::jmax(4.f,  hw * (1.4f + e * 0.6f));
        const float orbH = juce::jmax(2.5f, hw * (0.9f + e * 0.2f));

        // Outer glow ellipse (semi -- outside the orb body, atmospheric)
        {
            const float glowW = orbW * 2.2f;
            const float glowH = orbH * 2.0f;
            juce::Path glow;
            glow.addEllipse(-glowW * 0.5f, -glowH * 0.5f, glowW, glowH);
            glow.applyTransform(
                juce::AffineTransform::rotation(ang).translated(s.pos.x, s.pos.y));
            g.setColour(kOrbGlow.withAlpha(0.28f + e * 0.14f));
            g.fillPath(glow);
        }

        // Solid opaque orb core
        {
            juce::Path orb;
            orb.addEllipse(-orbW * 0.5f, -orbH * 0.5f, orbW, orbH);
            orb.applyTransform(
                juce::AffineTransform::rotation(ang).translated(s.pos.x, s.pos.y));

            juce::ColourGradient grad(
                kOrbCore, s.pos.x, s.pos.y - orbH * 0.3f,
                kOrbMid,  s.pos.x, s.pos.y + orbH * 0.5f, false);
            grad.addColour(0.55, kOrbGlow);
            g.setGradientFill(grad);
            g.fillPath(orb);
        }

        // Tiny specular dot (solid white, no transparency)
        {
            const float sr = juce::jmax(1.5f, hw * 0.28f);
            // Rotate offset to follow cable angle
            const float ox = -std::sin(ang) * orbH * 0.28f - std::cos(ang) * orbW * 0.15f;
            const float oy = -std::cos(ang) * orbH * 0.28f + std::sin(ang) * orbW * 0.15f;
            g.setColour(kOrbSpecular);
            g.fillEllipse(s.pos.x + ox - sr, s.pos.y + oy - sr, sr * 2.f, sr * 2.f);
        }
    }

    struct Sample { juce::Point<float> pos, tangent, normal; float halfW = 0.f; };

    static Sample sampleAlong(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float ft = juce::jlimit(0.f, 1.f, t) * (float)BubblegumCableGeometry::kSeg;
        const int   i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)ft);
        const int   i1 = juce::jmin(BubblegumCableGeometry::kSeg, i0 + 1);
        const float a  = ft - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        Sample s;
        s.pos    = { juce::jmap(a, p0.x,  p1.x),  juce::jmap(a, p0.y,  p1.y) };
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
