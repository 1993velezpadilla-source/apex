#pragma once
// APEX Tune Live: standalone, allocation-free, mono YIN pitch tracker.
// Original implementation of the published YIN difference/CMNDF method.
// Not derived from proprietary pitch-correction plugin code.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace APEX { namespace TuneLive {
class PitchTracker final {
public:
    static constexpr int kWindow = 512;
    static constexpr int kFastWindow = 256;  // ~21ms preliminary onset detection
    static constexpr int kFastMaxLag = 72;  // F0>=~165Hz only; never force bass octave
    static constexpr int kHop = 64;
    static constexpr int kMaxLag = 256;
    struct Result { float hz = 0.0f; float confidence = 0.0f; bool voiced = false; };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 8000.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
        decimation_ = std::max(1, std::min(16, static_cast<int>(std::lround(sampleRate_ / 12000.0))));
        detectorRate_ = sampleRate_ / decimation_;
        pole_ = std::exp(-2.0 * 3.14159265358979323846 * 1800.0 / sampleRate_);
        minLag_ = std::max(2, static_cast<int>(std::floor(detectorRate_ / 1000.0)));
        maxLag_ = std::min(kMaxLag, static_cast<int>(std::ceil(detectorRate_ / 55.0)));
        reset();
    }
    void reset() noexcept {
        ring_.fill(0.0f); frame_.fill(0.0f); diff_.fill(0.0f); cmndf_.fill(0.0f);
        ringPos_ = ready_ = hop_ = substep_ = 0;
        low1_ = low2_ = 0.0; last_ = {};
        lagEvaluations_ = 0;
    }
    // Accept samples from exactly one monophonic signal. Return true on a new estimate.
    bool push(float input) noexcept {
        if (!std::isfinite(input)) input = 0.0f;
        low1_ = pole_ * low1_ + (1.0 - pole_) * input;
        low2_ = pole_ * low2_ + (1.0 - pole_) * low1_;
        if (++substep_ < decimation_) return false;
        substep_ = 0;
        ring_[static_cast<std::size_t>(ringPos_)] = static_cast<float>(low2_);
        ringPos_ = (ringPos_ + 1) % kWindow;
        if (ready_ < kWindow) ++ready_;
        if (++hop_ < kHop) return false;
        hop_ = 0;
        if (ready_ < kFastWindow) return false;
        if (ready_ < kWindow) {
            // Produce useful upper-register F0 at ~21ms while the long
            // 43ms window is still filling; use the full low-register tracker
            // automatically once its history exists.
            calculate(kFastWindow,std::min(kFastMaxLag,maxLag_),165.0f);
            return true;
        }
        calculate(kWindow,maxLag_,55.0f);
        return true;
    }
    Result latest() const noexcept { return last_; }
    // Diagnostic: number of difference lags examined in the latest frame.
    // Allows a portable RT regression to verify short-circuit performance.
    int lastLagEvaluations() const noexcept { return lagEvaluations_; }
    double analysisWindowMs() const noexcept { return 1000.0 * kWindow / detectorRate_; }
private:
    void calculate(int window,int maxLag,float minHz) noexcept {
        const int start = (ringPos_ + kWindow - window) % kWindow;
        double energy = 0.0;
        for(int i=0;i<window;++i) {
            const float v=ring_[static_cast<std::size_t>((start+i)%kWindow)];
            frame_[static_cast<std::size_t>(i)]=v;
            energy+=static_cast<double>(v)*v;
        }
        last_ = {};
        lagEvaluations_ = 0;
        if (energy < static_cast<double>(window) * 0.000004) return; // -54dBFS RMS
        const int w = window - maxLag;
        double sum = 0.0;
        int best = -1;
        float chosen = 1.0f;
        for (int lag = 1; lag <= maxLag; ++lag) {
            ++lagEvaluations_;
            double d = 0.0;
            for (int i = 0; i < w; ++i) {
                double e = static_cast<double>(frame_[static_cast<std::size_t>(i)]) - frame_[static_cast<std::size_t>(i + lag)];
                d += e * e;
            }
            diff_[static_cast<std::size_t>(lag)] = static_cast<float>(d);
            sum += d;
            const float normalized = sum > 1.0e-16 ? static_cast<float>((d * lag) / sum) : 1.0f;
            cmndf_[static_cast<std::size_t>(lag)] = normalized;
            // We can terminate YIN after the first completed below-threshold
            // trough. The preceding implementation always calculated 218-256
            // lags even for a voiced 440Hz singer (lag ~27 @ 12k).
            // A completed trough requires the NEXT point to turn upward,
            // preserving the published first-dip + local-minimum rule.
            if (lag > minLag_) {
                const auto prior=static_cast<std::size_t>(lag-1);
                if (cmndf_[prior] < 0.17f && normalized >= cmndf_[prior]) {
                    best = lag-1;
                    chosen = cmndf_[prior];
                    break;
                }
            }
        }
        if (best < 0) {
            // A last-point minimum without a next sample still counts.
            // Keep a conservative fallback for silence/noisy borderline data.
            for (int lag = minLag_; lag <= maxLag; ++lag) {
                if (cmndf_[static_cast<std::size_t>(lag)] < 0.17f) {
                    best=lag;
                    chosen=cmndf_[static_cast<std::size_t>(lag)];
                    break;
                }
            }
        }
        if (best < 0 || chosen > 0.17f) return;
        float lag = static_cast<float>(best);
        if (best > minLag_ && best < maxLag) {
            const float a = cmndf_[static_cast<std::size_t>(best - 1)];
            const float b = cmndf_[static_cast<std::size_t>(best)];
            const float c = cmndf_[static_cast<std::size_t>(best + 1)];
            const float denominator = a - 2.0f*b + c;
            if (std::abs(denominator) > 1.e-8f)
                lag += std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f);
        }
        const float hz = static_cast<float>(detectorRate_ / lag);
        if (hz >= minHz && hz <= 1000.0f && std::isfinite(hz))
            last_ = {hz, 1.0f - chosen, true};
    }
    double sampleRate_ = 48000.0, detectorRate_ = 12000.0, pole_ = 0.0;
    double low1_ = 0.0, low2_ = 0.0;
    int decimation_ = 4, minLag_ = 12, maxLag_ = 160;
    int ringPos_ = 0, ready_ = 0, hop_ = 0, substep_ = 0;
    int lagEvaluations_ = 0;
    std::array<float, kWindow> ring_{}, frame_{};
    std::array<float, kMaxLag + 1> diff_{}, cmndf_{};
    Result last_{};
};

// Return the nearest MIDI note in the chosen key/scale.
// scale=0 chromatic, 1 major, 2 natural minor. root is a pitch class (C=0).
inline int nearestScaleMidi(float midi, int root, int scale) noexcept {
    if (!std::isfinite(midi)) return 69;
    root = (root % 12 + 12) % 12;
    if (scale == 0) return std::clamp(static_cast<int>(std::lround(midi)), 0, 127);
    constexpr bool major[12] = {true,false,true,false,true,true,false,true,false,true,false,true};
    constexpr bool minor[12] = {true,false,true,true,false,true,false,true,true,false,true,false};
    const bool* allowed = scale == 1 ? major : minor;
    int best = 69;
    float distance = 1e9f;
    for (int note = 0; note <= 127; ++note) {
        if (!allowed[(note - root + 120) % 12]) continue;
        const float d = std::abs(midi - note);
        if (d < distance) {distance = d; best = note;}
    }
    return best;
}
}} // namespace APEX::TuneLive
