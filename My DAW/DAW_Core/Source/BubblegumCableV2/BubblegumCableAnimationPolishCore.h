#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"

namespace DAW::BgV2 {

class BubblegumCableAnimationPolishCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const CableInput& in, const MaterialPalette& pal) const {
        if (in.interactionGlow > 0.01f && !geo.ribbon.isEmpty()) {
            g.setColour(pal.sheenTop.withAlpha(0.25f * in.interactionGlow));
            g.strokePath(geo.ribbon,
                juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
        }
        if (in.activationPulse > 0.01f && !geo.ribbon.isEmpty()) {
            g.setColour(pal.specPeak.withAlpha(0.40f * in.activationPulse));
            g.strokePath(geo.ribbon,
                juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
        }
    }
};

} // namespace DAW::BgV2
