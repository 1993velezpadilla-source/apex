#pragma once

#include "ParametricEQTypes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace APEX::ParametricEQ
{

struct SketchPoint
{
    double x = 0.0;       // normalised logarithmic-frequency axis [0, 1]
    double gainDb = 0.0;  // requested display gain
};

struct SketchBand
{
    FilterShape shape = FilterShape::Bell;
    double frequencyHz = 1000.0;
    double gainDb = 0.0;
    double q = 1.0;
};

struct SketchPlan
{
    static constexpr int kMaximumBands = 12;
    std::array<SketchBand, kMaximumBands> bands {};
    int count = 0;
};

// GUI/control-thread utility for APEX EQ Sketch. It converts a freehand curve
// into a bounded set of ordinary APEX EQ bands. No allocations are required;
// the resulting bands are then published through the normal hosted parameters.
class SketchPlanner
{
public:
    static constexpr int kMaximumInputPoints = 128;
    static constexpr int kResamplePoints = 64;

    static SketchPlan plan (const SketchPoint* input, int count,
                            double minimumFrequencyHz = 20.0,
                            double maximumFrequencyHz = 24000.0) noexcept
    {
        SketchPlan result;
        if (input == nullptr || count < 2)
            return result;

        const auto minHz = std::max (1.0, minimumFrequencyHz);
        const auto maxHz = std::max (minHz * 1.01, maximumFrequencyHz);
        std::array<SketchPoint, kMaximumInputPoints> points {};
        int valid = 0;
        for (int index = 0; index < std::min (count, kMaximumInputPoints); ++index)
        {
            if (! std::isfinite (input[index].x)
                || ! std::isfinite (input[index].gainDb))
                continue;
            points[static_cast<std::size_t> (valid++)] = {
                std::clamp (input[index].x, 0.0, 1.0),
                std::clamp (input[index].gainDb, -18.0, 18.0)
            };
        }
        if (valid < 2)
            return result;

        std::sort (points.begin(), points.begin() + valid,
                   [] (const SketchPoint& a, const SketchPoint& b)
                   {
                       return a.x < b.x;
                   });

        // Collapse effectively duplicate X coordinates, keeping their mean.
        std::array<SketchPoint, kMaximumInputPoints> unique {};
        int uniqueCount = 0;
        for (int index = 0; index < valid;)
        {
            const double x = points[static_cast<std::size_t> (index)].x;
            double total = 0.0;
            int samples = 0;
            int next = index;
            while (next < valid
                   && std::abs (points[static_cast<std::size_t> (next)].x - x)
                        < 1.0e-4)
            {
                total += points[static_cast<std::size_t> (next)].gainDb;
                ++samples;
                ++next;
            }
            unique[static_cast<std::size_t> (uniqueCount++)]
                = { x, total / std::max (1, samples) };
            index = next;
        }
        if (uniqueCount < 2)
            return result;

        std::array<double, kResamplePoints> curve {};
        int segment = 0;
        for (int i = 0; i < kResamplePoints; ++i)
        {
            const double x = static_cast<double> (i) / (kResamplePoints - 1);
            while (segment + 1 < uniqueCount
                   && unique[static_cast<std::size_t> (segment + 1)].x < x)
                ++segment;
            if (x <= unique[0].x)
                curve[static_cast<std::size_t> (i)] = unique[0].gainDb;
            else if (x >= unique[static_cast<std::size_t> (uniqueCount - 1)].x)
                curve[static_cast<std::size_t> (i)]
                    = unique[static_cast<std::size_t> (uniqueCount - 1)].gainDb;
            else
            {
                const auto& a = unique[static_cast<std::size_t> (segment)];
                const auto& b = unique[static_cast<std::size_t> (
                    std::min (segment + 1, uniqueCount - 1))];
                const double span = std::max (1.0e-9, b.x - a.x);
                const double t = std::clamp ((x - a.x) / span, 0.0, 1.0);
                curve[static_cast<std::size_t> (i)]
                    = a.gainDb + t * (b.gainDb - a.gainDb);
            }
        }

        // Two fixed smoothing passes suppress pointer jitter while retaining
        // broad intentional features.
        for (int pass = 0; pass < 2; ++pass)
        {
            auto copy = curve;
            for (int i = 1; i < kResamplePoints - 1; ++i)
                curve[static_cast<std::size_t> (i)]
                    = 0.25 * copy[static_cast<std::size_t> (i - 1)]
                    + 0.50 * copy[static_cast<std::size_t> (i)]
                    + 0.25 * copy[static_cast<std::size_t> (i + 1)];
        }

        struct Candidate
        {
            int index = 0;
            double score = 0.0;
        };
        std::array<Candidate, kResamplePoints> candidates {};
        int candidateCount = 0;

        for (int i = 2; i < kResamplePoints - 2; ++i)
        {
            const double centre = curve[static_cast<std::size_t> (i)];
            if (std::abs (centre) < 0.65)
                continue;
            const double leftSlope = centre - curve[static_cast<std::size_t> (i - 2)];
            const double rightSlope = curve[static_cast<std::size_t> (i + 2)] - centre;
            const bool maximum = leftSlope > 0.0 && rightSlope < 0.0;
            const bool minimum = leftSlope < 0.0 && rightSlope > 0.0;
            if (! maximum && ! minimum)
                continue;

            const double localReference = 0.5 * (
                curve[static_cast<std::size_t> (i - 2)]
              + curve[static_cast<std::size_t> (i + 2)]);
            const double prominence = std::abs (centre - localReference);
            if (prominence < 0.20 && std::abs (centre) < 1.5)
                continue;
            candidates[static_cast<std::size_t> (candidateCount++)]
                = { i, std::abs (centre) + 1.5 * prominence };
        }

        std::sort (candidates.begin(), candidates.begin() + candidateCount,
                   [] (const Candidate& a, const Candidate& b)
                   {
                       return a.score > b.score;
                   });

        auto frequencyForIndex = [minHz, maxHz] (int index) noexcept
        {
            const double x = static_cast<double> (index) / (kResamplePoints - 1);
            return minHz * std::pow (maxHz / minHz, x);
        };

        auto append = [&result] (const SketchBand& band)
        {
            if (result.count < SketchPlan::kMaximumBands)
                result.bands[static_cast<std::size_t> (result.count++)] = band;
        };

        // Broad non-zero tails are represented as shelves before interior
        // extrema are selected.
        double lowMean = 0.0, highMean = 0.0;
        constexpr int tail = 6;
        for (int i = 0; i < tail; ++i)
        {
            lowMean += curve[static_cast<std::size_t> (i)];
            highMean += curve[static_cast<std::size_t> (
                kResamplePoints - 1 - i)];
        }
        lowMean /= tail;
        highMean /= tail;
        if (std::abs (lowMean) >= 1.0)
            append ({ FilterShape::LowShelf, frequencyForIndex (5),
                      std::clamp (lowMean, -18.0, 18.0), 0.707 });
        if (std::abs (highMean) >= 1.0)
            append ({ FilterShape::HighShelf,
                      frequencyForIndex (kResamplePoints - 6),
                      std::clamp (highMean, -18.0, 18.0), 0.707 });

        std::array<bool, kResamplePoints> selected {};
        for (int candidate = 0;
             candidate < candidateCount && result.count < SketchPlan::kMaximumBands;
             ++candidate)
        {
            const int index = candidates[static_cast<std::size_t> (candidate)].index;
            bool tooClose = false;
            for (int delta = -4; delta <= 4; ++delta)
            {
                const int nearby = index + delta;
                if (nearby >= 0 && nearby < kResamplePoints
                    && selected[static_cast<std::size_t> (nearby)])
                {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose)
                continue;
            selected[static_cast<std::size_t> (index)] = true;

            const double gain = curve[static_cast<std::size_t> (index)];
            const double half = std::abs (gain) * 0.5;
            int left = index;
            int right = index;
            while (left > 0
                   && std::abs (curve[static_cast<std::size_t> (left)]) > half)
                --left;
            while (right < kResamplePoints - 1
                   && std::abs (curve[static_cast<std::size_t> (right)]) > half)
                ++right;
            const double low = frequencyForIndex (left);
            const double high = frequencyForIndex (right);
            const double bandwidthOctaves = std::max (
                0.08, std::log2 (std::max (high, low * 1.0001) / low));
            const double q = std::clamp (
                1.0 / (2.0 * std::sinh (
                    std::log (2.0) * bandwidthOctaves * 0.5)),
                0.25, 12.0);
            append ({ FilterShape::Bell, frequencyForIndex (index),
                      std::clamp (gain, -18.0, 18.0), q });
        }

        // A smooth non-flat gesture may have no local extremum (for example a
        // broad diagonal). Represent its strongest point rather than silently
        // producing an empty plan.
        if (result.count == 0)
        {
            int strongest = 0;
            for (int i = 1; i < kResamplePoints; ++i)
                if (std::abs (curve[static_cast<std::size_t> (i)])
                    > std::abs (curve[static_cast<std::size_t> (strongest)]))
                    strongest = i;
            if (std::abs (curve[static_cast<std::size_t> (strongest)]) >= 0.75)
                append ({ FilterShape::Bell, frequencyForIndex (strongest),
                          curve[static_cast<std::size_t> (strongest)], 0.707 });
        }

        std::sort (result.bands.begin(), result.bands.begin() + result.count,
                   [] (const SketchBand& a, const SketchBand& b)
                   {
                       return a.frequencyHz < b.frequencyHz;
                   });
        return result;
    }
};

} // namespace APEX::ParametricEQ
