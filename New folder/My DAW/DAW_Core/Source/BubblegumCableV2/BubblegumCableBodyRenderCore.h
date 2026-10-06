#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"
#include "BubblegumCableAnchorResponseCore.h"
#include <cmath>

namespace DAW::BgV2 {

class BubblegumCableBodyRenderCore {
public:
    void paintBody(juce::Graphics& g, const CableGeometry& geo,
                   const CableInput& in, const MaterialPalette& pal) const {
        if (geo.ribbon.isEmpty()) return;

        // 1. Drop shadow
        {
            juce::Path sh = geo.ribbon;
            sh.applyTransform(juce::AffineTransform::translation(0.f, 5.f));
            g.setColour(juce::Colour(0x66000000));
            g.fillPath(sh);
            sh.applyTransform(juce::AffineTransform::translation(0.f, 3.f));
            g.setColour(juce::Colour(0x33000000));
            g.fillPath(sh);
        }

        // 2. Cylindrical gradient fill
        const auto b = ribbonBounds(geo);
        const float cx = b.getCentreX();
        juce::ColourGradient cyl(
            pal.specPeak,    cx, b.getY(),
            pal.shadowMid,   cx, b.getBottom(), false);
        cyl.addColour(0.08, pal.litTop);
        cyl.addColour(0.22, pal.midLit);
        cyl.addColour(0.45, pal.albedo);
        cyl.addColour(0.72, pal.shadowMid);
        g.setGradientFill(cyl);
        g.fillPath(geo.ribbon);

        // 3. Outline
        g.setColour(pal.shadowDeep);
        g.strokePath(geo.ribbon,
            juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                       juce::PathStrokeType::rounded));
        juce::ignoreUnused(in);
    }

    void paintEndpointBalls(juce::Graphics& g, const CableGeometry& geo,
                            const CableInput& in, const MaterialPalette& pal,
                            float time) const {
        paintBall(g, geo.sx, geo.sy, geo.thickness, in.sendEnergy, pal, time);
        paintBall(g, geo.tx, geo.ty, geo.thickness, in.sendEnergy, pal, time);
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

    void paintBall(juce::Graphics& g, float cx, float cy, float thickness,
                   float energy, const MaterialPalette& pal, float time) const {
        const float r = BubblegumCableAnchorResponseCore::ballRadius(thickness);

        g.setColour(juce::Colour(0x55000000));
        g.fillEllipse(cx - r, cy - r * 0.8f + 5.f, r * 2.f, r * 1.6f);

        juce::ColourGradient grad(
            pal.specPeak,    cx, cy - r * 0.55f,
            pal.shadowDeep,  cx, cy + r, false);
        grad.addColour(0.30, pal.litTop);
        grad.addColour(0.52, pal.albedo);
        grad.addColour(0.76, pal.shadowMid);
        g.setGradientFill(grad);
        g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);

        const float pulse = 0.82f + 0.18f * std::sin(time * 2.4f + cx * 0.02f);
        const float gw = r * (0.50f + energy * 0.08f) * pulse;
        const float gh = r * (0.28f + energy * 0.04f) * pulse;
        juce::ColourGradient glint(
            pal.specPeak,  cx - gw * 0.2f, cy - r * 0.58f,
            pal.litTop,    cx + gw * 0.5f, cy - r * 0.25f, false);
        g.setGradientFill(glint);
        g.fillEllipse(cx - gw * 0.5f, cy - r * 0.65f, gw, gh);

        g.setColour(pal.shadowDeep);
        g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.2f);
    }
};

} // namespace DAW::BgV2
