#pragma once

#include "ApexTuneLivePitchTracker.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace APEX::TuneLive {

// Causal normalized squared-difference detector. Short periods use a short
// comparison. Lower voices receive at least one full period of evidence.
class RealtimePitchTracker final {
public:
    void prepare(double rate) noexcept
    {
        rate = std::isfinite(rate) && rate >= 8000.0 && rate <= 192000.0 ? rate : 48000.0;
        stride_ = std::max(1, static_cast<int>(std::lround(rate / 8000.0)));
        detectorRate_ = rate / stride_;
        firstLag_ = std::max(3, static_cast<int>(std::floor(detectorRate_ / 1200.0)));
        lastLag_ = static_cast<int>(std::ceil(detectorRate_ / 55.0));
        hop_ = std::max(4, static_cast<int>(std::lround(detectorRate_ * 0.001)));
        constexpr double tau = 6.28318530717958647692;
        lowPassStep_ = static_cast<float>(-std::expm1(-tau * 1600.0 / rate));
        dcStep_ = static_cast<float>(-std::expm1(-tau * 40.0 / rate));
        reset();
    }

    void reset() noexcept
    {
        samples_.fill(0.0f);
        energy_.fill(0.0);
        difference_.fill(0.0);
        position_ = 0;
        ready_ = decimationPhase_ = analysisPhase_ = missingFrames_ = 0;
        dc_ = low1_ = low2_ = pendingHz_ = 0.0f;
        result_ = {};
    }

    void setVocalRange(VocalRange range) noexcept
    {
        if (range == range_)
            return;
        range_ = range;
        result_ = {};
        pendingHz_ = 0.0f;
        missingFrames_ = 0;
    }

    bool push(float input) noexcept
    {
        input = std::isfinite(input) ? input : 0.0f;
        dc_ += dcStep_ * (input - dc_);
        low1_ += lowPassStep_ * (input - dc_ - low1_);
        low2_ += lowPassStep_ * (low1_ - low2_);
        if (++decimationPhase_ < stride_)
            return false;
        decimationPhase_ = 0;
        samples_[position_] = low2_;
        energy_[position_] = energy_[historyIndex(1)] + static_cast<double>(low2_) * low2_;
        ready_ = std::min(ready_ + 1, kHistory);

        const int first = firstLag_ - 1;
        const int shortWindowEnd = std::min(kMinimumWindow, lastLag_ + 2);
        // In short lags every candidate shares the same recent comparison
        // window. At longer lags it scales with the candidate period.
        const double departing = sampleAtAge(kMinimumWindow);
        for (int lag = first; lag < shortWindowEnd; ++lag)
            accumulateDifference(lag, kMinimumWindow, departing);
        for (int lag = shortWindowEnd; lag <= lastLag_ + 1; ++lag)
            accumulateDifference(lag, lag, sampleAtAge(lag));

        if (++analysisPhase_ >= hop_)
        {
            analysisPhase_ = 0;
            update(analyse());
        }
        position_ = (position_ + 1) & (kHistory - 1);
        return analysisPhase_ == 0;
    }

    PitchTracker::Result latest() const noexcept { return result_; }
    int hopSamples() const noexcept { return stride_ * hop_; }
    double hopMilliseconds() const noexcept { return 1000.0 * hop_ / detectorRate_; }

private:
    static constexpr int kHistory = 1024;
    static constexpr int kMaxLag = 224;
    static constexpr int kMinimumWindow = 32;

    unsigned historyIndex(int age) const noexcept
    {
        return (position_ - static_cast<unsigned>(age)) & (kHistory - 1);
    }

    float sampleAtAge(int age) const noexcept { return samples_[historyIndex(age)]; }
    double energyAtAge(int age) const noexcept { return energy_[historyIndex(age)]; }

    void accumulateDifference(int lag, int length, double departing) noexcept
    {
        const double entering = static_cast<double>(low2_) - sampleAtAge(lag);
        const double leaving = departing - sampleAtAge(length + lag);
        difference_[static_cast<std::size_t>(lag)] += entering * entering - leaving * leaving;
    }

    bool isPeak(int lag) const noexcept
    {
        return scores_[lag] > scores_[lag - 1] && scores_[lag] >= scores_[lag + 1];
    }

    float peakFrequency(int lag) const noexcept
    {
        const float left = scores_[lag - 1];
        const float centre = scores_[lag];
        const float right = scores_[lag + 1];
        const float curvature = left - 2.0f * centre + right;
        const float fraction = curvature < -1.0e-6f
            ? std::clamp(0.5f * (left - right) / curvature, -0.5f, 0.5f) : 0.0f;
        return static_cast<float>(detectorRate_ / (lag + fraction));
    }

