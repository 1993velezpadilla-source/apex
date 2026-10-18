#pragma once

#include "ParametricEQSketchCore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace APEX::ParametricEQ
{

// Independently designed APEX Match. All work runs on the JUCE message
// thread, using pinned *PRE* FFT frames copied to fixed-size arrays.
// This is not a sample-accurate or proprietary FabFilter EQ Match algorithm.
class MatchCapture
{
public:
    static constexpr int kPoints = 96;
    static constexpr int kRequiredFrames = 32;
    static constexpr double kMinimumHz = 40.0;
    static constexpr double kMaximumHz = 16000.0;

    void reset() noexcept
    {
        sumPower_.fill (0.0);
        frames_ = 0;
        binHz_ = 0.0;
    }

    int frameCount() const noexcept { return frames_; }
    bool ready() const noexcept { return frames_ >= kRequiredFrames; }

    bool append (const float* db, int bins, double binHz) noexcept
    {
        if (db == nullptr || bins < 64 || ! std::isfinite (binHz)
            || binHz <= 0.0 || ready())
            return false;
        if (binHz_ > 0.0 && std::abs (binHz / binHz_ - 1.0) > 0.002)
        {
            // New sample rate: discard the incomplete capture; do not blend
            // FFT profiles from different frequency scales.
            reset();
        }
        if ((bins - 2) * binHz < kMaximumHz)
            return false;

        std::array<double, kPoints> powers {};
        double loudestDb = -160.0;
        for (int point = 0; point < kPoints; ++point)
        {
            const double x = static_cast<double> (point) / (kPoints - 1);
            const double frequency = kMinimumHz
                * std::pow (kMaximumHz / kMinimumHz, x);
            const double fractional = frequency / binHz;
            const int left = std::clamp (
                static_cast<int> (fractional), 0, bins - 2);
            const double fraction = std::clamp (
                fractional - left, 0.0, 1.0);
            const double l = static_cast<double> (db[left]);
            const double r = static_cast<double> (db[left + 1]);
            if (! std::isfinite (l) || ! std::isfinite (r)
                || l > 48.0 || r > 48.0)
                return false;  // atomic reject: zero partial updates
            const double level = std::clamp (
                l + fraction * (r - l), -160.0, 48.0);
            loudestDb = std::max (loudestDb, level);
            powers[static_cast<std::size_t> (point)]
                = std::pow (10.0, 0.1 * level);
        }
        if (loudestDb < -75.0)
            return false;  // don't learn an almost silent noise floor

        for (int point = 0; point < kPoints; ++point)
            sumPower_[static_cast<std::size_t> (point)]
                += powers[static_cast<std::size_t> (point)];
        binHz_ = binHz;
        ++frames_;
        return true;
    }

    bool magnitudeDb (std::array<double, kPoints>& output) const noexcept
    {
        if (! ready())
            return false;
        for (int point = 0; point < kPoints; ++point)
        {
            const double power = sumPower_[static_cast<std::size_t> (point)]
                               / static_cast<double> (frames_);
            output[static_cast<std::size_t> (point)]
                = 10.0 * std::log10 (std::max (1.0e-16, power));
        }
        return true;
    }

private:
    std::array<double, kPoints> sumPower_ {};
    int frames_ = 0;
    double binHz_ = 0.0;
};

class MatchEngine
{
public:
    // Reference minus input, after rejecting global volume differences.
    // The existing native Sketch planner bounds the proposal to 12 ordinary
    // hosted EQ bands, and Apply is a separate explicit user action.
    static SketchPlan fit (const MatchCapture& reference,
                           const MatchCapture& input) noexcept
    {
        SketchPlan empty;
        std::array<double, MatchCapture::kPoints> refDb {}, inDb {};
        if (! reference.magnitudeDb (refDb) || ! input.magnitudeDb (inDb))
            return empty;
        std::array<double, MatchCapture::kPoints> differences {};
        for (int i = 0; i < MatchCapture::kPoints; ++i)
            differences[static_cast<std::size_t> (i)]
                = refDb[static_cast<std::size_t> (i)]
                - inDb[static_cast<std::size_t> (i)];

        // Median rejects an overall playback gain mismatch, preserving
        // localized tonal differences and reducing excessive global boosts.
        auto ranked = differences;
        std::sort (ranked.begin(), ranked.end());
        constexpr int n = MatchCapture::kPoints;
        const double overallDb = 0.5 * (
            ranked[static_cast<std::size_t> (n / 2 - 1)]
          + ranked[static_cast<std::size_t> (n / 2)]);

        for (auto& db : differences)
            db = std::clamp (db - overallDb, -9.0, 9.0);
        // Smooth narrow analyzer-bin artifacts before freehand fitting.
        for (int pass = 0; pass < 3; ++pass)
        {
            const auto before = differences;
            for (int i = 1; i < n - 1; ++i)
                differences[static_cast<std::size_t> (i)]
                    = 0.25 * before[static_cast<std::size_t> (i - 1)]
                    + 0.50 * before[static_cast<std::size_t> (i)]
                    + 0.25 * before[static_cast<std::size_t> (i + 1)];
        }
        std::array<SketchPoint, n> sketch {};
        double maximumDifference = 0.0;
        constexpr double logMin = 0.0; // computed for frequency remap below
        (void) logMin;
        const double denominator = std::log (
            24000.0 / 20.0);
        for (int i = 0; i < n; ++i)
        {
            const double x = static_cast<double> (i) / (n - 1);
            const double hz = MatchCapture::kMinimumHz * std::pow (
                MatchCapture::kMaximumHz / MatchCapture::kMinimumHz, x);
            sketch[static_cast<std::size_t> (i)] = {
                std::log (hz / 20.0) / denominator,
                differences[static_cast<std::size_t> (i)]
            };
            maximumDifference = std::max (
                maximumDifference,
                std::abs (differences[static_cast<std::size_t> (i)]));
        }
        if (maximumDifference < 1.25)
            return empty;
        return SketchPlanner::plan (sketch.data(), n, 20.0, 24000.0);
    }
};

} // namespace APEX::ParametricEQ
