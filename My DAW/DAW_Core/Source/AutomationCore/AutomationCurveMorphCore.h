#pragma once
#include "AutomationLaneCore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace DAW {

/**
 * Offline UI-thread curve-to-curve morph. This generates ordinary automation
 * breakpoints; it does not run a second modulator in the audio callback.
 *
 * At blend=0 and blend=1 source shape/tension is kept exactly. Intermediate
 * blends sample every original knot plus 7 interior positions per segment:
 * bounded approximation of all 14 source curve families using LINEAR knots.
 * Both inputs MUST refer to the exact same parameter ID.
 */
struct AutomationCurveMorphCore
{
    static constexpr size_t maxInputKnots = 4096;
    static constexpr size_t maxOutputKnots = 32768;
    static constexpr int substeps = 8;

    static bool blend(const juce::String& parameterID,
                      const std::vector<AutomationPoint>& left,
                      const std::vector<AutomationPoint>& right,
                      float amount,
                      std::vector<AutomationPoint>& output)
    {
        if (parameterID.isEmpty() || left.empty() || right.empty()
            || left.size() + right.size() > maxInputKnots
            || !std::isfinite(amount) || amount < 0.0f || amount > 1.0f)
            return false;

        const auto valid = [](const std::vector<AutomationPoint>& points)
        {
            int64_t prev = -1;
            for (const auto& p : points)
            {
                if (p.timeSamples < 0 || p.timeSamples <= prev
                    || !std::isfinite(p.value) || !std::isfinite(p.tensionToNext))
                    return false;
                prev = p.timeSamples;
            }
            return true;
        };
        if (!valid(left) || !valid(right))
            return false;

        if (amount == 0.0f) { output = left; return true; }
        if (amount == 1.0f) { output = right; return true; }

        std::vector<int64_t> times;
        times.reserve(left.size() + right.size());
        for (const auto& p : left) times.push_back(p.timeSamples);
        for (const auto& p : right) times.push_back(p.timeSamples);
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());

        if (times.size() > (maxOutputKnots - 1) / substeps)
            return false;

        AutomationLaneCore a("morph-left", parameterID);
        AutomationLaneCore b("morph-right", parameterID);
        a.points = left;
        b.points = right;

        std::vector<AutomationPoint> next;
        next.reserve(times.size() * substeps);
        const auto add = [&](int64_t when)
        {
            const float va = a.getValueAtSample(when, left.front().value);
            const float vb = b.getValueAtSample(when, right.front().value);
            if (!std::isfinite(va) || !std::isfinite(vb))
                return false;

            AutomationPoint p;
            p.timeSamples = when;
            p.value = AutomationLaneCore::clampValueForParameter(
                parameterID, va + amount * (vb - va));
            p.curveToNext = AutomationCurveType::Linear;
            p.tensionToNext = 0.0f;
            if (next.empty() || next.back().timeSamples != when)
                next.push_back(p);
            return next.size() <= maxOutputKnots;
        };

        for (size_t i = 0; i + 1 < times.size(); ++i)
        {
            const int64_t lo = times[i], hi = times[i + 1];
            const double width = static_cast<double>(hi) - static_cast<double>(lo);
            for (int part = 0; part < substeps; ++part)
            {
                const int64_t pos = lo + static_cast<int64_t>(
                    std::llround(width * static_cast<double>(part) / substeps));
                if (!add(pos))
                    return false;
            }
        }
        if (!add(times.back()))
            return false;

        output = std::move(next);
        return true;
    }
};

} // namespace DAW
