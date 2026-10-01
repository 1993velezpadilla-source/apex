#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"
#include "BubblegumCableGeometryCoreV2.h"
#include "BubblegumCableBodyRenderCore.h"
#include "BubblegumCableWaterStreamCore.h"
#include "BubblegumCableWaterSplashCore.h"
#include "BubblegumCableUndersideShadowCore.h"
#include "BubblegumCableGlossCore.h"
#include "BubblegumCableHighlightCore.h"
#include "BubblegumCableTranslucencyCore.h"
#include "BubblegumCableDepthGradeCore.h"
#include "BubblegumSlimeCoatingCore.h"
#include "BubblegumDripGenerationCore.h"
#include "BubblegumDripAttachmentCore.h"
#include "BubblegumCableAnimationCoreV2.h"
#include "BubblegumCableMotionStabilityCore.h"
#include "BubblegumCableAnimationPolishCore.h"
#include "BubblegumCableFallbackRenderCore.h"
#include "BubblegumCableOpenGL2EngineCore.h"
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace DAW::BgV2 {

class BubblegumCableRenderPipelineCore {
public:
    void tick(float deltaSec, double nowSec) {
        animationLastDt_ = deltaSec;
        animation_.tick(deltaSec);
        nowSec_ = nowSec;
    }

    void setMotionState(int vpX, int vpY, int mxX, int mxY) {
        motion_.update(vpX, vpY, mxX, mxY, nowSec_);
    }

    void paint(juce::Graphics& g,
               const std::vector<CableInput>& cables,
               float laneBottomY) {
        const bool moving = motion_.isMoving(nowSec_);
        const float t = animation_.time();

        for (const auto& in : cables) {
            if (!in.active) continue;

            auto geo = geometry_.build(in, laneBottomY);

            // TIER 0 — always, unconditional, pure geometry
            coating_.paint(g, geo, material_.palette(), in.sendEnergy);
            waterStream_.paintBody(g, geo, in, material_.palette());
            underside_.paint(g, geo, material_.palette());
            gloss_.paint(g, geo, material_.palette(), t);
            paintStaticEndpointDots_(g, geo, material_.palette());

            // Tier 1+ only when settled
            if (moving) continue;

            // TIER 1
            translucency_.paint(g, geo, material_.palette());
            depth_.paint(g, geo, material_.palette());

            // TIER 2
            if (in.dragTension < 0.05f) {
                highlight_.paint(g, geo, material_.palette(), t);
                paintTier2Drips_(g, geo, in, t, false);
                polish_.paint(g, geo, in, material_.palette());
            }

            // TIER 3
            waterSplash_.paint(g, geo, in, material_.palette(), t);
        }
    }

    BubblegumCableMaterialCore&        material()  noexcept { return material_;  }
    BubblegumCableAnimationCoreV2&     animation() noexcept { return animation_; }
    BubblegumCableMotionStabilityCore& motion()    noexcept { return motion_;    }
    BubblegumCableFallbackRenderCore&  fallback()  noexcept { return fallback_;  }

private:
    void paintStaticEndpointDots_(juce::Graphics& g,
                                   const CableGeometry& geo,
                                   const MaterialPalette& pal) const {
        const float r = juce::jmax(6.f, geo.thickness * 0.9f);
        auto drawDot = [&](float cx, float cy) {
            juce::ColourGradient gr(
                pal.specPeak, cx - r * 0.3f, cy - r * 0.4f,
                pal.albedo,   cx + r,        cy + r, false);
            g.setGradientFill(gr);
            g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
            g.setColour(pal.shadowDeep);
            g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.f);
        };
        drawDot(geo.sx, geo.sy);
        drawDot(geo.tx, geo.ty);
    }

    // Stable integer key from source/target positions — avoids float map instability
    static uint32_t cableKey(const CableInput& in) noexcept {
        const uint32_t sx = static_cast<uint32_t>(std::abs(in.source.x) * 100.f) & 0xFFFF;
        const uint32_t tx = static_cast<uint32_t>(std::abs(in.target.x) * 100.f) & 0xFFFF;
        return (sx << 16) | tx;
    }

    void paintTier2Drips_(juce::Graphics& g, const CableGeometry& geo,
                           const CableInput& in, float t, bool moving) {
        auto& dripState = perCableDrips_[cableKey(in)];
        const auto& drips = dripState.tick(geo.seed, animationLastDt_, in.sendEnergy, moving);
        dripPaint_.paint(g, geo, drips, material_.palette());
        juce::ignoreUnused(t);
    }

    BubblegumCableMaterialCore        material_;
    BubblegumCableGeometryCoreV2      geometry_;
    BubblegumCableBodyRenderCore      body_;
    BubblegumCableWaterStreamCore     waterStream_;
    BubblegumCableWaterSplashCore     waterSplash_;
    BubblegumCableUndersideShadowCore underside_;
    BubblegumCableGlossCore           gloss_;
    BubblegumCableHighlightCore       highlight_;
    BubblegumCableTranslucencyCore    translucency_;
    BubblegumCableDepthGradeCore      depth_;
    BubblegumSlimeCoatingCore         coating_;
    BubblegumDripAttachmentCore       dripPaint_;
    BubblegumCableAnimationCoreV2     animation_;
    BubblegumCableMotionStabilityCore motion_;
    BubblegumCableAnimationPolishCore polish_;
    BubblegumCableFallbackRenderCore  fallback_;

    std::unordered_map<uint32_t, BubblegumDripGenerationCore> perCableDrips_;
    double nowSec_ = 0.0;
    float  animationLastDt_ = 1.f / 60.f;
};

} // namespace DAW::BgV2
