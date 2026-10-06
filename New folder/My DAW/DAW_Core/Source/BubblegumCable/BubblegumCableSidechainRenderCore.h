#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "BubblegumCableTypes.h"
#include "../Bubblegum/BubblegumCableStyleSettingsCore.h"

namespace DAW {

/**
 * BubblegumCableSidechainRenderCore -- chain-ellipse sidechain cable renderer.
 *
 * Visual contract:
 *   - Premium smoked-metal chain with cool-steel base and champagne-white pulse highlight.
 *   - Active cable: continuous link flow via arc-length offset (~28 px/s).
 *   - Trigger pulse: bright wave travels source -> dest over 500 ms.
 *   - Inactive cable: frozen chain, reduced alpha.
 *   - Respects reducedMotion flag: falls back to static chain.
 *   - Skips tick() advancement when offscreen.
 *   - Small "SC" badge near target endpoint.
 *
 * Geometry (arc-length based):
 *   Path is flattened to a polyline; each link is placed by arc-length so
 *   spacing is physically uniform. Binary search gives point + tangent angle,
 *   making the chain follow sharp bends correctly. nVisible + 4 extra links
 *   are drawn so flow is seamless with endpoint fade-in/out.
 *
 * Animation update:
 *   Call tick() once per frame with deltaMs.
 *   Feed signal levels via feedTriggerLevel() -- pulses fire when level >= -40 dBFS.
 *
 * paint-pure: tick() mutates state; paint() reads it read-only.
 */
class BubblegumCableSidechainRenderCore
{
public:
    static constexpr float  kSpacingPx         = 12.f;
    static constexpr float  kFaceRy            =  3.0f;
    static constexpr float  kEdgeRy            =  1.0f;
    static constexpr float  kRx                =  6.2f;
    static constexpr float  kStrokeWidth       =  0.9f;
    static constexpr float  kFlowSpeedPxPerSec = 28.f;
    static constexpr float  kPulseDurationMs   = 500.f;
    static constexpr float  kPulseGlowRadius   = 0.10f;
    static constexpr float  kTriggerThreshold  = 0.01f;
    static constexpr float  kInactiveAlpha     = 0.35f;
    static constexpr float  kActiveAlpha       = 0.85f;

    // User-changeable cable colours — default to a premium steel/champagne palette.
    juce::Colour colourBase   { 0xFF17191Fu };
    juce::Colour colourBright { 0xFFE7C98Du };
    float thicknessMultiplier = 1.0f;  // User-adjustable thickness multiplier (1.0 = default, range 0.5-2.5)
    bubblegum::BubblegumCableStyleSettingsCore::Style::QualityMode qualityMode =
        bubblegum::BubblegumCableStyleSettingsCore::Style::QualityMode::Performance;

    struct CableState
    {
        juce::String edgeId;
        float flowOffsetPx  = 0.f;
        float pulseProgress = -1.f;
        float triggerLevel  = 0.f;
    };

    bool reducedMotion = false;

    /** Returns true if any sidechain edge is animating (flow advancing or
     *  pulse in flight). Used by the overlay repaint gate to decide whether
     *  compositeFrameDirty_ should be set this tick.
     *  Note: flowOffsetPx is always > 0 after the first tick, so active
     *  sidechain cables always return true here — which is correct since
     *  the chain flow IS a continuous animation. */
    bool hasActiveEdges() const noexcept
    {
        if (reducedMotion || states_.empty()) return false;
        for (const auto& s : states_)
            if (s.pulseProgress >= 0.f || s.flowOffsetPx > 0.f)
                return true;
        return false;
    }

    void tick(float deltaMs) noexcept
    {
        if (reducedMotion) return;
        const float dt = deltaMs * 0.001f;

        for (auto& s : states_)
        {
            s.flowOffsetPx += kFlowSpeedPxPerSec * dt;

            if (s.pulseProgress >= 0.f)
            {
                s.pulseProgress += dt / (kPulseDurationMs * 0.001f);
                if (s.pulseProgress > 1.f)
                    s.pulseProgress = -1.f;
            }

            if (s.triggerLevel >= kTriggerThreshold && s.pulseProgress < 0.f)
                s.pulseProgress = 0.f;
        }
    }

    void feedTriggerLevel(const juce::String& edgeId, float linearLevel) noexcept
    {
        findOrCreate(edgeId)->triggerLevel = linearLevel;
    }

    void setOffscreen(const juce::String& edgeId, bool offscreen) noexcept
    {
        juce::ignoreUnused(edgeId, offscreen);
    }

    void removeEdge(const juce::String& edgeId) noexcept
    {
        for (int i = 0; i < (int)states_.size(); ++i)
        {
            if (states_[(size_t)i].edgeId == edgeId)
            {
                states_.erase(states_.begin() + i);
                return;
            }
        }
    }

