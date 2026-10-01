#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimePoolCore
//
// Renders a slime pool / puddle blob at each cable endpoint.
// Like in the reference: where slime connects to a surface, it pools
// into a rounded organic blob rather than ending in a sharp point.
//
// Two pools per cable: source endpoint + target endpoint.
// Each pool is a multi-layer ellipse with gloss, slowly breathing.
// =====================================================================
class BubblegumCableSlimePoolCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time,
               float                         energy) const
    {
        const float e  = juce::jlimit(0.f, 1.f, energy);
        const float s1 = BubblegumCableAnimationCore::cableSeed(in);
        const float s2 = s1 + 99.3f;

        paintPool(g, geo.sx, geo.sy, s1, time, e, geo.thickness, true);
        paintPool(g, geo.tx, geo.ty, s2, time, e, geo.thickness, false);
    }

private:
    const juce::Colour kPoolBase  { 0xFFFF6FB0 };
    const juce::Colour kPoolLit   { 0xFFFFD4EC };
    const juce::Colour kPoolDeep  { 0xFF9B2055 };
    const juce::Colour kGloss     { 0xFFFFF8FC };
    const juce::Colour kAura      { 0xFFFF75BE };

    void paintPool(juce::Graphics& g,
                   float cx, float cy,
                   float seed, float time,
                   float e, float thickness,
                   bool isSource) const
    {
        const float breathe = 1.0f + 0.10f * std::sin(
            time * 1.1f + BubblegumCableAnimationCore::stable01(seed)
                        * juce::MathConstants<float>::twoPi);

        const float r  = juce::jmax(4.5f, thickness * (0.42f + e * 0.15f)) * breathe;
        const float rx = r * (0.90f + BubblegumCableAnimationCore::stable01(seed + 1.1f) * 0.25f);
        const float ry = r * (0.80f + BubblegumCableAnimationCore::stable01(seed + 2.2f) * 0.25f);
        const float ang = BubblegumCableAnimationCore::stable01(seed + 3.3f)
                        * juce::MathConstants<float>::pi * 0.5f;

        // Outer soft aura
        juce::ColourGradient aura(
            kAura.withAlpha(0.12f + e * 0.05f), cx, cy,
            kAura.withAlpha(0.0f),               cx, cy + r * 2.0f, true);
        g.setGradientFill(aura);
        g.fillEllipse(cx - rx * 1.55f, cy - ry * 1.55f, rx * 3.1f, ry * 3.1f);

        // Pool body
        juce::Path body;
        body.addEllipse(-rx, -ry, rx * 2.0f, ry * 2.0f);
        body.applyTransform(juce::AffineTransform::rotation(ang).translated(cx, cy));

        juce::ColourGradient grad(
            kPoolLit .withAlpha(0.88f), cx, cy - ry * 0.4f,
            kPoolDeep.withAlpha(0.92f), cx, cy + ry, false);
        grad.addColour(0.35, kPoolBase.withAlpha(0.90f));
        grad.addColour(0.72, kPoolDeep.withAlpha(0.91f));
        g.setGradientFill(grad);
        g.fillPath(body);

        // Inner rim
        g.setColour(kPoolDeep.withAlpha(0.34f));
        g.strokePath(body, juce::PathStrokeType(0.8f));

        // Gloss cap
        g.setColour(kGloss.withAlpha(0.52f));
        g.fillEllipse(cx - rx * 0.35f, cy - ry * 0.55f, rx * 0.55f, ry * 0.38f);

        // Small secondary gloss
        g.setColour(kGloss.withAlpha(0.28f));
        g.fillEllipse(cx + rx * 0.22f, cy - ry * 0.28f, rx * 0.22f, ry * 0.18f);
    }
};

} // namespace DAW
