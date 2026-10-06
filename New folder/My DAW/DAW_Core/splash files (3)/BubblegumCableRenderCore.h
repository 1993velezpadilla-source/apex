#pragma once
#include <JuceHeader.h>
#include <vector>
#include "BubblegumCableTypes.h"
#include "BubblegumCableStyleSettingsCore.h"
#include "BubblegumCableVisibilityCore.h"
#include "BubblegumCableMotionStabilityCore.h"
#include "BubblegumCableHangingCore.h"
#include "BubblegumCableAudioReactiveCore.h"
#include "BubblegumCableGeometryCore.h"
#include "BubblegumCableSurfaceShaderCore.h"
#include "BubblegumDropletCore.h"
#include "BubblegumCableImpactSplashCore.h"

namespace bubblegum
{
    //=========================================================================
    // BubblegumCableRenderCore
    //
    // Responsibility: ORCHESTRATION ONLY.
    //
    // Pipeline (in order):
    //   1. Visibility filter         — skip invisible snapshots immediately
    //   2. Motion stability pass     — smooth positions to remove jitter
    //   3. Audio reactive pass       — compute visual multipliers per send
    //   4. Hanging pass              — solve bezier control points
    //   5. Resolved frame build      — assemble CableResolvedFrame
    //   6. Surface shader call       — paint the liquid stream
    //
    // This core NEVER:
    //   - decides routing truth
    //   - decides if a send exists
    //   - decides dot color
    //   - decides send state
    //
    // It only reads CableWorldSnapshot and paints.
    //=========================================================================
    class BubblegumCableRenderCore
    {
    public:
        using VisibilityInputs = BubblegumCableVisibilityCore::Inputs;

        struct RenderStats
        {
            int submitted = 0;  // total snapshots received
            int visible   = 0;  // passed visibility filter
            int rendered  = 0;  // actually painted
        };

        void resetMotion();   // call when routing resets or tracks reorder
        void resetAudio();    // call when all audio state should clear

        void setStyle(const BubblegumCableStyleSettingsCore::Style& newStyle);
        const BubblegumCableStyleSettingsCore::Style& getStyle() const noexcept;

        // Main entry point. Call once per frame from your paint method.
        void paintAll(
            juce::Graphics& g,
            const std::vector<CableWorldSnapshot>& snapshots,
            const VisibilityInputs& visibilityInputs,
            float timeSeconds,
            float dt,
            RenderStats* outStats = nullptr);

    private:
        BubblegumCableStyleSettingsCore   styleCore;
        BubblegumCableVisibilityCore      visibilityCore;
        BubblegumCableMotionStabilityCore motionStabilityCore;
        BubblegumCableHangingCore         hangingCore;
        BubblegumCableAudioReactiveCore   audioReactiveCore;
        BubblegumCableGeometryCore        geometryCore;
        BubblegumDropletCore              dropletCore;
        BubblegumCableImpactSplashCore    impactSplashCore;
        BubblegumCableSurfaceShaderCore   surfaceShaderCore;

        CableResolvedFrame makeResolvedFrame(
            const CableWorldSnapshot& snapshot,
            float timeSeconds,
            float dt) noexcept;
    };

} // namespace bubblegum