    // Erase states for any edge IDs not present in liveIds.
    // Call this whenever the routing topology changes.
    void pruneStates(const juce::StringArray& liveIds) noexcept
    {
        for (int i = (int)states_.size() - 1; i >= 0; --i)
            if (!liveIds.contains(states_[(size_t)i].edgeId))
                states_.erase(states_.begin() + i);
    }

    void paint(juce::Graphics& g,
               const BubblegumCableInput& cable,
               bool isActive,
               float laneBottomY) const
    {
        if (!cable.shouldDraw()) return;

        const float sx = cable.source.x, sy = cable.source.y;
        const float tx = cable.target.x, ty = cable.target.y;

        juce::Path path;
        path.startNewSubPath(sx, sy);
        path.quadraticTo((sx + tx) * 0.5f, laneBottomY, tx, ty);

        const float flattenTolerance = qualityMode == bubblegum::BubblegumCableStyleSettingsCore::Style::QualityMode::Minimal ? 1.2f
                                    : qualityMode == bubblegum::BubblegumCableStyleSettingsCore::Style::QualityMode::Cinematic ? 0.35f
                                                                                                                               : 0.5f;
        const auto samples = flattenPath(path, flattenTolerance);
        if (samples.size() < 2) return;

        const float totalLength = samples.back().arcLength;
        if (totalLength < 1.f) return;

        const auto* state = findConst(cable.edgeId);
        const float baseAlpha = cable.alphaMult * (isActive ? kActiveAlpha : kInactiveAlpha);

        // ── Dynamic link spacing — scales with thickness but uses taper to prevent
        // bloat at large thickness or cramping at small thickness ──────────────────
        const float baseSpacing = qualityMode == bubblegum::BubblegumCableStyleSettingsCore::Style::QualityMode::Minimal ? (kSpacingPx + 4.f)
                               : qualityMode == bubblegum::BubblegumCableStyleSettingsCore::Style::QualityMode::Cinematic ? (kSpacingPx - 1.f)
                                                                                                                             : kSpacingPx;
        // Apply thickness with a logarithmic taper: larger thickness gets slightly reduced spacing ratio.
        // Formula keeps chain coherent from 0.3px (tiny) to 4.0px (huge).
        const float spacingScale = 0.7f + 0.3f / (thicknessMultiplier * 0.5f + 0.3f);
        const float linkSpacing = baseSpacing * thicknessMultiplier * spacingScale;

        const int nVisible = (int)(totalLength / linkSpacing) + 1;
        const int nTotal   = nVisible + 4;

        const float flowOffset = (!reducedMotion && isActive && state)
            ? std::fmod(state->flowOffsetPx, linkSpacing * (float)nTotal)
            : 0.f;

        const float pulsePos = (!reducedMotion && state && state->pulseProgress >= 0.f)
            ? state->pulseProgress : -1.f;

        const float fadeZone = linkSpacing * 2.f;

        for (int i = 0; i < nTotal; ++i)
        {
            float pos = std::fmod((float)i * linkSpacing + flowOffset,
                                  (float)nTotal * linkSpacing);
            if (pos > totalLength) continue;

            float alpha = baseAlpha;
            if (pos < fadeZone)
                alpha *= pos / fadeZone;
            else if (pos > totalLength - fadeZone)
                alpha *= (totalLength - pos) / fadeZone;
            if (alpha <= 0.01f) continue;

            const auto hit = sampleAtArcLength(samples, pos);

            // ── Dynamic link dimensions — scale with thickness but apply subtle
            // taper at extremes to preserve chain character ────────────────────────
            const float faceTaper = 0.85f + 0.15f / (thicknessMultiplier * 0.4f + 0.5f);
            const float edgeTaper = 0.90f + 0.10f / (thicknessMultiplier * 0.4f + 0.5f);
            const float rxTaper   = 0.80f + 0.20f / (thicknessMultiplier * 0.5f + 0.3f);

            const float ry = ((i % 2 == 0) ? kFaceRy * faceTaper : kEdgeRy * edgeTaper) * thicknessMultiplier;
            const float rx = kRx * rxTaper * thicknessMultiplier;

            float glow = 0.f;
            if (pulsePos >= 0.f)
            {
                const float t    = pos / totalLength;
                const float dist = std::abs(t - pulsePos);
                if (dist < kPulseGlowRadius)
                    glow = 1.f - dist / kPulseGlowRadius;
            }

            juce::Colour outerStroke = isActive
                ? colourBase.darker(0.25f).withAlpha(alpha * 0.92f)
                : juce::Colour(0xFF111318u).withAlpha(alpha * 0.76f);
            juce::Colour midStroke = isActive
                ? colourBase.interpolatedWith(juce::Colour(0xFF434A58u), 0.36f).withAlpha(alpha * 0.94f)
                : juce::Colour(0xFF4E545Eu).withAlpha(alpha * 0.76f);
            juce::Colour innerStroke = glow > 0.35f
                ? colourBright.brighter(0.08f).withAlpha(alpha * 0.92f)
                : colourBright.withAlpha(alpha * (isActive ? 0.28f : 0.18f));
            juce::Colour topGlint = colourBright.interpolatedWith(juce::Colours::white, 0.42f)
                .withAlpha(alpha * (0.10f + glow * 0.20f));

            const float baseStrokeW = (kStrokeWidth + glow * 0.9f) * thicknessMultiplier;
            const float outerStrokeW = baseStrokeW + 1.7f * thicknessMultiplier;
            const float glowStrokeW  = baseStrokeW + 3.2f * thicknessMultiplier * (0.45f + glow * 0.55f);

            juce::Path link;
            link.addEllipse(-rx, -ry, rx * 2.f, ry * 2.f);
            link.applyTransform(
                juce::AffineTransform::rotation(hit.angleRad)
                                    .translated(hit.point.x, hit.point.y));

            juce::Path linkShadow = link;
            linkShadow.applyTransform(juce::AffineTransform::translation(0.0f, 1.1f * thicknessMultiplier));

            if (glow > 0.01f)
            {
                g.setColour(colourBright.withAlpha(alpha * 0.10f * (0.45f + glow * 0.55f)));
                g.strokePath(link, juce::PathStrokeType(glowStrokeW,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }

            g.setColour(juce::Colours::black.withAlpha(alpha * (isActive ? 0.16f : 0.10f)));
            g.strokePath(linkShadow, juce::PathStrokeType(outerStrokeW + 0.7f * thicknessMultiplier,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));

            g.setColour(outerStroke);
            g.strokePath(link, juce::PathStrokeType(outerStrokeW,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

            g.setColour(midStroke);
            g.strokePath(link, juce::PathStrokeType(baseStrokeW,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

            g.setColour(colourBright.withAlpha(alpha * 0.06f));
            g.strokePath(link, juce::PathStrokeType(juce::jmax(0.8f, baseStrokeW * 0.62f),
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

            g.setColour(innerStroke);
            g.strokePath(link, juce::PathStrokeType(juce::jmax(0.9f, baseStrokeW * 0.38f),
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

            juce::Path glint;
            glint.addEllipse(-rx * 0.52f, -ry * 0.88f, rx * 1.04f, juce::jmax(0.9f, ry * 0.72f));
            glint.applyTransform(
                juce::AffineTransform::rotation(hit.angleRad)
                                    .translated(hit.point.x, hit.point.y));
            g.setColour(topGlint);
            g.strokePath(glint, juce::PathStrokeType(juce::jmax(0.7f, baseStrokeW * 0.22f),
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
        // SC badge anchor removed — hidden under the mixer scrollbar.
    }

private:
    struct PathSample
    {
        juce::Point<float> point;
        float angleRad;
        float arcLength;
    };

    struct PathHit
    {
        juce::Point<float> point;
        float angleRad;
    };

    static std::vector<PathSample> flattenPath(const juce::Path& path, float tolerance)
    {
        std::vector<PathSample> samples;
        samples.reserve(256);

        juce::PathFlatteningIterator iter(path, juce::AffineTransform(), tolerance);
        float cumulative = 0.f;
        bool first = true;

        while (iter.next())
        {
            const juce::Point<float> a(iter.x1, iter.y1);
            const juce::Point<float> b(iter.x2, iter.y2);

            if (first)
            {
                samples.push_back({ a, std::atan2(b.y - a.y, b.x - a.x), 0.f });
                first = false;
            }

            cumulative += a.getDistanceFrom(b);
            samples.push_back({ b, std::atan2(b.y - a.y, b.x - a.x), cumulative });
        }
        return samples;
    }

    static PathHit sampleAtArcLength(const std::vector<PathSample>& samples, float arcLen)
    {
        if (samples.empty())                    return { {}, 0.f };
        if (arcLen <= 0.f)                      return { samples.front().point, samples.front().angleRad };
        if (arcLen >= samples.back().arcLength) return { samples.back().point,  samples.back().angleRad };

        size_t lo = 0, hi = samples.size() - 1;
        while (hi - lo > 1)
        {
            const size_t mid = (lo + hi) / 2;
            (samples[mid].arcLength < arcLen ? lo : hi) = mid;
        }

        const auto& s0 = samples[lo];
        const auto& s1 = samples[hi];
        const float span = juce::jmax(1e-6f, s1.arcLength - s0.arcLength);
        const float t    = (arcLen - s0.arcLength) / span;
        return { s0.point + (s1.point - s0.point) * t, s1.angleRad };
    }

    CableState* findOrCreate(const juce::String& edgeId) noexcept
    {
        for (auto& s : states_)
            if (s.edgeId == edgeId) return &s;
        states_.push_back({ edgeId });
        return &states_.back();
    }

    const CableState* findConst(const juce::String& edgeId) const noexcept
    {
        for (const auto& s : states_)
            if (s.edgeId == edgeId) return &s;
        return nullptr;
    }

    mutable std::vector<CableState> states_;
};

} // namespace DAW
