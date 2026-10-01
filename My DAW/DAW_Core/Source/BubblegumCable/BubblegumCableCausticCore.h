#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>

namespace DAW {

class BubblegumCableCausticCore
{
public:
    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput&    in,
               float                         time) const
    {
        if (geo.ribbon.isEmpty()) return;

        const float e    = juce::jlimit(0.f, 1.f, in.sendEnergy);
        const float seed = in.source.x * 0.021f + in.target.x * 0.014f + 17.3f;
        const int count  = 3 + (int) std::floor(BubblegumCableAnimationCore::stable01(seed) * 3.f);

        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(geo.ribbon);

        for (int i = 0; i < count; ++i)
        {
            const float fs = seed + (float) i * 5.7f;
            const float t  = juce::jlimit(0.08f, 0.92f,
                             juce::jmap((float) (i + 1) / (float) (count + 1), 0.12f, 0.88f));
            const auto s   = sampleAlong(geo, t);

            const float len   = juce::jmax(8.f, s.halfW * (4.5f + BubblegumCableAnimationCore::stable01(fs + 1.1f) * 2.5f));
            const float thick = juce::jmax(0.8f, s.halfW * (0.35f + BubblegumCableAnimationCore::stable01(fs + 2.3f) * 0.25f));
            const float shift = (BubblegumCableAnimationCore::stable01(fs + 3.7f) - 0.5f) * s.halfW * 0.55f;

            const auto centre = juce::Point<float>(
                s.pos.x + s.normal.x * shift,
                s.pos.y + s.normal.y * shift);

            const float pulse = 0.82f + 0.18f * std::sin(time * (0.9f + 0.12f * (float) i) + fs);
            const float alpha = (0.16f + e * 0.08f) * pulse;

            juce::Path p;
            p.startNewSubPath(centre.x - s.tangent.x * len * 0.5f,
                              centre.y - s.tangent.y * len * 0.5f);
            p.lineTo         (centre.x + s.tangent.x * len * 0.5f,
                              centre.y + s.tangent.y * len * 0.5f);

            g.setColour(kCaustic.withAlpha(alpha));
            g.strokePath(p, juce::PathStrokeType(thick,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(kHot.withAlpha(alpha * 0.8f));
            g.strokePath(p, juce::PathStrokeType(juce::jmax(0.5f, thick * 0.35f),
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

private:
    const juce::Colour kCaustic { 0xFFFFD9EF };
    const juce::Colour kHot     { 0xFFFFFFFF };

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
