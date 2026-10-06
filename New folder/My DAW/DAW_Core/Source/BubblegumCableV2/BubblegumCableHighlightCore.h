#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"
#include "BubblegumCableAnimationCoreV2.h"
#include <cmath>

namespace DAW::BgV2 {

class BubblegumCableHighlightCore {
public:
    void paint(juce::Graphics& g, const CableGeometry& geo,
               const MaterialPalette& pal, float time) const {
        if (geo.ribbon.isEmpty()) return;

        const float phase = BubblegumCableAnimationCoreV2::loopPhase(time, 0.18f, geo.seed);
        const auto s = sampleAlong(geo, phase);
        const float ang = std::atan2(s.tangent.y, s.tangent.x);

        const auto ctr = juce::Point<float>(
            s.pos.x + s.normal.x * s.halfW * 0.55f,
            s.pos.y + s.normal.y * s.halfW * 0.55f);

        const float w = juce::jmax(10.f, s.halfW * 4.0f);
        const float h = juce::jmax(2.5f, s.halfW * 0.65f);

        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);
        juce::Path blob;
        blob.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
        blob.applyTransform(juce::AffineTransform::rotation(ang).translated(ctr.x, ctr.y));
        juce::ColourGradient sg(
            pal.specPeak, ctr.x, ctr.y,
            pal.litTop,   ctr.x + w * 0.5f, ctr.y, true);
        g.setGradientFill(sg);
        g.fillPath(blob);
    }

private:
    struct Sample { juce::Point<float> pos, tangent, normal; float halfW; };
    static Sample sampleAlong(const CableGeometry& geo, float t) noexcept {
        const float ft = juce::jlimit(0.f, 1.f, t) * (float)CableGeometry::kSeg;
        const int i0 = juce::jlimit(0, CableGeometry::kSeg - 1, (int)ft);
        const int i1 = juce::jmin(CableGeometry::kSeg, i0 + 1);
        const float a = ft - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        Sample s;
        s.pos    = { juce::jmap(a, p0.x,  p1.x),  juce::jmap(a, p0.y,  p1.y) };
        s.normal = { juce::jmap(a, p0.nx, p1.nx), juce::jmap(a, p0.ny, p1.ny) };
        s.halfW  = juce::jmap(a, p0.halfW, p1.halfW);
        const float nl = std::sqrt(s.normal.x * s.normal.x + s.normal.y * s.normal.y);
        if (nl > 1e-4f) { s.normal.x /= nl; s.normal.y /= nl; }
        else { s.normal = { 0.f, -1.f }; }
        s.tangent = { -s.normal.y, s.normal.x };
        return s;
    }
};

} // namespace DAW::BgV2
