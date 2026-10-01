#pragma once
#include "BubblegumCableTypes.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCablePulseGlowCore  -- premium outer aura / glow halo
//
// Radiates a thick colored energy field outward from the cable body.
// This is drawn BEFORE the body so the solid tube always sits on top.
//
// Three concentric halos:
//   1. Wide diffuse outer aura  (reacts strongly to send energy)
//   2. Mid glow ring            (pulsing at ~1.2 Hz)
//   3. Tight inner corona       (always visible, confirms cable is live)
//
// The glow uses the ribbon outline stroke at increasing widths so it
// perfectly follows the cable shape, including endpoint balls.
// =====================================================================
class BubblegumCablePulseGlowCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput& in,
               float time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e     = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float pulse = 0.60f + 0.40f * std::sin(time * 1.2f * juce::MathConstants<float>::twoPi
                                                       + geo.sx * 0.008f);
        const float act   = juce::jlimit(0.f, 1.f, in.activationPulse);

        // ── Outer diffuse aura ────────────────────────────────────────────
        {
            const float w = 6.f + e * 5.f + act * 3.f;
            g.setColour(kAuraOuter.withAlpha((0.12f + e * 0.12f) * pulse));
            g.strokePath(geo.ribbon, juce::PathStrokeType(w,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── Mid glow ring ─────────────────────────────────────────────────
        {
            const float w = 3.f + e * 2.f;
            const float p2 = 0.70f + 0.30f * std::sin(time * 2.1f + geo.tx * 0.009f);
            g.setColour(kAuraMid.withAlpha((0.26f + e * 0.16f) * p2));
            g.strokePath(geo.ribbon, juce::PathStrokeType(w,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── Tight inner corona (always live) ─────────────────────────────
        {
            const float w = 1.5f + e * 1.0f;
            g.setColour(kCorona.withAlpha(0.50f + e * 0.24f + act * 0.20f));
            g.strokePath(geo.ribbon, juce::PathStrokeType(w,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── Endpoint ball auras ────────────────────────────────────────────
        paintBallAura(g, geo.sx, geo.sy, geo.thickness, e, pulse, act);
        paintBallAura(g, geo.tx, geo.ty, geo.thickness, e, pulse, act);
    }

private:
    const juce::Colour kAuraOuter { 0xFFFF4FA0 };  // hot pink outer fog
    const juce::Colour kAuraMid   { 0xFFFF80C0 };  // mid glow
    const juce::Colour kCorona    { 0xFFFFB8DC };  // tight bright corona

    void paintBallAura(juce::Graphics& g,
                       float cx, float cy, float thickness,
                       float e, float pulse, float act) const
    {
        const float r = juce::jmax(7.f, thickness * 0.78f);

        const float auraR = r + 4.f + e * 3.f;
        g.setColour(kAuraOuter.withAlpha((0.14f + e * 0.14f) * pulse));
        g.fillEllipse(cx - auraR, cy - auraR, auraR * 2.f, auraR * 2.f);

        const float midR = r + 2.f + e * 2.f;
        g.setColour(kAuraMid.withAlpha(0.28f + e * 0.18f + act * 0.15f));
        g.fillEllipse(cx - midR, cy - midR, midR * 2.f, midR * 2.f);
    }
};

} // namespace DAW
