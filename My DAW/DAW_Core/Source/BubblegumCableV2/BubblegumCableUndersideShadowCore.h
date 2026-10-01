#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"

namespace DAW::BgV2 {

class BubblegumCableUndersideShadowCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const MaterialPalette& pal) const {
        if (geo.bottomContour.isEmpty()) return;
        g.setColour(pal.shadowMid);
        g.strokePath(geo.bottomContour,
            juce::PathStrokeType(2.6f, juce::PathStrokeType::curved,
                                       juce::PathStrokeType::rounded));
        g.setColour(pal.shadowDeep);
        g.strokePath(geo.bottomContour,
            juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                       juce::PathStrokeType::rounded));
    }
};

} // namespace DAW::BgV2
