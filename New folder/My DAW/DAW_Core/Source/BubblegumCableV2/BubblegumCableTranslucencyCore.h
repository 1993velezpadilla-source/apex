#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"

namespace DAW::BgV2 {

class BubblegumCableTranslucencyCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const MaterialPalette& pal) const {
        constexpr int N = CableGeometry::kSeg;
        for (int i = 0; i < N; ++i) {
            const auto& p0 = geo.points[(size_t)i];
            const auto& p1 = geo.points[(size_t)(i + 1)];
            const float fres = BubblegumCableMaterialCore::fresnelTerm(
                                   0.5f * (p0.nx + p1.nx));
            if (fres < 0.10f) continue;
            const float alpha = juce::jlimit(0.f, 0.45f, fres * 0.55f);
            const float t0x = p0.x + p0.nx * p0.halfW;
            const float t0y = p0.y + p0.ny * p0.halfW;
            const float t1x = p1.x + p1.nx * p1.halfW;
            const float t1y = p1.y + p1.ny * p1.halfW;
            g.setColour(pal.sssWarm.withAlpha(alpha));
            g.drawLine(t0x, t0y, t1x, t1y, 1.6f);
        }
    }
};

} // namespace DAW::BgV2
