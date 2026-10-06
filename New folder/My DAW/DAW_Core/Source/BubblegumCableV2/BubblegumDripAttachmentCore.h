#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"
#include "BubblegumCableGeometryCoreV2.h"
#include "BubblegumDripGenerationCore.h"
#include <cmath>

namespace DAW::BgV2 {

class BubblegumDripAttachmentCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const BubblegumDripGenerationCore::CableDrips& drips,
               const MaterialPalette& pal) const {
        for (const auto& d : drips.drips) {
            if (!d.active) continue;
            paintOneDrip(g, geo, d, pal);
        }
    }

private:
    void paintOneDrip(juce::Graphics& g, const CableGeometry& geo,
                       const BubblegumDripGenerationCore::Drip& d,
                       const MaterialPalette& pal) const {
        float radius, dropDist, alpha, neckThin;
        BubblegumDripGenerationCore::shapeFromPhase(d.phase, d.maxR,
                                                    radius, dropDist, alpha, neckThin);
        if (alpha <= 0.01f || radius <= 0.1f) return;

        const auto attach = BubblegumCableGeometryCoreV2::undersideAt(geo, d.anchorT);
        const float bulbCx = attach.x;
        const float bulbCy = attach.y + dropDist;

        // Drip shadow
        g.setColour(juce::Colour(0x44000000).withAlpha(0.20f * alpha));
        g.fillEllipse(bulbCx - radius * 0.9f, bulbCy + 1.f,
                       radius * 1.8f, radius * 1.4f);

        // Neck
        {
            juce::Path neck;
            const float neckTopY = bulbCy - radius * 0.85f;
            const float midX     = (attach.x + bulbCx) * 0.5f;
            const float midY     = (attach.y + neckTopY) * 0.5f + 0.4f;
            const float neckW    = juce::jmax(0.6f, geo.thickness * 0.30f * (1.0f - neckThin * 0.55f));

            neck.startNewSubPath(attach.x - neckW, attach.y);
            neck.quadraticTo(midX - neckW * 0.5f, midY,
                              bulbCx - neckW * 0.4f, neckTopY);
            neck.lineTo(bulbCx + neckW * 0.4f, neckTopY);
            neck.quadraticTo(midX + neckW * 0.5f, midY,
                              attach.x + neckW, attach.y);
            neck.closeSubPath();
            juce::ColourGradient ng(
                pal.midLit,   attach.x, attach.y,
                pal.shadowMid, bulbCx, bulbCy, false);
            ng.addColour(0.5, pal.albedo);
            g.setGradientFill(ng);
            g.fillPath(neck);
        }

        // Bulb
        {
            juce::ColourGradient bg(
                pal.specPeak,    bulbCx, bulbCy - radius * 0.55f,
                pal.shadowDeep,  bulbCx, bulbCy + radius, false);
            bg.addColour(0.30, pal.litTop);
            bg.addColour(0.55, pal.albedo);
            bg.addColour(0.80, pal.shadowMid);
            g.setGradientFill(bg);
            g.fillEllipse(bulbCx - radius, bulbCy - radius,
                          radius * 2.f, radius * 2.f);

            g.setColour(pal.specPeak.withAlpha(0.95f * alpha));
            g.fillEllipse(bulbCx - radius * 0.35f, bulbCy - radius * 0.50f,
                          radius * 0.40f, radius * 0.30f);

            g.setColour(pal.shadowDeep.withAlpha(alpha));
            g.drawEllipse(bulbCx - radius, bulbCy - radius,
                          radius * 2.f, radius * 2.f, 0.8f);
        }
    }
};

} // namespace DAW::BgV2
