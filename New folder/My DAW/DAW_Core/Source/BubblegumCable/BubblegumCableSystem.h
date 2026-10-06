#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableGeometryCore.h"
#include "BubblegumCableRenderCore.h"
#include "BubblegumCableEffectsCore.h"
#include "BubblegumCableInteractionCore.h"
#include "BubblegumCableDebugCore.h"
#include "BubblegumCableAnimationCore.h"
#include "BubblegumCableLiquidCore.h"
#include "BubblegumCableSlimeLayerCore.h"
#include "BubblegumCableSlimeBodyCore.h"
#include "BubblegumCableSlimeDripCore.h"
#include "BubblegumCableSlimeTopBlobCore.h"
#include "BubblegumCableSlimeGlossCore.h"
#include "BubblegumCableSlimePoolCore.h"
#include "BubblegumCableSlimeSkinCore.h"
#include "BubblegumCableSlimeTendrilCore.h"
#include "BubblegumCableRenderCacheCore.h"
#include "BubblegumCableGravityCore.h"
#include "BubblegumCable25DLightingCore.h"
#include "BubblegumCableSlimeFlowCore.h"
#include "BubblegumCableSlimeFillCore.h"
#include "BubblegumCableSlimeRippleCore.h"
#include "BubblegumCableSlimeSplashCore.h"
#include "BubblegumCablePulseGlowCore.h"
#include "BubblegumCableTravelingPulseCore.h"
#include "BubblegumCableSlimeBlobsCore.h"
#include "BubblegumCableSlimeDripBeadCore.h"
#include "BubblegumCableSlimeCoatCore.h"
#include "BubblegumCableSlimeStringCore.h"
#include "BubblegumCableSlimeOverlayCore.h"
#include "BubblegumCableSlimeShimmerCore.h"
#include "BubblegumCableFresnelRimCore.h"
#include "BubblegumCableSlimeWebCore.h"
#include "BubblegumCableSlimePuddleCore.h"
#include "BubblegumCableInnerLightCore.h"
#include "BubblegumCableDepthHazeCore.h"
#include "BubblegumCableCausticCore.h"
#include "BubblegumCableGelRefractionCore.h"

namespace DAW {

// =====================================================================
// BubblegumCableSystem — orchestrator only.
//
// Owns the five cable nucleos and runs the strict pipeline every frame:
//
//   1. Build geometry     (GeometryCore)
//   2. Paint base cable   (RenderCore)         <-- GUARANTEED VISIBLE
//   3. Paint effects      (EffectsCore)        <-- optional
//   4. Paint debug overlay(DebugCore)          <-- off by default
//
// If any layer after (2) fails, the cable still renders. That is the
// central contract of this system.
//
// Back-compat types (Cable / TrackHub) are provided as thin aliases so
// existing callers (BubblegumCableOverlayComponent, BubblegumV2System)
// can switch to this core with zero field-name churn.
// =====================================================================
class BubblegumCableSystem
{
public:
    using Cable    = BubblegumCableInput;
    using TrackHub = BubblegumTrackHubInfo;

    // ── Lifecycle ────────────────────────────────────────────────────────

    /** Per-frame update. Advances all nucleos. */
    void tick(float deltaMs, float pulseAlpha) noexcept
    {
        pulse_ = pulseAlpha;
        anim_.tick(deltaMs);
        liquid_.update(deltaMs * 0.001f, pulseAlpha);
        effects_.update(deltaMs, pulseAlpha);
        splash_.update(deltaMs * 0.001f);
    }

    void reset() noexcept
    {
        effects_.reset();
        liquid_.reset();
        splash_.reset();
        anim_.reset();
        interaction_.clear();
        pulse_ = 0.f;
        geometryMoving_ = false;
    }

    // No-op kept for callers that still invoke invalidateDroplets()
    void invalidateDroplets() noexcept
    {
        liquid_.invalidateDroplets();
        effects_.invalidateDroplets();
        renderCache_.invalidateAll();
    }

    // ── Paint (matches legacy call surface) ─────────────────────────────

