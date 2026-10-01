#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"

namespace DAW::BgV2 {

// Cable body rendered as a translucent water/gel stream:
// soft outer spray + semi-transparent main body + bright liquid core + white caustic filament.
class BubblegumCableWaterStreamCore {
public:
    void paintBody(juce::Graphics& g, const CableGeometry& geo,
                   const CableInput& in, const MaterialPalette& pal) const {
        if (geo.ribbon.isEmpty()) return;
        const float e = juce::jlimit(0.f, 1.f, in.sendEnergy);

        // Pass 1 — Drop shadow (grounded)
        {
            juce::Path sh = geo.ribbon;
            sh.applyTransform(juce::AffineTransform::translation(0.f, 4.f));
            g.setColour(juce::Colour(0x44000000));
            g.fillPath(sh);
        }

        // Pass 2 — Wide soft outer spray (the mist halo around a stream)
        g.setColour(pal.albedo.withAlpha(0.22f + e * 0.10f));
        g.strokePath(geo.ribbon,
            juce::PathStrokeType(geo.thickness * 3.5f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));

        // Pass 3 — Main stream body (semi-transparent cylindrical gel)
        {
            const auto b = ribbonBounds(geo);
            const float cx = b.getCentreX();
            juce::ColourGradient mid(
                pal.specPeak.withAlpha(0.95f),  cx, b.getY(),
                pal.shadowMid.withAlpha(0.75f), cx, b.getBottom(), false);
            mid.addColour(0.15, pal.litTop.withAlpha(0.95f));
            mid.addColour(0.35, pal.albedo.withAlpha(0.95f));
            mid.addColour(0.65, pal.shadowMid.withAlpha(0.85f));
            g.setGradientFill(mid);
            g.fillPath(geo.ribbon);
        }

        // Pass 4 — Bright liquid core (light catching the inside of the stream)
        g.setColour(pal.specPeak.withAlpha(0.90f));
        g.strokePath(geo.topContour,
            juce::PathStrokeType(geo.thickness * 0.28f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));

        // Pass 5 — White caustic filament (the fiber of pure light inside water)
        g.setColour(juce::Colours::white.withAlpha(0.65f + e * 0.20f));
        g.strokePath(geo.topContour,
            juce::PathStrokeType(geo.thickness * 0.10f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));

        juce::ignoreUnused(in);
    }

private:
    static juce::Rectangle<float> ribbonBounds(const CableGeometry& geo) noexcept {
        float mnX = geo.points[0].x, mxX = mnX;
        float mnY = geo.points[0].y, mxY = mnY;
        for (const auto& p : geo.points) {
            mnX = juce::jmin(mnX, p.x - p.halfW);
            mxX = juce::jmax(mxX, p.x + p.halfW);
            mnY = juce::jmin(mnY, p.y - p.halfW);
            mxY = juce::jmax(mxY, p.y + p.halfW);
        }
        return { mnX, mnY,
                 juce::jmax(1.f, mxX - mnX),
                 juce::jmax(1.f, mxY - mnY) };
    }
};

} // namespace DAW::BgV2
