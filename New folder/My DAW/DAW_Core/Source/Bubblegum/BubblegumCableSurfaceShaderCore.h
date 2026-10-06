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
    // Full pass order (dual-quality aware):
    //   1.    Mist halo                   [toggle: enableMistPass]
    //   2.    Depth shadow offset         [always on]
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
    };

} // namespace bubblegum
