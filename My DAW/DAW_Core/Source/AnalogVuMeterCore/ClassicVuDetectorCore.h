#pragma once
#include <cmath>

namespace DAW {

/** Audio-clock VU movement: full-wave rectification followed by a damped
    second-order movement (99% rise in 300 ms, approximately 1.25% overshoot).
    Lobdell & Allen, JASA 121 (2007), Appendix A: zeta=.81272, wn=13.512.
    Linear rectification, sine-calibrated: a sine of peak A settles at A.
    Unlike block RMS plus a UI timer, every sample contributes exactly once.
    Double state is necessary for the very low pole frequency at audio rates.
    prepare/reset require the audio thread or a quiescent engine. */
class ClassicVuDetectorCore
{
public:
    ClassicVuDetectorCore() noexcept { prepare(44100.0); }

    void prepare(double sampleRate) noexcept
    {
        if (!std::isfinite(sampleRate) || sampleRate < 1.0) sampleRate = 44100.0;
        constexpr double wn = 13.512, damping = 0.81272;
        const double k = 2.0 * sampleRate;
        const double denominator = k * k + 2.0 * damping * wn * k + wn * wn;
        b0_ = wn * wn / denominator;
        b1_ = 2.0 * b0_;
        b2_ = b0_;
        a1_ = (2.0 * wn * wn - 2.0 * k * k) / denominator;
        a2_ = (k * k - 2.0 * damping * wn * k + wn * wn) / denominator;
        reset();
    }

    float process(float sample) noexcept
    {
        constexpr double sineCalibration = 1.57079632679489661923;
        const double input = std::isfinite(sample) ? std::abs((double) sample) * sineCalibration : 0.0;
        const double output = b0_ * input + z1_;
        z1_ = b1_ * input - a1_ * output + z2_;
        z2_ = b2_ * input - a2_ * output;
        // The mechanical needle rests at zero after the small release overshoot.
        return (float) (output > 0.0 ? output : 0.0);
    }

    void reset() noexcept { z1_ = z2_ = 0.0; }

private:
    double b0_ = 0.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

} // namespace DAW
