#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableAnimationCoreV2.h"
#include <array>
#include <cmath>

namespace DAW::BgV2 {

class BubblegumDripGenerationCore {
public:
    static constexpr int kMaxDripsPerCable = 3;

    struct Drip {
        float anchorT  = 0.5f;
        float phase    = 0.f;
        float freq     = 0.30f;
        float maxR     = 2.5f;
        bool  active   = false;
    };

    struct CableDrips {
        std::array<Drip, kMaxDripsPerCable> drips;
    };

    const CableDrips& tick(float cableSeed, float dt, float energy, bool isMoving) {
        if (isMoving) return drips_;

        const float e = juce::jlimit(0.f, 1.f, energy);

        for (int i = 0; i < kMaxDripsPerCable; ++i) {
            auto& d = drips_.drips[(size_t)i];
            if (!d.active) {
                d.active = true;
                d.anchorT = 0.30f + BubblegumCableAnimationCoreV2::rand01(cableSeed + (float)i * 7.1f) * 0.40f;
                d.freq    = 0.22f + BubblegumCableAnimationCoreV2::rand01(cableSeed + (float)i * 13.7f) * 0.28f;
                d.maxR    = 1.6f + e * 0.8f;
                d.phase   = BubblegumCableAnimationCoreV2::rand01(cableSeed + (float)i * 19.3f);
            }
            d.phase += d.freq * dt;
            if (d.phase > 1.0f) d.phase -= std::floor(d.phase);
            d.maxR = 1.6f + e * 0.8f;
        }

        return drips_;
    }

    static void shapeFromPhase(float phase, float maxR,
                               float& outRadius, float& outDropDist,
                               float& outAlpha, float& outNeckThin) noexcept {
        const float p = juce::jlimit(0.f, 1.f, phase);
        if (p < 0.55f) {
            const float k = BubblegumCableAnimationCoreV2::smoothstep(p / 0.55f);
            outRadius   = maxR * k;
            outDropDist = maxR * 0.6f * k;
            outAlpha    = k;
            outNeckThin = 0.0f;
        } else if (p < 0.82f) {
            const float k = BubblegumCableAnimationCoreV2::smoothstep((p - 0.55f) / 0.27f);
            outRadius   = maxR * (1.0f - 0.15f * k);
            outDropDist = maxR * 0.6f + maxR * 1.4f * k;
            outAlpha    = 1.0f;
            outNeckThin = k;
        } else {
            const float k = BubblegumCableAnimationCoreV2::smoothstep((p - 0.82f) / 0.18f);
            outRadius   = maxR * 0.85f * (1.0f - k);
            outDropDist = maxR * 2.0f * (1.0f - k);
            outAlpha    = 1.0f - k;
            outNeckThin = 1.0f - k;
        }
    }

private:
    CableDrips drips_;
};

} // namespace DAW::BgV2