    PitchTracker::Result analyse() noexcept
    {
        // A correlation peak calculated from trailing voiced history can
        // misidentify near-silence as a strong 55-Hz subharmonic. The most
        // recent millisecond must actually contain audible voiced energy;
        // checking only the larger, lag-dependent history is insufficient.
        // Uses the existing prefix-energy ring: no allocation or FFT.
        constexpr int kRecentSamples = 8;
        constexpr double kMinimumRecentMeanSquare = 8.0e-6;
        if (ready_ <= kRecentSamples ||
            energyAtAge(0) - energyAtAge(kRecentSamples)
                < kMinimumRecentMeanSquare * kRecentSamples)
            return {};
        const auto& range = rangeInfo(range_);
        const int low = std::max(firstLag_, static_cast<int>(std::floor(detectorRate_ / range.maximumHz)));
        const int high = std::min(lastLag_, static_cast<int>(std::ceil(detectorRate_ / range.minimumHz)));
        for (int lag = low - 1; lag <= high + 1; ++lag)
        {
            const int length = std::max(kMinimumWindow, lag);
            const double power = (energyAtAge(0) - energyAtAge(length))
                               + (energyAtAge(lag) - energyAtAge(length + lag));
            scores_[lag] = ready_ >= lag + length && power >= 8.0e-6 * length
                ? static_cast<float>(1.0 - std::max(0.0, difference_[lag]) / power) : 0.0f;
        }

        float best = 0.0f;
        for (int lag = low; lag <= high; ++lag)
            if (isPeak(lag))
                best = std::max(best, scores_[lag]);

        const float minimum = std::max(result_.voiced ? 0.78f : 0.90f, best - 0.18f);
        const float previousPeriod = result_.voiced ? static_cast<float>(detectorRate_ / result_.hz) : 0.0f;
        int selected = 0;
        float selectedScore = 0.0f;
        float selectedHz = 0.0f;
        for (int lag = low; lag <= high; ++lag)
        {
            const float clarity = scores_[lag];
            if (clarity < minimum || !isPeak(lag))
                continue;
            const float hz = peakFrequency(lag);
            if (hz < range.minimumHz || hz > range.maximumHz)
                continue;

            // A running tone can persist through weaker vowel cycles.
            // Penalise exact multiples of a credible short period so the
            // continuity preference cannot trap a genuine octave-up leap.
            float score = clarity;
            if (std::abs(lag - previousPeriod) < 0.11f * previousPeriod)
                score += 0.10f;
            if (selected > 0)
            {
                const float multiple = static_cast<float>(lag) / selected;
                const int harmonic = static_cast<int>(multiple + 0.5f);
                if (harmonic >= 2 && std::abs(multiple - harmonic) < 0.08f * multiple)
                    score -= 0.11f;
            }
            if (selected == 0 || score > selectedScore)
            {
                selected = lag;
                selectedScore = score;
                selectedHz = hz;
            }
        }
        return selected > 0 ? PitchTracker::Result{selectedHz, scores_[selected], true}
                            : PitchTracker::Result{};
    }

    void update(PitchTracker::Result candidate) noexcept
    {
        if (!candidate.voiced)
        {
            pendingHz_ = 0.0f;
            if (++missingFrames_ >= 2)
            {
                result_ = {};
                missingFrames_ = 2;
            }
            return;
        }
        missingFrames_ = 0;
        const bool nearCurrent = result_.voiced &&
            candidate.hz > 0.945f * result_.hz && candidate.hz < 1.06f * result_.hz;
        const bool confirmedChange = pendingHz_ > 0.0f &&
            candidate.hz > 0.974f * pendingHz_ && candidate.hz < 1.027f * pendingHz_;
        pendingHz_ = candidate.hz;
        if (nearCurrent || confirmedChange)
            result_ = candidate;
    }

    std::array<float, kHistory> samples_{};
    std::array<double, kHistory> energy_{};
    std::array<double, kMaxLag + 1> difference_{};
    std::array<float, kMaxLag + 1> scores_{};
    PitchTracker::Result result_{};
    VocalRange range_ = VocalRange::Automatic;
    double detectorRate_ = 8000.0;
    float lowPassStep_ = 0.0f, dcStep_ = 0.0f;
    float dc_ = 0.0f, low1_ = 0.0f, low2_ = 0.0f, pendingHz_ = 0.0f;
    unsigned position_ = 0;
    int stride_ = 6, hop_ = 8, firstLag_ = 6, lastLag_ = 146;
    int ready_ = 0, decimationPhase_ = 0, analysisPhase_ = 0, missingFrames_ = 0;
};

} // namespace APEX::TuneLive
