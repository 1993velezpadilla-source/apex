#pragma once
#include "BubblegumCableTypesV2.h"

namespace DAW::BgV2 {

class BubblegumCableSagTensionCore {
public:
    static float compute(const CableInput& in, float laneBottomY) noexcept {
        const float dx     = in.target.x - in.source.x;
        const float dy     = in.target.y - in.source.y;
        const float span   = juce::jmax(1.f, std::abs(dx));
        const float energy = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float drag   = juce::jlimit(0.f, 1.f, in.dragTension);

        const float levelK  = 1.f - juce::jlimit(0.f, 1.f,
                                  std::abs(dy) / juce::jmax(40.f, span));

        const float sagBase = juce::jlimit(34.f, 190.f,
                                  span * 0.46f + energy * 18.f);

        const float rawSag  = sagBase * (0.70f + 0.30f * levelK)
                                      * (1.f - drag * 0.28f);

        const float midLineY = 0.5f * (in.source.y + in.target.y);

        if (laneBottomY <= 0.f) return rawSag;
        return juce::jmin(rawSag, juce::jmax(18.f, laneBottomY - midLineY - 4.f));
    }
};

} // namespace DAW::BgV2
