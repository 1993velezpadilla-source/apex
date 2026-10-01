#pragma once
#include <JuceHeader.h>
#include "BubblegumCableTypes.h"
#include "BubblegumCableStyleSettingsCore.h"

namespace bubblegum
{
    //=========================================================================
    // BubblegumCableImpactSplashCore
    //
    // Responsibility: paint the radial burst of pink liquid at the anchor
    // points where the cable meets the scrollbar strip (source exit +
    // destination landing).
    //
    // This is NOT the same as BubblegumDropletCore's drips:
    //   - Drips hang off the BODY of the stream (mid-arc, pulled by gravity)
    //   - Impact splashes BURST at the ANCHOR points (source and destination)
    //
    // Composition per splash:
    //   - Central glow dot (soft outer pink halo)
    //   - Inner bright centre (sharp white-pink highlight)
    //   - N radial rays as tapered teardrops, angularly animated over time
    //
    // Source splash is smaller (liquid squeezing OUT of the track under pressure).
    // Destination splash is larger (liquid LANDING / impacting the receive track).
    //
    // This core only paints. Positions come from the resolved frame's
    // source/destination points. No geometry is cached between frames.
    //=========================================================================
    class BubblegumCableImpactSplashCore
    {
    public:
        // Paint a single radial impact splash at one anchor point.
        //
        //   anchor        : world-space point where the cable meets the anchor strip
        //   thickness     : cable thickness — splash scales with this
        //   timeSeconds   : global time for animation
        //   audioEnergy01 : [0..1] audio hint — boosts pulse size/brightness
        //   intensity     : [0..1] multiplier — 0.65 for source, 1.00 for destination
        //   seed          : arbitrary int used to phase-shift per-ray wobble so
        //                   source and destination splashes animate independently
        //
        // Call twice per cable: once for source, once for destination.
        void paintImpactSplash(
            juce::Graphics& g,
            juce::Point<float> anchor,
            float thickness,
            float timeSeconds,
            float audioEnergy01,
            float sendLevel01,
            float intensity,
            int   seed,
            bool  isDestination,
            const BubblegumCableStyleSettingsCore::Style& style) const;

        /** Returns true when splashes are active (stateless renderer — always
         *  driven by live cables, so this returns true whenever any snapshots
         *  with visible cables exist). Callers use the snapshot list directly;
         *  this method exists for interface symmetry with the gate logic. */
        bool hasActiveSplashes() const noexcept { return false; }

        /** Performance mode: set to 2 to halve drip tail count. Cinematic: keep at 1. */
        void setDripTailDivisor(int divisor) noexcept { dripTailDivisor_ = juce::jmax(1, divisor); }

    private:
        int dripTailDivisor_ = 1;  // 1 = full, 2 = half
    };

} // namespace bubblegum
