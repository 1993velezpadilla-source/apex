#include "BubblegumCableGeometryCore.h"
#include <cmath>

namespace bubblegum
{
    //=========================================================================
    // Curve evaluation
    //=========================================================================

    juce::Point<float> BubblegumCableGeometryCore::pointOnCurve(
        const CableResolvedFrame& f, float t) const noexcept
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        const float u  = 1.0f - t;
        const float b0 = u * u * u;
        const float b1 = 3.0f * u * u * t;
        const float b2 = 3.0f * u * t * t;
        const float b3 = t * t * t;
        return {
            b0 * f.source.x + b1 * f.controlA.x + b2 * f.controlB.x + b3 * f.destination.x,
            b0 * f.source.y + b1 * f.controlA.y + b2 * f.controlB.y + b3 * f.destination.y
        };
    }

    juce::Point<float> BubblegumCableGeometryCore::tangentOnCurve(
        const CableResolvedFrame& f, float t) const noexcept
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        const float u = 1.0f - t;
        auto v = (f.controlA    - f.source)      * (3.0f * u * u)
               + (f.controlB    - f.controlA)    * (6.0f * u * t)
               + (f.destination - f.controlB)    * (3.0f * t * t);
        const float len = std::sqrt(v.x * v.x + v.y * v.y);
        return (len > 0.0001f) ? v / len : juce::Point<float>{ 1.0f, 0.0f };
    }

    //=========================================================================
    // Short cable scale — suppresses chaos on small connections
    //=========================================================================

    float BubblegumCableGeometryCore::cableLengthScale(
        const CableResolvedFrame& frame,
        const BubblegumCableStyleSettingsCore::Style& style) const noexcept
    {
        const float len = frame.source.getDistanceFrom(frame.destination);
        return juce::jmap(
            juce::jlimit(style.shortCableStart, style.shortCableEnd, len),
            style.shortCableStart,
            style.shortCableEnd,
            0.45f,
            1.0f);
    }

    //=========================================================================
    // Sampling
    //=========================================================================

    std::vector<CableStrokeSample> BubblegumCableGeometryCore::sampleLiquidStroke(
        const CableResolvedFrame& frame,
        int steps,
        const BubblegumCableStyleSettingsCore::Style& style) const
    {
        std::vector<CableStrokeSample> out;
        out.reserve((size_t)steps + 1);

        const float lengthScale = cableLengthScale(frame, style);

        for (int i = 0; i <= steps; ++i)
        {
            const float t   = (float)i / (float)steps;
            const auto  p   = pointOnCurve(frame, t);
            const auto  tan = tangentOnCurve(frame, t);
            const juce::Point<float> n(-tan.y, tan.x);

            CableStrokeSample s;
            s.point           = p;
            s.tangent         = tan;
            s.normal          = n;
            s.t               = t;
            s.widthMul        = computeWidthMul(frame, t, style, lengthScale);
            s.edgeNoise       = computeEdgeNoise(frame, t, style, lengthScale);
            s.lowerSplashBias = computeLowerSplashBias(t, lengthScale);
            s.splashGate      = splashPresence(frame, t, lengthScale);
            out.push_back(s);
        }

        return out;
    }

    //=========================================================================
    // Body path — UPGRADED: visible bulges + pinches, heavy bottom, calm top
    //=========================================================================

    juce::Path BubblegumCableGeometryCore::buildLiquidBodyPath(
        const CableResolvedFrame& frame,
        const std::vector<CableStrokeSample>& samples,
        const BubblegumCableStyleSettingsCore::Style& style) const
    {
        juce::Path p;
        if (samples.size() < 2) return p;

        std::vector<juce::Point<float>> top, bot;
        top.reserve(samples.size());
        bot.reserve(samples.size());

        for (const auto& s : samples)
        {
            const float base = frame.thickness * 0.5f * s.widthMul;

            // Top edge: tight, smooth, minimal warp — held by surface tension
            const float topWarp =
                s.edgeNoise * frame.thickness * 0.10f * style.topBreakupBias;

            const float topTension =
                base * (0.82f + s.edgeNoise * 0.03f * style.topBreakupBias);

            // Bottom edge: heavy, chaotic, sagging — gravity and liquid mass
            const float bottomWarp =
                s.edgeNoise * frame.thickness * 0.34f * style.lowerBreakupBias;

            const float bottomMass =
                base * (1.12f
                    + s.lowerSplashBias * 0.95f * style.lowerBreakupBias
                    + std::abs(s.edgeNoise) * 0.32f * style.lowerBreakupBias);

            // Downward "tearing" at high-mass zones — simulates liquid pooling
            const float downwardTear =
                frame.thickness * (
                    0.10f * gaussian(s.t, 0.22f, 0.05f) +
                    0.18f * gaussian(s.t, 0.47f, 0.05f) +
                    0.12f * gaussian(s.t, 0.72f, 0.05f));

            auto topPt = s.point + s.normal * (topTension + topWarp);
            auto botPt = s.point - s.normal * (bottomMass - bottomWarp);
            botPt.y   += downwardTear;  // push pooled zones further down

            // ── Temporal flow distortion ───────────────────────────────────────
            // The surface itself ripples with flow — not just highlights.
            // Bottom edge moves 3x stronger than top: gravity + mass bias.
            const float flowDistort =
                std::sin(s.t * 9.0f - frame.timeSeconds * 3.5f * frame.audioFlowSpeedScale)
                * frame.thickness * 0.06f;

            topPt += s.normal * flowDistort * 0.4f;
            botPt += s.normal * flowDistort * 1.2f;

            top.push_back(topPt);
            bot.push_back(botPt);
        }

        buildSmoothForward(p, top);
        buildSmoothReverse(p, bot);
        p.closeSubPath();
        return p;
    }

    //=========================================================================
    // Mist envelope — outermost soft halo, barely visible
    //=========================================================================

    juce::Path BubblegumCableGeometryCore::buildMistEnvelopePath(
        const CableResolvedFrame& frame,
        const std::vector<CableStrokeSample>& samples,
        const BubblegumCableStyleSettingsCore::Style& style) const
    {
        juce::Path p;
        if (samples.size() < 2) return p;

        std::vector<juce::Point<float>> top, bot;
        top.reserve(samples.size());
        bot.reserve(samples.size());

        for (const auto& s : samples)
        {
            const float base = frame.thickness * 0.5f * s.widthMul;
            top.push_back(s.point + s.normal * (base * style.mistTopScale));
            bot.push_back(s.point - s.normal * (base * style.mistBottomScale));
        }

        buildSmoothForward(p, top);
        buildSmoothReverse(p, bot);
        p.closeSubPath();
        return p;
    }

    //=========================================================================
    // Inner core — narrow bright spine inside the body
    //=========================================================================

    juce::Path BubblegumCableGeometryCore::buildInnerCorePath(
        const CableResolvedFrame& frame,
        const std::vector<CableStrokeSample>& samples,
        const BubblegumCableStyleSettingsCore::Style& style) const
    {
        juce::Path p;
        if (samples.size() < 2) return p;

        std::vector<juce::Point<float>> top, bot;
        top.reserve(samples.size());
        bot.reserve(samples.size());

        for (const auto& s : samples)
        {
            const float h = frame.thickness * style.coreScale * s.widthMul;
            top.push_back(s.point + s.normal * h);
            bot.push_back(s.point - s.normal * h);
        }

        buildSmoothForward(p, top);
        buildSmoothReverse(p, bot);
        p.closeSubPath();
        return p;
    }

    //=========================================================================
    // Splash drips — UPGRADED: fewer, better, with drift per drip
    //=========================================================================

    std::vector<juce::Path> BubblegumCableGeometryCore::buildSecondarySplashPaths(
        const CableResolvedFrame& frame,
        const std::vector<CableStrokeSample>& samples,
        const BubblegumCableStyleSettingsCore::Style& style) const
    {
        std::vector<juce::Path> out;
        out.reserve(8);

        for (const auto& s : samples)
        {
            // Avoid unstable ends; raised gate = fewer but more convincing drips
            if (s.t < 0.14f || s.t > 0.88f) continue;
            if (s.splashGate < 0.64f)         continue;

            const float gate = s.splashGate;
            // Tuned for thin cables: absolute minimum length ~2.5px so drips
            // read even when cable base is only 5px. Neck widened 2.4x.
            const float len  = frame.thickness * (1.8f + gate * 3.8f);
            const float neck = frame.thickness * (0.14f + gate * 0.18f);

            const juce::Point<float> down(0.0f, 1.0f);

            // Origin: bottom edge of the body at this sample
            const auto origin = s.point
                - s.normal * (frame.thickness * 0.55f * (1.0f + s.lowerSplashBias));

            // Each drip drifts sideways uniquely — no two are identical
            const float driftX =
                std::sin(s.t * 47.0f + frame.timeSeconds * 2.8f) * frame.thickness * 0.10f;

            auto tip = origin + down * len + juce::Point<float>(driftX, 0.0f);

            // ── Live underside drift ──────────────────────────────────────────
            // Drip tips breathe vertically over time — no two frames identical.
            const float driftY =
                std::sin(frame.timeSeconds * 2.2f + s.t * 10.0f)
                * frame.thickness * 0.12f;

            tip.y += driftY;

            // Cubic Bezier teardrop: wide at neck, tapers to a point at tip
            juce::Path drip;
            drip.startNewSubPath(origin + s.normal * neck);
            drip.cubicTo(
                origin + s.normal * (neck * 0.55f) + down * (len * 0.28f),
                tip    + s.normal * (neck * 0.10f) + down * (-len * 0.14f),
                tip);
            drip.cubicTo(
                tip    - s.normal * (neck * 0.10f) + down * (-len * 0.14f),
                origin - s.normal * (neck * 0.55f) + down * (len * 0.28f),
                origin - s.normal * neck);
            drip.closeSubPath();

            out.push_back(std::move(drip));
        }

        juce::ignoreUnused(style);
        return out;
    }

    //=========================================================================
    // Droplet particles — secondary, sparse, below the stream
    //=========================================================================

    std::vector<DropletPoint> BubblegumCableGeometryCore::buildDropletPoints(
        const CableResolvedFrame& frame,
        const std::vector<CableStrokeSample>& samples,
        const BubblegumCableStyleSettingsCore::Style& style) const
    {
        std::vector<DropletPoint> out;
        out.reserve(samples.size() * 2);

        for (const auto& s : samples)
        {
            if (s.t < 0.08f || s.t > 0.92f) continue;

            const float gate = s.splashGate
                             * style.dropletDensity
                             * juce::jmin(1.25f, frame.audioDropletChanceScale);

            const int count = juce::jlimit(
                0,
                style.maxDropletsPerSample,
                (int)std::floor(gate * 3.0f));

            for (int i = 0; i < count; ++i)
            {
                const float phase   = s.t * 41.0f + (float)i * 2.9f + frame.timeSeconds * 0.65f;
                const float lateral = std::sin(phase) * frame.thickness * 1.1f;
                const float downOff = frame.thickness * (0.9f + gate * 1.7f + (float)i * 0.45f);

                DropletPoint dp;
                dp.position = s.point
                            - s.normal  * downOff
                            + s.tangent * lateral * 0.15f;
                // Tuned for thin cables: floor at ~0.7px so particles don't vanish.
                dp.radius   = juce::jmax(0.7f,
                                         frame.thickness * (0.07f + gate * 0.14f));
                dp.opacity  = 0.14f + gate * 0.36f;
                out.push_back(dp);
            }
        }

        return out;
    }

    //=========================================================================
    // Catmull-Rom spline builders (tension = 0.40)
    //=========================================================================

    void BubblegumCableGeometryCore::buildSmoothForward(
        juce::Path& p,
        const std::vector<juce::Point<float>>& pts) const
    {
        if (pts.size() < 2) return;
        p.startNewSubPath(pts.front());

        const size_t n = pts.size();
        for (size_t i = 0; i + 1 < n; ++i)
        {
            const auto& p0 = pts[i > 0 ? i - 1 : 0];
            const auto& p1 = pts[i];
            const auto& p2 = pts[i + 1];
            const auto& p3 = pts[i + 2 < n ? i + 2 : n - 1];

            const auto cp1 = p1 + (p2 - p0) * tension;
            const auto cp2 = p2 - (p3 - p1) * tension;
            p.cubicTo(cp1.x, cp1.y, cp2.x, cp2.y, p2.x, p2.y);
        }
    }

    void BubblegumCableGeometryCore::buildSmoothReverse(
        juce::Path& p,
        const std::vector<juce::Point<float>>& pts) const
    {
        if (pts.empty()) return;
        p.lineTo(pts.back());  // right-side cap

        const size_t n = pts.size();
        for (size_t i = n - 1; i > 0; --i)
        {
            const auto& p0 = pts[i < n - 1 ? i + 1 : n - 1];
            const auto& p1 = pts[i];
            const auto& p2 = pts[i - 1];
            const auto& p3 = pts[i >= 2 ? i - 2 : 0];

            const auto cp1 = p1 + (p2 - p0) * tension;
            const auto cp2 = p2 - (p3 - p1) * tension;
            p.cubicTo(cp1.x, cp1.y, cp2.x, cp2.y, p2.x, p2.y);
        }
        // caller closes the subpath (left-side cap back to top front)
    }

    //=========================================================================
    // Signal functions — UPGRADED for stronger pressure read
    //=========================================================================

    float BubblegumCableGeometryCore::computeWidthMul(
        const CableResolvedFrame& frame,
        float t,
        const BubblegumCableStyleSettingsCore::Style& style,
        float lengthScale) const noexcept
    {
        const float time  = frame.timeSeconds;
        const float audio = frame.audioEnergy01;

        // ── Moving bulges ────────────────────────────────────────────────────
        // Mass travels forward along the cable over time. Audio modulates speed.
        // This is what makes the stream read as "flowing" not "static with ripples".
        const float flowOffset = time * 0.22f * frame.audioFlowSpeedScale;

        // Five major structural bulges — varied heights, shifting position
        const float majorBulges =
            0.70f * gaussian(t + flowOffset, 0.12f, 0.06f) +
            1.05f * gaussian(t + flowOffset, 0.29f, 0.07f) +
            1.20f * gaussian(t + flowOffset, 0.48f, 0.06f) +
            0.92f * gaussian(t + flowOffset, 0.68f, 0.07f) +
            0.62f * gaussian(t + flowOffset, 0.86f, 0.05f);

        // Turbulent ripple riding on top of the major bulges
        const float turbulentLumps =
            0.22f * std::sin(t * 13.0f - time * 2.6f) +
            0.16f * std::sin(t * 31.0f + time * 3.7f + 0.6f) +
            0.10f * std::sin(t * 57.0f - time * 5.1f + 1.7f);

        // Pinch zones between bulges — creates neck narrowings like real pressure streams
        const float pinch =
            -0.18f * gaussian(t, 0.21f, 0.035f) +
            -0.14f * gaussian(t, 0.58f, 0.040f) +
            -0.10f * gaussian(t, 0.79f, 0.030f);

        // End taper: narrower at source and destination anchors
        const float taper = 0.68f + 0.24f * std::sin(t * juce::MathConstants<float>::pi);

        const float audioBoost = 1.0f + audio * 0.12f;
        const float raw = taper + ((majorBulges + turbulentLumps + pinch) * lengthScale);
        return juce::jlimit(style.widthMulMin, style.widthMulMax, raw * audioBoost);
    }

    float BubblegumCableGeometryCore::computeEdgeNoise(
        const CableResolvedFrame& frame,
        float t,
        const BubblegumCableStyleSettingsCore::Style& style,
        float lengthScale) const noexcept
    {
        const float time = frame.timeSeconds;

        // Four frequency bands — produces sharper, less vector-clean edge breakup
        const float n1 = std::sin(t * 18.0f  + time * 2.4f);
        const float n2 = std::sin(t * 42.0f  - time * 3.7f + 0.8f);
        const float n3 = std::sin(t * 77.0f  + time * 6.2f + 1.6f);
        const float n4 = std::sin(t * 121.0f - time * 8.5f + 2.1f);

        const float combined = n1 * 0.34f + n2 * 0.24f + n3 * 0.14f + n4 * 0.08f;

        // ── Cinematic-only: silhouette breathing ──────────────────────────────
        // Very high-frequency micro-distortion at low amplitude. Kept off in
        // Performance mode to save the per-sample sin() call.
        float extra = 0.0f;
        if (style.qualityMode == BubblegumCableStyleSettingsCore::Style::QualityMode::Cinematic)
        {
            extra = std::sin(t * 173.0f + time * 9.0f) * 0.035f;
        }

        return juce::jlimit(-1.0f, 1.0f, combined * lengthScale + extra);
    }

    float BubblegumCableGeometryCore::computeLowerSplashBias(
        float t,
        float lengthScale) const noexcept
    {
        // Four zones of downward mass — more zones = more irregular underside
        const float bias =
            0.48f * gaussian(t, 0.18f, 0.08f) +
            0.72f * gaussian(t, 0.43f, 0.08f) +
            0.56f * gaussian(t, 0.63f, 0.07f) +
            0.34f * gaussian(t, 0.82f, 0.06f);

        return juce::jlimit(0.0f, 1.10f, bias * lengthScale);
    }

    float BubblegumCableGeometryCore::splashPresence(
        const CableResolvedFrame& frame,
        float t,
        float lengthScale) const noexcept
    {
        const float time = frame.timeSeconds;

        const float structural =
            0.72f * gaussian(t, 0.20f, 0.07f) +
            0.96f * gaussian(t, 0.46f, 0.06f) +
            0.60f * gaussian(t, 0.74f, 0.07f);

        const float flutter =
            0.20f * std::sin(t * 20.0f + time * 3.3f) +
            0.10f * std::sin(t * 43.0f - time * 5.4f);

        return juce::jlimit(0.0f, 1.0f, (structural + flutter) * lengthScale);
    }

    float BubblegumCableGeometryCore::gaussian(float x, float center, float width) noexcept
    {
        const float d = (x - center) / juce::jmax(0.0001f, width);
        return std::exp(-(d * d));
    }

} // namespace bubblegum
