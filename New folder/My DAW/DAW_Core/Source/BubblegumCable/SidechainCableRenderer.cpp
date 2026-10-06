#include "SidechainCableRenderer.h"

namespace bg::sidechain
{

// =====================================================================================
// Path construction
// =====================================================================================
juce::Path SidechainCableRenderer::buildDefaultPath (juce::Point<float> source,
                                                     juce::Point<float> dest)
{
    juce::Path p;
    const float dx = dest.x - source.x;
    const float dy = dest.y - source.y;
    const float distance = std::sqrt (dx * dx + dy * dy);

    // Sag scales with distance, capped so long cables don't droop into the abyss.
    const float sag = juce::jmin (distance * 0.075f, 60.0f);

    const juce::Point<float> mid { (source.x + dest.x) * 0.5f,
                                   (source.y + dest.y) * 0.5f + sag };

    p.startNewSubPath (source);
    p.quadraticTo (mid, dest);
    return p;
}

// =====================================================================================
// Path sampling — flatten the Bezier into a polyline so we can place links by
// arc-length and read the local tangent at any point.
// =====================================================================================
namespace
{
    struct PathSample
    {
        juce::Point<float> point;
        float angleRad;     // tangent angle at this sample
        float arcLength;    // cumulative distance from start
    };

    static std::vector<PathSample> flattenPath (const juce::Path& path)
    {
        std::vector<PathSample> samples;
        samples.reserve (256);

        juce::PathFlatteningIterator iter (path, juce::AffineTransform(), 0.5f);
        float cumulative = 0.0f;
        bool firstSeg = true;

        while (iter.next())
        {
            const juce::Point<float> a (iter.x1, iter.y1);
            const juce::Point<float> b (iter.x2, iter.y2);

            if (firstSeg)
            {
                samples.push_back ({ a, std::atan2 (b.y - a.y, b.x - a.x), 0.0f });
                firstSeg = false;
            }

            const float segLen = a.getDistanceFrom (b);
            cumulative += segLen;
            samples.push_back ({ b, std::atan2 (b.y - a.y, b.x - a.x), cumulative });
        }
        return samples;
    }

    struct PathHit { juce::Point<float> point; float angleRad; };

    static PathHit sampleAtArcLength (const std::vector<PathSample>& samples,
                                      float arcLen)
    {
        if (samples.empty())            return { {}, 0.0f };
        if (arcLen <= 0.0f)             return { samples.front().point, samples.front().angleRad };
        if (arcLen >= samples.back().arcLength)
                                        return { samples.back().point,  samples.back().angleRad };

        // Binary search for the segment containing arcLen.
        size_t lo = 0, hi = samples.size() - 1;
        while (hi - lo > 1)
        {
            const size_t mid = (lo + hi) / 2;
            (samples[mid].arcLength < arcLen ? lo : hi) = mid;
        }

        const auto& s0 = samples[lo];
        const auto& s1 = samples[hi];
        const float span = juce::jmax (1e-6f, s1.arcLength - s0.arcLength);
        const float t = (arcLen - s0.arcLength) / span;
        return { s0.point + (s1.point - s0.point) * t, s1.angleRad };
    }
}

// =====================================================================================
// Renderer
// =====================================================================================
void SidechainCableRenderer::draw (juce::Graphics& g,
                                   const juce::Path& cablePath,
                                   const SidechainCableAnimator& anim,
                                   const Style& style)
{
    const auto samples = flattenPath (cablePath);
    if (samples.size() < 2) return;

    const float totalLength = samples.back().arcLength;
    if (totalLength < 1.0f) return;

    const float linkSpacing = style.getLinkSpacing();
    const float ringRx      = style.getRingRx();
    const float faceRy      = style.getFaceRy();
    const float edgeRy      = style.getEdgeRy();
    const float strokeBase  = style.getStrokeWidth();

    const int nVisible = (int) (totalLength / linkSpacing) + 1;
    const int nTotal   = nVisible + 4; // extra links for off-end fade-in/fade-out

    const float flowOffset = anim.getFlowOffsetPx (linkSpacing, nTotal, style.flowSpeed);
    const float pulsePos   = anim.getPulsePosition (style.pulseDurationSec);

    const float fade = linkSpacing * 2.0f;

    for (int i = 0; i < nTotal; ++i)
    {
        // Position of this link along the cable, with continuous flow offset.
        float pos = std::fmod (i * linkSpacing + flowOffset,
                               nTotal * linkSpacing);
        if (pos > totalLength) continue;

        // Fade in/out at the endpoints so links appearing/disappearing don't pop.
        float alpha = 1.0f;
        if (pos < fade)                       alpha = pos / fade;
        else if (pos > totalLength - fade)    alpha = (totalLength - pos) / fade;
        if (alpha <= 0.01f) continue;

        const auto hit = sampleAtArcLength (samples, pos);

        // Alternate face-on vs. edge-on so the chain reads as interlocked.
        const bool  isFace = (i % 2) == 0;
        const float ry     = isFace ? faceRy : edgeRy;

        // Pulse glow — distance from pulse position determines brightness.
        float glow = 0.0f;
        if (pulsePos >= 0.0f)
        {
            const float t    = pos / totalLength;
            const float dist = std::abs (t - pulsePos);
            if (dist < style.pulseGlowRadius)
                glow = 1.0f - (dist / style.pulseGlowRadius);
        }

        juce::Colour stroke;
        float strokeW;
        if (glow > 0.5f)
        {
            stroke  = style.blueBright.withAlpha (alpha);
            strokeW = strokeBase + glow * 1.8f;
        }
        else
        {
            stroke  = style.blueBase.withAlpha (alpha);
            strokeW = strokeBase + glow * 0.8f;
        }

        // Build the link as a rotated ellipse so it follows the cable's tangent.
        juce::Path link;
        link.addEllipse (-ringRx, -ry, ringRx * 2.0f, ry * 2.0f);
        link.applyTransform (juce::AffineTransform::rotation (hit.angleRad)
                                                  .translated (hit.point.x, hit.point.y));

        g.setColour (stroke);
        g.strokePath (link,
                      juce::PathStrokeType (strokeW,
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    }
}

// =====================================================================================
// Trigger detector
// =====================================================================================
bool SidechainTriggerDetector::process (uint64_t sendId,
                                        float levelLinear,
                                        double nowSeconds,
                                        const Config& cfg)
{
    auto& s = states[sendId];

    const float currentDb = juce::Decibels::gainToDecibels (
                                juce::jmax (levelLinear, 1.0e-6f));

    const bool aboveThreshold = currentDb > cfg.thresholdDb;
    const bool rising         = (currentDb - s.lastDb) > cfg.minRiseDbPerCall;
    const bool cooledDown     = (nowSeconds - s.lastFireSec) > cfg.minIntervalSec;

    s.lastDb = currentDb;

    if (aboveThreshold && rising && cooledDown)
    {
        s.lastFireSec = nowSeconds;
        return true;
    }
    return false;
}

void SidechainTriggerDetector::reset (uint64_t sendId)
{
    states.erase (sendId);
}

} // namespace bg::sidechain