    void paint(juce::Graphics& g,
               const std::vector<Cable>& cables,
               const std::vector<TrackHub>& /*hubs*/,
               float laneBottomY) const
    {
        const float time  = visualTime_ >= 0.f ? visualTime_ : anim_.time();

        for (const auto& c : cables)
        {
            if (!c.shouldDraw()) continue;
            const float e    = juce::jlimit(0.f, 1.f, c.sendEnergy);
            const float seed = BubblegumCableAnimationCore::cableSeed(c);
            const auto  geo  = geometry_.buildGeometry(c, laneBottomY, pulse_);

            // Stable structural pipeline
            pulseGlow_    .paint(g, geo, c, time);
            slimePuddles_ .paint(g, geo, c, time);
            slimeCoat_    .paint(g, geo, c, time);
            render_       .paint(g, geo, c, pulse_);
            innerLight_   .paint(g, geo, c, time);
            depthHaze_    .paint(g, geo, c, time);
            fresnelRim_   .paint(g, geo, c, time);
            slimeOverlay_ .paint(g, geo, c, time);

            // Settled-only premium detail pipeline
            if (!geometryMoving_)
            {
                caustics_       .paint(g, geo, c, time);
                gelRefraction_  .paint(g, geo, c, time);
                slimeWeb_       .paint(g, geo, c, time);
                slimeStrings_   .paint(g, geo, c, time);
                slimeBlobs_     .paint(g, geo, c, time);
                slimeDripBeads_ .paint(g, geo, c, time);
                slimeShimmer_   .paint(g, geo, c, time);
                travelingPulse_ .paint(g, geo, c, time);
            }

            debug_.paint(g, geo);

            // ── Toon ink contour — always drawn last so it sits on top of ALL layers ──
            // A solid black stroke around the cable silhouette gives the 2.5D cartoon
            // read visible in the reference image (thick black outline like a cel drawing).
            {
                const juce::Colour kInk { 0xFF060608 };
                const float inkAlpha = 0.80f + e * 0.10f;
                // Wide outer silhouette first (renders behind the thinner inner line)
                g.setColour(kInk.withAlpha(inkAlpha * 0.55f));
                g.strokePath(geo.ribbon, juce::PathStrokeType(3.2f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                // Crisp inner ink line
                g.setColour(kInk.withAlpha(inkAlpha));
                g.strokePath(geo.ribbon, juce::PathStrokeType(1.6f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                // Endpoint ball toon ring
                const float br = juce::jmax(7.f, geo.thickness * 0.78f);
                g.setColour(kInk.withAlpha(inkAlpha));
                g.drawEllipse(geo.sx - br, geo.sy - br, br * 2.f, br * 2.f, 1.8f);
                g.drawEllipse(geo.tx - br, geo.ty - br, br * 2.f, br * 2.f, 1.8f);
            }

            juce::ignoreUnused(seed, e);
        }
    }

    /** Back-compat overload: no hubs. */
    void paint(juce::Graphics& g,
               const std::vector<Cable>& cables,
               float laneBottomY) const
    {
        paint(g, cables, {}, laneBottomY);
    }

    // ── Public access to sub-cores ──────────────────────────────────────

    BubblegumCableGeometryCore&    geometry()    noexcept { return geometry_; }
    BubblegumCableRenderCore&      render()      noexcept { return render_; }
    BubblegumCableEffectsCore&     effects()     noexcept { return effects_; }
    BubblegumCableInteractionCore& interaction() noexcept { return interaction_; }
    BubblegumCableDebugCore&       debug()       noexcept { return debug_; }
    BubblegumCableAnimationCore&   animation()   noexcept { return anim_; }
    BubblegumCableLiquidCore&      liquid()      noexcept { return liquid_; }
    BubblegumCableSlimeLayerCore&  slime()       noexcept { return slime_; }
    BubblegumCableSlimeBodyCore&   slimeBody()   noexcept { return slimeBody_; }
    BubblegumCableSlimeDripCore&   slimeDrips()  noexcept { return slimeDrips_; }

    const BubblegumCableEffectsCore& effects() const noexcept { return effects_; }
    const BubblegumCableDebugCore&   debug()   const noexcept { return debug_; }

    void setVisualTime(float time) noexcept { visualTime_ = time; }
    void setGeometryMoving(bool moving) noexcept { geometryMoving_ = moving; }

private:
    void paintLiquidBeads(juce::Graphics& g,
                          const BubblegumCableGeometry& geo,
                          const BubblegumCableInput& in,
                          float e) const
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(geo.ribbon);

        liquid_.forEachAliveParticle(in.id(), [&](const BubblegumCableLiquidCore::Particle& p)
        {
            const auto pos = BubblegumCableSlimeLayerCore::resolveParticlePos(
                geo, p.laneT, p.normalBias);

            const float ct = juce::jlimit(0.f, 1.f, p.laneT);
            const float fi = ct * (float)BubblegumCableGeometry::kSeg;
            const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1,
                                        (int)std::floor(fi));
            const float hw = geo.points[(size_t)i0].halfW;

            BubblegumCableSlimeLayerCore::paintLiquidBead(g, pos, hw, p.radius, p.alpha(), e);
        });
    }

    void spawnLiquidBeads(const BubblegumCableInput& in,
                          const BubblegumCableGeometry& geo,
                          float time, float e, float seed) const
    {
        const int blobCount = 4 + (int)std::floor(
            BubblegumCableAnimationCore::stable01(seed + 1.7f) * 3.f);

        const float spawnSeed = seed + time * 0.013f;
        if (!liquid_.shouldSpawnThisTick(in.id(), e, spawnSeed, 0.016f))
            return;

        const int blobIdx = (int)(BubblegumCableAnimationCore::stable01(spawnSeed + 9.1f)
                                 * (float)blobCount);
        const float laneT = BubblegumCableAnimationCore::blobLaneT(
            time, seed, blobIdx, blobCount, 1.6f);

        liquid_.spawnInBlobLane(in.id(), laneT, blobIdx, e,
                                seed + time * 0.077f + (float)blobIdx * 3.1f);
    }

    void maybeSpawnSplash(const BubblegumCableInput& in,
                          const BubblegumCableGeometry& geo,
                          float time, float e, float seed) const
    {
        // Spawn a splash burst roughly once every 2s per drip, randomised
        const float spawnSeed = seed + time * 0.051f;
        const float chance    = (0.008f + e * 0.012f) * 0.016f;
        if (BubblegumCableAnimationCore::stable01(spawnSeed) > chance) return;

        const int dripCount = 3 + (int)(BubblegumCableAnimationCore::stable01(seed + 0.3f) * 3.f);
        const int dripIdx   = (int)(BubblegumCableAnimationCore::stable01(spawnSeed + 5.1f)
                                   * (float)dripCount);
        const float dripT   = juce::jlimit(0.18f, 0.82f,
            juce::jmap((float)(dripIdx + 1) / (float)(dripCount + 1), 0.18f, 0.82f));

        splash_.spawnSplash(dripT, in.id(), e, spawnSeed + 1.7f);
    }

    BubblegumCableGeometryCore       geometry_;
    BubblegumCableRenderCore         render_;
    BubblegumCablePulseGlowCore      pulseGlow_;
    BubblegumCableTravelingPulseCore travelingPulse_;
    BubblegumCableSlimeBlobsCore     slimeBlobs_;
    BubblegumCableSlimeDripBeadCore  slimeDripBeads_;
    BubblegumCableSlimeCoatCore      slimeCoat_;
    BubblegumCableSlimeStringCore    slimeStrings_;
    BubblegumCableSlimeOverlayCore   slimeOverlay_;
    BubblegumCableSlimeShimmerCore   slimeShimmer_;
    BubblegumCableFresnelRimCore     fresnelRim_;
    BubblegumCableSlimeWebCore       slimeWeb_;
    BubblegumCableSlimePuddleCore    slimePuddles_;
    BubblegumCableInnerLightCore     innerLight_;
    BubblegumCableDepthHazeCore      depthHaze_;
    BubblegumCableCausticCore        caustics_;
    BubblegumCableGelRefractionCore  gelRefraction_;
    mutable BubblegumCableEffectsCore      effects_;
    BubblegumCableInteractionCore    interaction_;
    BubblegumCableDebugCore          debug_;
    BubblegumCableAnimationCore      anim_;
    mutable BubblegumCableLiquidCore       liquid_;
    BubblegumCableSlimeLayerCore     slime_;
    BubblegumCableSlimeBodyCore      slimeBody_;
    mutable BubblegumCableSlimeDripCore    slimeDrips_;
    BubblegumCableSlimeTopBlobCore   slimeTopBlobs_;
    BubblegumCableSlimeGlossCore     slimeGloss_;
    BubblegumCableSlimePoolCore      slimePool_;
    BubblegumCableSlimeSkinCore      slimeSkin_;
    BubblegumCableSlimeTendrilCore   slimeTendrils_;
    mutable BubblegumCableRenderCacheCore  renderCache_;
    BubblegumCable25DLightingCore    lighting25D_;
    BubblegumCableSlimeFlowCore      slimeFlow_;
    BubblegumCableSlimeFillCore      slimeFill_;
    BubblegumCableSlimeRippleCore    slimeRipple_;
    mutable BubblegumCableSlimeSplashCore  splash_;

    float pulse_ = 0.f;
    float visualTime_ = -1.f;
    bool  geometryMoving_ = false;
};

} // namespace DAW
