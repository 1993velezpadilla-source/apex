#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include <unordered_set>
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
            int cacheHits = 0;
            int cacheMisses = 0;
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
            RenderStats* outStats = nullptr,
            bool snapMotionNow = false);

    private:
        struct CableCacheKey
        {
            juce::int64 edgeId = 0;
            int qSrcX = 0, qSrcY = 0;
            int qCtrlAX = 0, qCtrlAY = 0;
            int qCtrlBX = 0, qCtrlBY = 0;
            int qDstX = 0, qDstY = 0;
            int qThickness = 0;
            int qSendLevel = 0;
            int styleVersion = 0;
            int stateId = 0;
            int isMasterTarget = 0;
            int cableType = 0;       // (int)CableType
            int qualityMode = 0;
            int steps = 0;

            bool operator==(const CableCacheKey& op) const noexcept
            {
                return edgeId == op.edgeId
                    && qSrcX == op.qSrcX && qSrcY == op.qSrcY
                    && qCtrlAX == op.qCtrlAX && qCtrlAY == op.qCtrlAY
                    && qCtrlBX == op.qCtrlBX && qCtrlBY == op.qCtrlBY
                    && qDstX == op.qDstX && qDstY == op.qDstY
                    && qThickness == op.qThickness
                    && qSendLevel == op.qSendLevel
                    && styleVersion == op.styleVersion
                    && stateId == op.stateId
                    && isMasterTarget == op.isMasterTarget
                    && cableType == op.cableType
                    && qualityMode == op.qualityMode
                    && steps == op.steps;
            }
        };

        struct CableCacheEntry
        {
            juce::Image image;
            CableCacheKey key;
            juce::Rectangle<int> bounds;
            int lastUsedFrame = 0;
        };

        static constexpr size_t kMaxCachedCables = 128;

        mutable std::unordered_map<juce::int64, CableCacheEntry> cableCache_;
        mutable int frameCounter_ = 0;
        int styleVersion_ = 1;

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

        CableCacheKey makeCacheKey(const CableResolvedFrame& frame, int steps) const noexcept;
        juce::Rectangle<int> makeCacheBounds(const CableResolvedFrame& frame) const noexcept;
        void evictCacheEntries(const std::unordered_set<juce::int64>& touchedThisFrame) const;
    };

} // namespace bubblegum
