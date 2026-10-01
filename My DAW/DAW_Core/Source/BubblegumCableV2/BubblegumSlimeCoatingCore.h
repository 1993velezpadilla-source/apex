#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"

namespace DAW::BgV2 {

class BubblegumSlimeCoatingCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const MaterialPalette& pal, float energy) const {
        if (geo.ribbon.isEmpty()) return;
        const float e = juce::jlimit(0.f, 1.f, energy);

        g.setColour(pal.auraOuter.withAlpha(0.22f + e * 0.18f));
        g.strokePath(geo.ribbon,
            juce::PathStrokeType(8.0f + e * 3.0f,
                                 juce::PathStrokeType::curved,
                                 juce::PathStrokeType::rounded));
    }
};

} // namespace DAW::BgV2
