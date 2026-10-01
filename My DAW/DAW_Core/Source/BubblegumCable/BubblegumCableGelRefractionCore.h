#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

class BubblegumCableGelRefractionCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;

        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float seed = in.source.x * 0.029f + in.target.x * 0.016f + 41.9f;
        const int count  = 2 + (int) std::floor(BubblegumCableAnimationCore::stable01(seed) * 3.f);

        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        for (int i = 0; i < count; ++i)
        {
            const float fs = seed + (float) i * 8.1f;
            const float t  = juce::jlimit(0.10f, 0.90f,
                             juce::jmap(BubblegumCableAnimationCore::stable01(fs + 1.4f), 0.16f, 0.84f));
            const auto s   = sampleAlong(geo, t);

            const float off = (BubblegumCableAnimationCore::stable01(fs + 2.7f) - 0.5f) * s.halfW * 0.65f;
            const auto c = juce::Point<float>(
                s.pos.x + s.normal.x * off,
                s.pos.y + s.normal.y * off);

            const float w = juce::jmax(5.f, s.halfW * (2.2f + BubblegumCableAnimationCore::stable01(fs + 4.2f) * 1.8f));
            const float h = juce::jmax(2.f, s.halfW * (0.75f + BubblegumCableAnimationCore::stable01(fs + 5.6f) * 0.45f));
            const float a = std::atan2(s.tangent.y, s.tangent.x);
            const float pulse = 0.84f + 0.16f * std::sin(time * (0.7f + 0.08f * (float) i) + fs * 0.5f);

            juce::Path lens;
            lens.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
            lens.applyTransform(juce::AffineTransform::rotation(a).translated(c.x, c.y));

            juce::ColourGradient grad(
                kEdge.withAlpha((0.14f + e * 0.06f) * pulse), c.x - std::cos(a) * w * 0.3f, c.y - std::sin(a) * w * 0.3f,
                kCore.withAlpha((0.24f + e * 0.08f) * pulse), c.x + std::cos(a) * w * 0.3f, c.y + std::sin(a) * w * 0.3f,
                true);
            grad.addColour(0.55, kHot.withAlpha((0.22f + e * 0.08f) * pulse));
            g.setGradientFill(grad);
            g.fillPath(lens);
        }
    }

private:
    const juce::Colour kEdge { 0xFFFF9FD3 };
    const juce::Colour kCore { 0xFFFFE7F4 };
    const juce::Colour kHot  { 0xFFFFFFFF };

    struct Sample
    {
        juce::Point<float> pos, normal, tangent;
        float halfW = 0.f;
    };

    static Sample sampleAlong(const BubblegumCableGeometry& geo, float t) noexcept
    {
        const float ft = juce::jlimit(0.f, 1.f, t) * (float) BubblegumCableGeometry::kSeg;
        const int i0   = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int) std::floor(ft));
        const int i1   = juce::jmin(BubblegumCableGeometry::kSeg, i0 + 1);
        const float a  = ft - (float) i0;
        const auto& p0 = geo.points[(size_t) i0];
        const auto& p1 = geo.points[(size_t) i1];

        Sample s;
        s.pos    = { juce::jmap(a, p0.x, p1.x), juce::jmap(a, p0.y, p1.y) };
        s.normal = { juce::jmap(a, p0.nx, p1.nx), juce::jmap(a, p0.ny, p1.ny) };
        s.halfW  = juce::jmap(a, p0.topW, p1.topW);
        const float nl = std::sqrt(s.normal.x * s.normal.x + s.normal.y * s.normal.y);
        if (nl > 1e-4f) { s.normal.x /= nl; s.normal.y /= nl; }
        else            { s.normal = { 0.f, -1.f }; }
        s.tangent = { -s.normal.y, s.normal.x };
        return s;
    }
};

} // namespace DAW
