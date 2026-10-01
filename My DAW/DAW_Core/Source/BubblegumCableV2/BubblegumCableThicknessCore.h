#pragma once
#include "BubblegumCableTypesV2.h"

namespace DAW::BgV2 {

class BubblegumCableThicknessCore {
public:
    static float baseHalfWidth(float energy) noexcept {
        return 5.0f + juce::jlimit(0.f, 1.f, energy) * 2.0f;
    }

    static float halfWidthAt(float t, float baseHw) noexcept {
        const float arch   = std::sin(t * juce::MathConstants<float>::pi);
        const float shaped = std::pow(arch, 1.5f);
        return baseHw * (1.0f + shaped * 0.06f);
    }
};

} // namespace DAW::BgV2
