#pragma once
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableSagTensionCore.h"
#include "BubblegumCableThicknessCore.h"
#include <cmath>

namespace DAW::BgV2 {

class BubblegumCableGeometryCoreV2 {
public:
    CableGeometry build(const CableInput& in, float laneBottomY) const {
        CableGeometry geo;
        geo.sx = in.source.x; geo.sy = in.source.y;
        geo.tx = in.target.x; geo.ty = in.target.y;
        geo.seed = in.id();
        geo.sag  = BubblegumCableSagTensionCore::compute(in, laneBottomY);

        const float baseHw = BubblegumCableThicknessCore::baseHalfWidth(in.sendEnergy);
        geo.thickness = baseHw * 2.f;

        const float dx = geo.tx - geo.sx;
        const float dy = geo.ty - geo.sy;

        constexpr float kHW = 1.90f;
        const float coshMax = std::cosh(kHW);
        const float coshDen = juce::jmax(1e-4f, coshMax - 1.f);

        constexpr int N = CableGeometry::kSeg;

        for (int i = 0; i <= N; ++i) {
            const float t = (float)i / (float)N;
            auto& p = geo.points[(size_t)i];
            p.t = t;
            p.x = geo.sx + dx * t;
            const float lineY = geo.sy + dy * t;
            const float xN    = (2.f * t - 1.f) * kHW;
            const float drop  = (coshMax - std::cosh(xN)) / coshDen;
            p.y = lineY + geo.sag * juce::jlimit(0.f, 1.f, drop);
        }

        for (int i = 0; i <= N; ++i) {
            const int a = juce::jmax(0, i - 1);
            const int b = juce::jmin(N, i + 1);
            const float tdx = geo.points[(size_t)b].x - geo.points[(size_t)a].x;
            const float tdy = geo.points[(size_t)b].y - geo.points[(size_t)a].y;
            const float len = std::sqrt(tdx * tdx + tdy * tdy);
            if (len < 1e-4f) { geo.points[(size_t)i].nx = 0.f; geo.points[(size_t)i].ny = -1.f; }
            else             { geo.points[(size_t)i].nx = -tdy / len; geo.points[(size_t)i].ny = tdx / len; }
            geo.points[(size_t)i].halfW = BubblegumCableThicknessCore::halfWidthAt(geo.points[(size_t)i].t, baseHw);
        }

        const auto& p0 = geo.points[0];
        geo.ribbon.startNewSubPath(p0.x + p0.nx * p0.halfW, p0.y + p0.ny * p0.halfW);
        geo.topContour.startNewSubPath(p0.x + p0.nx * p0.halfW, p0.y + p0.ny * p0.halfW);
        for (int i = 1; i <= N; ++i) {
            const auto& p = geo.points[(size_t)i];
            const float topX = p.x + p.nx * p.halfW;
            const float topY = p.y + p.ny * p.halfW;
            geo.ribbon.lineTo(topX, topY);
            geo.topContour.lineTo(topX, topY);
        }
        const auto& pN = geo.points[(size_t)N];
        geo.bottomContour.startNewSubPath(pN.x - pN.nx * pN.halfW, pN.y - pN.ny * pN.halfW);
        for (int i = N - 1; i >= 0; --i) {
            const auto& p = geo.points[(size_t)i];
            const float bx = p.x - p.nx * p.halfW;
            const float by = p.y - p.ny * p.halfW;
            geo.ribbon.lineTo(bx, by);
            geo.bottomContour.lineTo(bx, by);
        }
        geo.ribbon.closeSubPath();

        return geo;
    }

    static juce::Point<float> undersideAt(const CableGeometry& geo, float t) noexcept {
        const float ft = juce::jlimit(0.f, 1.f, t) * (float)CableGeometry::kSeg;
        const int i0 = juce::jlimit(0, CableGeometry::kSeg - 1, (int)ft);
        const int i1 = juce::jmin(CableGeometry::kSeg, i0 + 1);
        const float a = ft - (float)i0;
        const auto& p0 = geo.points[(size_t)i0];
        const auto& p1 = geo.points[(size_t)i1];
        const float cx = juce::jmap(a, p0.x,  p1.x);
        const float cy = juce::jmap(a, p0.y,  p1.y);
        const float nx = juce::jmap(a, p0.nx, p1.nx);
        const float ny = juce::jmap(a, p0.ny, p1.ny);
        const float hw = juce::jmap(a, p0.halfW, p1.halfW);
        return { cx - nx * hw, cy - ny * hw };
    }
};

} // namespace DAW::BgV2
