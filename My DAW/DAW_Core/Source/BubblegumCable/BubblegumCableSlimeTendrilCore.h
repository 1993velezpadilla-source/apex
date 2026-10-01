#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include "BubblegumCableSlimeDripCore.h"
#include <cmath>
#include <array>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeTendrilCore
//
// Draws thin connecting strings between adjacent drip tips, and between
// each drip and the cable underside.
//
// Like real slime: when you pull slime apart, thin "tendrils" stretch
// between the main body and the drip fingers. These are thin curved
// bezier lines with a small amount of gloss.
//
// The tendril shape:
//   - Starts at cable underside (neck of drip)
//   - Curves toward the tip of the adjacent drip
//   - Very thin (0.5-1.5px) so it reads as stretch, not a new shape
//
// All positions are cable-local (t-space) — scroll safe.
// =====================================================================
class BubblegumCableSlimeTendrilCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time,
               float                         energy,
               const BubblegumCableSlimeDripCore& drips) const
    {
        const float e    = juce::jlimit(0.f, 1.f, energy);
        const float seed = BubblegumCableAnimationCore::cableSeed(in);

        // For each adjacent drip pair, draw a thin connecting tendril
        const int n = drips.count();
        for (int i = 0; i < n - 1; ++i)
        {
            const auto a = drips.tipPosition(geo, i);
            const auto b = drips.tipPosition(geo, i + 1);
            if (!a.valid || !b.valid) continue;

            const float si  = seed + (float)i * 7.3f;
            const float sag = 6.0f + BubblegumCableAnimationCore::stable01(si) * 8.0f
                            + e * 4.0f;
            // slight animation
            const float animSag = sag * (1.0f + 0.12f * std::sin(
                time * (1.1f + 0.08f * (float)i)
                + BubblegumCableAnimationCore::stable01(si + 1.7f)
                * juce::MathConstants<float>::twoPi));

            // Control point sags downward between tips
            const float cpx = (a.x + b.x) * 0.5f;
            const float cpy = (a.y + b.y) * 0.5f + animSag;

            juce::Path tendril;
            tendril.startNewSubPath(a.x, a.y);
            tendril.quadraticTo(cpx, cpy, b.x, b.y);

            const float alpha = 0.30f + e * 0.10f;
            g.setColour(kTendril.withAlpha(alpha));
            g.strokePath(tendril, juce::PathStrokeType(
                0.5f + BubblegumCableAnimationCore::stable01(si + 0.3f) * 0.8f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

private:
    const juce::Colour kTendril { 0xFFFF6FB0 };
};

} // namespace DAW
