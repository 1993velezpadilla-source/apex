#pragma once
#include "BubblegumCableTypes.h"
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableInnerLightCore  -- subsurface light emission
//
// Simulates a warm light source INSIDE the gel tube, like a neon
// light-pipe or a fiber-optic cable coated in slime. The interior
// glows from within -- light scatters outward through the translucent
// gel body.
//
// Three concentric centerline strokes at decreasing widths:
//   1. Wide warm inner bloom  (fills the tube interior)
//   2. Mid bright core        (follows centerline closely)
//   3. Tight hot filament     (the light source itself)
//
// Drawn AFTER the body (render_) so it composites ON TOP of the opaque
// tube fill, clipped to the ribbon so no light leaks outside.
//
// Color temperature shifts slowly: warm pink -> cool white -> warm pink
// over an 8s cycle. More energy = brighter and cooler (whiter) light.
// =====================================================================
class BubblegumCableInnerLightCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;
        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);

        // Slow color temperature cycle (shape independent of time)
        const float cycle = std::fmod(time * 0.125f, 1.0f);  // 8s period
        const float warm  = 0.70f + 0.30f * std::cos(cycle * juce::MathConstants<float>::twoPi);
        // warm=1 -> kWarm, warm=0 -> kCool
        const juce::Colour lightCol = kWarm.interpolatedWith(kCool, 1.f - warm);

        const float brightness = 0.55f + e * 0.25f;

        const auto core = makeCenterPath(geo);

        // All clipped inside the ribbon
        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        // ── 1. Wide inner bloom (fills ~80% of tube interior) ─────────────
        {
            const float w = geo.thickness * 0.75f;
            g.setColour(lightCol.withAlpha(0.18f * brightness));
            g.strokePath(core, juce::PathStrokeType(w,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── 2. Mid bright core (fills ~40% of tube interior) ─────────────
        {
            const float w = geo.thickness * 0.36f;
            g.setColour(lightCol.withAlpha(0.35f * brightness));
            g.strokePath(core, juce::PathStrokeType(w,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── 3. Tight hot filament ─────────────────────────────────────────
        {
            const float w = juce::jmax(0.6f, geo.thickness * 0.14f);
            const float pulse = 0.88f + 0.12f * std::sin(time * 2.3f + geo.sx * 0.01f);
            g.setColour(kHot.withAlpha((0.65f + e * 0.15f) * pulse));
            g.strokePath(core, juce::PathStrokeType(w,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

private:
    const juce::Colour kWarm { 0xFFFFCCE8 };  // warm pink light
    const juce::Colour kCool { 0xFFF0F0FF };  // cool white light
    const juce::Colour kHot  { 0xFFFFFFFF };  // pure white filament

    static juce::Path makeCenterPath(const BubblegumCableGeometry& geo)
    {
        juce::Path p;
        p.startNewSubPath(geo.points[0].x, geo.points[0].y);
        for (int i = 1; i <= BubblegumCableGeometry::kSeg; ++i)
            p.lineTo(geo.points[i].x, geo.points[i].y);
        return p;
    }
};

} // namespace DAW
