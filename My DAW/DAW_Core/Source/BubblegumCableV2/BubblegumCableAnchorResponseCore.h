#pragma once
#include "BubblegumCableTypesV2.h"

namespace DAW::BgV2 {

class BubblegumCableAnchorResponseCore {
public:
    static Endpoint clampToViewport(Endpoint e, float vpLeftX, float vpRightX, float inset = 24.f) noexcept {
        const float minX = vpLeftX  + inset;
        const float maxX = vpRightX - inset;
        e.x = juce::jlimit(minX, maxX, e.x);
        return e;
    }

    static float ballRadius(float thickness) noexcept {
        return juce::jmax(7.f, thickness * 0.78f);
    }
};

} // namespace DAW::BgV2
