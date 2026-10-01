#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeStringCore  -- thin hanging slime threads
//
// Renders 4-8 hair-thin slime strands that hang from the cable underside
// like threads of honey or spider silk covered in gel.
//
// Each strand:
//   - Anchored at a fixed cable-relative point (stable, no jumping)
//   - Hangs straight down with a gentle catenary droop
//   - Tapers from ~1px at the anchor to a point at the tip
//   - Has a tiny terminal droplet bead at the bottom
//   - Slowly sways left-right with its own frequency
//   - Length: 4-12px (much shorter than drip beads)
//
// These strands are the "connective tissue" of the slime -- they make
// the cable look dripping and viscous even at rest.
// =====================================================================
class BubblegumCableSlimeStringCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float seed = in.source.x * 0.053f + in.target.x * 0.037f + 13.7f;

        const int count = 4 + (int)(BubblegumCableAnimationCore::stable01(seed) * 4.f); // 4-7

        for (int i = 0; i < count; ++i)
        {
            const float fs = seed + (float)i * 5.9f;

            // Stable position along cable
            const float t = juce::jmap(
                BubblegumCableAnimationCore::stable01(fs + 1.3f), 0.10f, 0.90f);

            const auto  s    = sampleAlong(geo, t);
            const float hw   = s.halfW;

            // Strand length varies per strand
            const float maxLen = 4.f + BubblegumCableAnimationCore::stable01(fs + 3.1f) * 8.f
                               + e * 3.f;

            // Slow sway animation (each strand has own phase & speed)
            const float swayFreq  = 0.5f + BubblegumCableAnimationCore::stable01(fs + 4.7f) * 0.6f;
            const float swayAmp   = 1.2f + BubblegumCableAnimationCore::stable01(fs + 6.3f) * 2.0f;
            const float swayPhase = BubblegumCableAnimationCore::stable01(fs + 8.1f)
                                  * juce::MathConstants<float>::twoPi;
            const float sway      = swayAmp * std::sin(time * swayFreq + swayPhase);

            // Anchor at cable bottom edge
            const float ax = s.pos.x - s.normal.x * hw;
            const float ay = s.pos.y - s.normal.y * hw;

            // Thread tip (bottom of strand) -- downward = away from normal, plus sway
            // "down" in 2D space is +Y, sway is along the cable tangent
            const float tipX = ax - s.normal.x * maxLen + s.tangent.x * sway;
            const float tipY = ay - s.normal.y * maxLen + s.tangent.y * sway;

            // Mid control point for slight catenary droop
            const float midX = (ax + tipX) * 0.5f + s.tangent.x * sway * 0.3f;
            const float midY = (ay + tipY) * 0.5f - s.normal.y * maxLen * 0.15f;

            // ── Strand body: tapered quadratic bezier ──────────────────────
            {
                // Draw as stroked path with reduced width, tapered by segment
                const int   segs    = 8;
                const float topW    = juce::jmax(0.7f, hw * 0.32f);
                for (int si = 0; si < segs; ++si)
                {
                    const float t0 = (float)si       / (float)segs;
                    const float t1 = (float)(si + 1) / (float)segs;
                    const float strokeW = juce::jmap(t1, topW, 0.25f);  // taper to tip

                    // Evaluate quadratic bezier at t0 and t1
                    auto bezier = [&](float bt) -> juce::Point<float>
                    {
                        const float ia = 1.f - bt;
                        return {
                            ia * ia * ax  + 2.f * ia * bt * midX + bt * bt * tipX,
                            ia * ia * ay  + 2.f * ia * bt * midY + bt * bt * tipY
                        };
                    };
                    const auto p0 = bezier(t0);
                    const auto p1 = bezier(t1);

                    // Alpha: fades toward tip
                    const float alpha = juce::jmap(t1, 0.92f, 0.30f);
                    g.setColour(kString.withAlpha(alpha));

                    juce::Path seg;
                    seg.startNewSubPath(p0.x, p0.y);
                    seg.lineTo(p1.x, p1.y);
                    g.strokePath(seg, juce::PathStrokeType(strokeW,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }

            // ── Terminal droplet bead at tip ───────────────────────────────
            {
                const float dr = juce::jmax(0.9f, hw * 0.40f);
                // Shadow
                g.setColour(juce::Colour(0x33000000));
                g.fillEllipse(tipX - dr * 0.8f, tipY + dr * 0.35f, dr * 1.6f, dr * 0.8f);
                // Body
                juce::ColourGradient grad(
                    kBeadHigh, tipX, tipY - dr * 0.3f,
                    kBeadDark, tipX, tipY + dr, false);
                grad.addColour(0.45, kBeadMid);
                g.setGradientFill(grad);
                g.fillEllipse(tipX - dr, tipY - dr, dr * 2.f, dr * 2.f);
                // Glint
                g.setColour(juce::Colour(0xFFFFFFFF));
                g.fillEllipse(tipX - dr * 0.28f - 0.6f, tipY - dr * 0.35f - 0.6f,
                              1.2f, 1.2f);
            }
        }
    }

private:
    const juce::Colour kString   { 0xFFD43A8A };
    const juce::Colour kBeadHigh { 0xFFFFD6EC };
    const juce::Colour kBeadMid  { 0xFFEE4FA0 };
    const juce::Colour kBeadDark { 0xFF2A0612 };

    struct Sample
    {
        juce::Point<float> pos, normal, tangent;
        float halfW = 0.f;
    };

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
