#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"

namespace DAW::BgV2 {

class BubblegumCableDepthGradeCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const MaterialPalette& pal) const {
        if (geo.ribbon.isEmpty()) return;
        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        const auto b = geo.ribbon.getBounds();

        juce::ColourGradient h2(
            juce::Colours::transparentBlack, b.getX(), b.getCentreY(),
            juce::Colours::transparentBlack, b.getRight(), b.getCentreY(), false);
        h2.addColour(0.50, pal.shadowMid.withAlpha(0.22f));
        g.setGradientFill(h2);
        g.fillRect(b);
    }
};

} // namespace DAW::BgV2
