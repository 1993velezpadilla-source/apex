#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"
#include <cmath>

namespace DAW::BgV2 {

class BubblegumCableGlossCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const MaterialPalette& pal, float time) const {
        if (geo.topContour.isEmpty()) return;
        const float pulse = 0.85f + 0.15f * std::sin(time * 1.8f + geo.seed * 11.f);

        g.setColour(pal.midLit.withAlpha(0.30f * pulse));
        g.strokePath(geo.topContour,
            juce::PathStrokeType(3.6f, juce::PathStrokeType::curved,
                                       juce::PathStrokeType::rounded));
        g.setColour(pal.sheenTop);
        g.strokePath(geo.topContour,
            juce::PathStrokeType(0.9f, juce::PathStrokeType::curved,
                                       juce::PathStrokeType::rounded));
    }
};

} // namespace DAW::BgV2
