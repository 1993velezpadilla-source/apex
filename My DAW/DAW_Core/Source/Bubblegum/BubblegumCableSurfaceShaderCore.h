#pragma once
#include <JuceHeader.h>
#include "BubblegumCableTypes.h"
#include "BubblegumCableStyleSettingsCore.h"
#include "BubblegumCableGeometryCore.h"
#include "BubblegumDropletCore.h"
#include "BubblegumCableImpactSplashCore.h"

namespace bubblegum
{
    //=========================================================================
    // BubblegumCableSurfaceShaderCore
    //
    // Responsibility: paint ALL visual layers of ONE liquid stream.
    //
    // Cable-type dispatch:
    //   CableType::NormalSend → paintArcStreamNormalSend (ARC STREAM violet plasma)
    //   CableType::MasterSend → existing liquid stream path below (unchanged)
    //   CableType::Sidechain    → separate Sidechain renderer (not here)
    //
    // Full pass order for Master/legacy (dual-quality aware):
    //   1.    Moist dream                   [toggle: enableMistPass]
    //   2.    Depth honeydew offset         [always on]
    //   2.5   Cinematic scatter glow      [Cinematic only + toggle]
    //   3.    Main body gradient          [always on]
    //   3.5   Cinematic depth aura        [Cinematic only + toggle]
    //   3.6   Anti-vector imperfection    [Cinematic only]
    //   4.    Axial fade                  [always on]
    //   5a.   Inner core gradient         [always on]
    //   5b.   Audio-reactive core glow    [always on]
    //   5c.   Inner flow density line     [toggle: enableInnerFlowDensityLine]
    //   6.    Underside shadow line       [toggle: enableUndersideShadowLine]
    //   7.    Specular lump flares        [toggle: enableSpecularFlares]
    //   8.    Top highlight ridge         [always on — identity pass]
    //   8.5   Cinematic micro sheen       [Cinematic only + toggle]
    //   9.    Splash drips + droplets     [toggles]
    //   9.5   Endpoint impact splashes    [toggle: enableImpactSplash]
    //   10.   Body outline                [toggle: enableOutline]
    //=========================================================================
    class BubblegumCableSurfaceShaderCore
    {
    public:
        // steps: resolved by render core from styleCore.getRecommendedSteps()
        void paintLiquidStream(
            juce::Graphics& g,
            const CableResolvedFrame& frame,
            const BubblegumCableGeometryCore& geometry,
            const BubblegumDropletCore& dropletCore,
            const BubblegumCableImpactSplashCore& impactSplashCore,
            const BubblegumCableStyleSettingsCore::Style& style,
            int steps = 80) const;

    private:
        // ARC STREAM normal Send/Aux cable renderer.
        // Violet electrical plasma with stable spine, moving threads,
        // directional packets, and broken-ring electrical sockets.
        // All geometry reused from the pre-cached frame.
        void paintArcStreamNormalSend(
            juce::Graphics& g,
            const CableResolvedFrame& frame,
            const BubblegumCableGeometryCore& geometry,
            const BubblegumCableStyleSettingsCore::Style& style,
            int steps,
            float timeSeconds) const;
    };

} // namespace bubblegum
