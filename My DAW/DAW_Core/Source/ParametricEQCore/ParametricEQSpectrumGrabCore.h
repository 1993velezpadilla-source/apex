#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace APEX::ParametricEQ
{

struct GrabPeak
{
    double frequencyHz = 0.0;
    double prominenceDb = 0.0;
    double suggestedQ = 1.0;
};

struct GrabPlan
{
    static constexpr int kMaxPeaks = 8;
    std::array<GrabPeak, kMaxPeaks> peaks {};
    int count = 0;
};

// APEX Spectrum Grab: bounded, deterministic, GUI-only inspection of a frozen
// PRE-EQ FFT frame. Finds resonances above their local log-frequency baseline,
// not the loudest bass bins. Neither the processor nor its parameter ABI changes.
// This must never be called from the realtime audio callback.
class SpectrumGrab
{
public:
    static GrabPlan analyze (const float* db, int bins, double binHz) noexcept
    {
        GrabPlan result;
        if (db == nullptr || bins < 16 || ! std::isfinite (binHz)
            || binHz <= 0.0)
            return result;

        const int low = std::max (3, static_cast<int> (
            std::ceil (45.0 / binHz)));
        const int high = std::min (bins - 4, static_cast<int> (
            std::floor (std::min (18000.0, (bins - 1) * binHz * 0.95)
                        / binHz)));
        if (low >= high)
            return result;

        constexpr double leftRatio = 0.78;   // ~1/3 octave shoulders
        constexpr double rightRatio = 1.28;
        constexpr double minProminenceDb = 7.0;
        constexpr double minPeakDb = -85.0;
        constexpr double spacingOctaves = 0.19;

        for (int bin = low; bin <= high; ++bin)
        {
            const double level = db[bin];
            if (! std::isfinite (level) || level < minPeakDb || level > 48.0)
                continue;

            // Require a real local maximum, rejecting flat spectra and
            // one-sided rises. Adjacent duplicate maxima are rejected.
            if (! (level > db[bin - 1] && level >= db[bin + 1]
                   && level >= db[bin - 2] && level >= db[bin + 2]))
                continue;

            const int l = std::clamp (static_cast<int> (
                std::floor (bin * leftRatio)), 1, bin - 2);
            const int r = std::clamp (static_cast<int> (
                std::ceil (bin * rightRatio)), bin + 2, bins - 2);
            if (! std::isfinite (db[l]) || ! std::isfinite (db[r]))
                continue;

            const double prominence = level - 0.5 * (db[l] + db[r]);
            if (! std::isfinite (prominence)
                || prominence < minProminenceDb)
                continue;

            // Q is an initial editing suggestion derived from the measured
            // resonance width, with safe limits for unresolved FFT peaks.
            const double halfHeight = level - std::max (3.0, prominence * 0.5);
            int left = bin, right = bin;
            while (left > low && std::isfinite (db[left - 1])
                   && db[left - 1] > halfHeight)
                --left;
            while (right < high && std::isfinite (db[right + 1])
                   && db[right + 1] > halfHeight)
                ++right;
            const double widthHz = std::max (
                binHz, (right - left + 1) * binHz);
            const GrabPeak candidate {
                bin * binHz, prominence,
                std::clamp ((bin * binHz) / widthHz, 0.7, 20.0)
            };

            int duplicate = -1;
            for (int index = 0; index < result.count; ++index)
            {
                const auto& existing = result.peaks[
                    static_cast<std::size_t> (index)];
                if (std::abs (std::log2 (
                        candidate.frequencyHz / existing.frequencyHz))
                    < spacingOctaves)
                {
                    duplicate = index;
                    break;
                }
            }

            if (duplicate >= 0)
            {
                auto& existing = result.peaks[
                    static_cast<std::size_t> (duplicate)];
                if (candidate.prominenceDb > existing.prominenceDb)
                    existing = candidate;
            }
            else if (result.count < GrabPlan::kMaxPeaks)
            {
                result.peaks[static_cast<std::size_t> (result.count++)]
                    = candidate;
            }
            else
            {
                int weakest = 0;
                for (int index = 1; index < result.count; ++index)
                    if (result.peaks[static_cast<std::size_t> (index)]
                            .prominenceDb
                        < result.peaks[static_cast<std::size_t> (weakest)]
                            .prominenceDb)
                        weakest = index;
                if (candidate.prominenceDb > result.peaks[
                        static_cast<std::size_t> (weakest)].prominenceDb)
                    result.peaks[static_cast<std::size_t> (weakest)]
                        = candidate;
            }
        }

        std::sort (result.peaks.begin(),
                   result.peaks.begin() + result.count,
                   [] (const GrabPeak& a, const GrabPeak& b)
                   {
                       return a.prominenceDb > b.prominenceDb;
                   });
        return result;
    }
};

} // namespace APEX::ParametricEQ
