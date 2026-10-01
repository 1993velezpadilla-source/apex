#pragma once

#include "ParametricEQTypes.h"

#include <array>
#include <cmath>
#include <complex>
#include <limits>

namespace APEX::ParametricEQ
{

constexpr double kPi = 3.141592653589793238462643383279502884;

struct BiquadCoefficients
{
    // H(z) = (b0 + b1 z^-1 + b2 z^-2)
    //        / (1 + a1 z^-1 + a2 z^-2)
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    static BiquadCoefficients identity() noexcept { return {}; }

    bool isFinite() const noexcept
    {
        return std::isfinite (b0) && std::isfinite (b1)
            && std::isfinite (b2) && std::isfinite (a1)
            && std::isfinite (a2);
    }

    bool hasStablePoles() const noexcept
    {
        if (! isFinite())
            return false;

        // Jury stability test for 1 + a1 z^-1 + a2 z^-2.  A small margin
        // rejects coefficients sitting numerically on the unit circle.
        constexpr double epsilon = 1.0e-12;
        return std::abs (a1) < 2.0 - epsilon
            && a2 < 1.0 - epsilon
            && a2 > std::abs (a1) - 1.0 + epsilon;
    }

    std::complex<double> response (double frequencyHz,
                                   double sampleRate) const noexcept
    {
        const auto rate = std::max (1.0, sampleRate);
        const auto frequency = std::clamp (frequencyHz, 0.0, rate * 0.5);
        const auto omega = 2.0 * kPi * frequency / rate;
        const auto z1 = std::polar (1.0, -omega);
        const auto z2 = z1 * z1;
        const auto numerator = b0 + b1 * z1 + b2 * z2;
        const auto denominator = 1.0 + a1 * z1 + a2 * z2;
        if (std::abs (denominator) < 1.0e-30)
            return {};
        return numerator / denominator;
    }
};

struct BiquadState
{
    // Transposed direct form II with double state.  Double state materially
    // improves low-frequency and high-Q robustness while preserving a float
    // host buffer and deterministic zero-latency operation.
    double s1 = 0.0;
    double s2 = 0.0;

    void reset() noexcept { s1 = s2 = 0.0; }

    float process (float input, const BiquadCoefficients& c) noexcept
    {
        const double x = std::isfinite (input) ? static_cast<double> (input) : 0.0;
        const double y = c.b0 * x + s1;
        const double nextS1 = c.b1 * x - c.a1 * y + s2;
        const double nextS2 = c.b2 * x - c.a2 * y;

        if (! std::isfinite (y) || ! std::isfinite (nextS1)
            || ! std::isfinite (nextS2))
        {
            reset();
            return static_cast<float> (x);
        }

        s1 = std::abs (nextS1) < 1.0e-30 ? 0.0 : nextS1;
        s2 = std::abs (nextS2) < 1.0e-30 ? 0.0 : nextS2;
        return static_cast<float> (y);
    }
};

struct CascadeCoefficients
{
    std::array<BiquadCoefficients, kMaxSectionsPerCascade> sections {};
    int count = 0;
    double outputGain = 1.0;
    bool usedAnalogMatchedDesign = false;
    bool fellBackToRealtimeDesign = false;

    static CascadeCoefficients identity() noexcept { return {}; }

    bool isIdentity() const noexcept
    {
        return count == 0 && outputGain == 1.0;
    }

    bool isValid() const noexcept
    {
        if (count < 0 || count > kMaxSectionsPerCascade
            || ! std::isfinite (outputGain))
            return false;
        for (int i = 0; i < count; ++i)
            if (! sections[static_cast<std::size_t> (i)].hasStablePoles())
                return false;
        return true;
    }

    std::complex<double> response (double frequencyHz,
                                   double sampleRate) const noexcept
    {
        std::complex<double> result { outputGain, 0.0 };
        for (int i = 0; i < count; ++i)
            result *= sections[static_cast<std::size_t> (i)].response (frequencyHz,
                                                                      sampleRate);
        return result;
    }
};

struct CascadeState
{
    std::array<BiquadState, kMaxSectionsPerCascade> sections {};

    void reset() noexcept
    {
        for (auto& section : sections)
            section.reset();
    }

    float process (float input, const CascadeCoefficients& coefficients) noexcept
    {
        auto value = input;
        for (int i = 0; i < coefficients.count; ++i)
            value = sections[static_cast<std::size_t> (i)].process (
                value, coefficients.sections[static_cast<std::size_t> (i)]);

        const auto output = static_cast<double> (value) * coefficients.outputGain;
        if (! std::isfinite (output))
        {
            reset();
            return std::isfinite (input) ? input : 0.0f;
        }
        return static_cast<float> (output);
    }
};

class FilterDesigner
{
public:
    static CascadeCoefficients designBand (const BandSettings& rawSettings,
                                           double sampleRate,
                                           DesignMode mode) noexcept
    {
        const auto settings = sanitise (rawSettings, sampleRate);
        if (! settings.enabled || settings.bypassed)
            return {};

        switch (settings.shape)
        {
            case FilterShape::Bell:
                if (std::abs (settings.gainDb) < 1.0e-12)
                    return {};
                return oneSection (designPeaking (settings.frequencyHz,
                                                  settings.q,
                                                  settings.gainDb,
                                                  sampleRate,
                                                  mode));
            case FilterShape::LowShelf:
                if (std::abs (settings.gainDb) < 1.0e-12)
                    return {};
                return oneSection (designRbj (FilterShape::LowShelf,
                                              settings.frequencyHz,
                                              settings.q,
                                              settings.gainDb,
                                              sampleRate));
            case FilterShape::HighShelf:
                if (std::abs (settings.gainDb) < 1.0e-12)
                    return {};
                return oneSection (designRbj (FilterShape::HighShelf,
                                              settings.frequencyHz,
                                              settings.q,
                                              settings.gainDb,
                                              sampleRate));
            case FilterShape::Notch:
                return oneSection (designRbj (FilterShape::Notch,
                                              settings.frequencyHz,
                                              settings.q,
                                              0.0,
                                              sampleRate));
            case FilterShape::BandPass:
                return oneSection (designBandPass (settings.frequencyHz,
                                                   settings.q,
                                                   sampleRate,
                                                   mode));
            case FilterShape::AllPass:
                return oneSection (designRbj (FilterShape::AllPass,
                                              settings.frequencyHz,
                                              settings.q,
                                              0.0,
                                              sampleRate));
            case FilterShape::Tilt:
                if (std::abs (settings.gainDb) < 1.0e-12)
                    return {};
                return designTilt (settings, sampleRate);
            case FilterShape::FlatTilt:
                if (std::abs (settings.gainDb) < 1.0e-12)
                    return {};
                return designFlatTilt (settings, sampleRate);
            case FilterShape::LowCut:
            case FilterShape::HighCut:
                break; // Cut orders are designed by designButterworthCut().
        }
        return {};
    }

    static CascadeCoefficients designButterworthCut (bool highPass,
                                                      int order,
                                                      double frequencyHz,
                                                      double sampleRate,
                                                      DesignMode mode) noexcept
    {
        CascadeCoefficients result;
        order = std::clamp (order, 0, kMaxCutOrder);
        if (order == 0)
            return result;

        const auto frequency = std::clamp (frequencyHz,
                                           kMinimumFrequencyHz,
                                           maximumUsableFrequency (sampleRate));
        const auto shape = highPass ? FilterShape::LowCut : FilterShape::HighCut;

        if ((order & 1) != 0)
            result.sections[static_cast<std::size_t> (result.count++)]
                = designFirstOrderCut (highPass, frequency, sampleRate);

        const int pairCount = order / 2;
        for (int pair = 0; pair < pairCount; ++pair)
        {
            const auto angle = (2.0 * pair + 1.0) * kPi / (2.0 * order);
            const auto q = 1.0 / (2.0 * std::sin (angle));
            DesignResult coefficient;
            if (mode == DesignMode::AnalogMatched)
                coefficient = designMatchedCut (highPass, frequency, q, sampleRate);
            else
                coefficient = { designRbj (shape, frequency, q, 0.0, sampleRate),
                                false, false };

            if (! coefficient.coefficients.hasStablePoles())
                coefficient = { designRbj (shape, frequency, q, 0.0, sampleRate),
                                false, true };

            result.sections[static_cast<std::size_t> (result.count++)]
                = coefficient.coefficients;
            result.usedAnalogMatchedDesign |= coefficient.usedAnalogMatched;
            result.fellBackToRealtimeDesign |= coefficient.fellBack;
        }

        if (! result.isValid())
            return {};
        return result;
    }

    static BiquadCoefficients designRbj (FilterShape shape,
                                         double frequencyHz,
                                         double q,
                                         double gainDb,
                                         double sampleRate) noexcept
    {
        const auto rate = validSampleRate (sampleRate);
        const auto frequency = std::clamp (frequencyHz,
                                           kMinimumFrequencyHz,
                                           maximumUsableFrequency (rate));
        q = std::clamp (q, kMinimumQ, kMaximumQ);
        gainDb = std::clamp (gainDb, kMinimumGainDb, kMaximumGainDb);

        const auto omega = 2.0 * kPi * frequency / rate;
        const auto cosine = std::cos (omega);
        const auto sine = std::sin (omega);
        const auto alpha = sine / (2.0 * q);
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto sqrtA = std::sqrt (A);

        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a0 = 1.0, a1 = 0.0, a2 = 0.0;

        switch (shape)
        {
            case FilterShape::LowCut: // High-pass response.
                b0 = (1.0 + cosine) * 0.5;
                b1 = -(1.0 + cosine);
                b2 = b0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosine;
                a2 = 1.0 - alpha;
                break;
            case FilterShape::HighCut: // Low-pass response.
                b0 = (1.0 - cosine) * 0.5;
                b1 = 1.0 - cosine;
                b2 = b0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosine;
                a2 = 1.0 - alpha;
                break;
            case FilterShape::BandPass:
                b0 = alpha;
                b1 = 0.0;
                b2 = -alpha;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosine;
                a2 = 1.0 - alpha;
                break;
            case FilterShape::Notch:
                b0 = 1.0;
                b1 = -2.0 * cosine;
                b2 = 1.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosine;
                a2 = 1.0 - alpha;
                break;
            case FilterShape::AllPass:
                b0 = 1.0 - alpha;
                b1 = -2.0 * cosine;
                b2 = 1.0 + alpha;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosine;
                a2 = 1.0 - alpha;
                break;
            case FilterShape::Bell:
                b0 = 1.0 + alpha * A;
                b1 = -2.0 * cosine;
                b2 = 1.0 - alpha * A;
                a0 = 1.0 + alpha / A;
                a1 = -2.0 * cosine;
                a2 = 1.0 - alpha / A;
                break;
            case FilterShape::LowShelf:
            {
                const auto twoRootAAlpha = 2.0 * sqrtA * alpha;
                b0 = A * ((A + 1.0) - (A - 1.0) * cosine + twoRootAAlpha);
                b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosine);
                b2 = A * ((A + 1.0) - (A - 1.0) * cosine - twoRootAAlpha);
                a0 = (A + 1.0) + (A - 1.0) * cosine + twoRootAAlpha;
                a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosine);
                a2 = (A + 1.0) + (A - 1.0) * cosine - twoRootAAlpha;
                break;
            }
            case FilterShape::HighShelf:
            {
                const auto twoRootAAlpha = 2.0 * sqrtA * alpha;
                b0 = A * ((A + 1.0) + (A - 1.0) * cosine + twoRootAAlpha);
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosine);
                b2 = A * ((A + 1.0) + (A - 1.0) * cosine - twoRootAAlpha);
                a0 = (A + 1.0) - (A - 1.0) * cosine + twoRootAAlpha;
                a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosine);
                a2 = (A + 1.0) - (A - 1.0) * cosine - twoRootAAlpha;
                break;
            }
            case FilterShape::Tilt:
            case FilterShape::FlatTilt:
                return {};
        }

        return normaliseOrIdentity (b0, b1, b2, a0, a1, a2);
    }

private:
    struct DesignResult
    {
        BiquadCoefficients coefficients {};
        bool usedAnalogMatched = false;
        bool fellBack = false;
    };

    static double validSampleRate (double sampleRate) noexcept
    {
        return std::isfinite (sampleRate) && sampleRate > 1.0
             ? sampleRate : 44100.0;
    }

    static CascadeCoefficients oneSection (DesignResult design) noexcept
    {
        CascadeCoefficients result;
        if (! design.coefficients.hasStablePoles())
            return result;
        result.sections[0] = design.coefficients;
        result.count = 1;
        result.usedAnalogMatchedDesign = design.usedAnalogMatched;
        result.fellBackToRealtimeDesign = design.fellBack;
        return result;
    }

    static CascadeCoefficients oneSection (BiquadCoefficients coefficient) noexcept
    {
        return oneSection ({ coefficient, false, false });
    }

    static BiquadCoefficients normaliseOrIdentity (double b0, double b1,
                                                    double b2, double a0,
                                                    double a1, double a2) noexcept
    {
        if (! std::isfinite (a0) || std::abs (a0) < 1.0e-30)
            return {};
        BiquadCoefficients result { b0 / a0, b1 / a0, b2 / a0,
                                    a1 / a0, a2 / a0 };
        return result.hasStablePoles() ? result : BiquadCoefficients::identity();
    }

    static BiquadCoefficients designFirstOrderCut (bool highPass,
                                                    double frequencyHz,
                                                    double sampleRate) noexcept
    {
        const auto rate = validSampleRate (sampleRate);
        const auto frequency = std::clamp (frequencyHz,
                                           kMinimumFrequencyHz,
                                           maximumUsableFrequency (rate));
        const auto K = std::tan (kPi * frequency / rate);
        const auto normaliser = 1.0 / (1.0 + K);
        if (highPass)
            return { normaliser, -normaliser, 0.0,
                     (K - 1.0) * normaliser, 0.0 };
        return { K * normaliser, K * normaliser, 0.0,
                 (K - 1.0) * normaliser, 0.0 };
    }

    static bool impulseInvariantPoles (double frequencyHz, double q,
                                       double sampleRate,
                                       double& a1, double& a2) noexcept
    {
        const auto rate = validSampleRate (sampleRate);
        const auto omega = 2.0 * kPi * std::clamp (
            frequencyHz, kMinimumFrequencyHz, maximumUsableFrequency (rate)) / rate;
        q = std::max (1.0e-6, q);
        const auto decay = std::exp (-q * omega);
        if (q <= 1.0)
            a1 = -2.0 * decay * std::cos (std::sqrt (1.0 - q * q) * omega);
        else
            a1 = -2.0 * decay * std::cosh (std::sqrt (q * q - 1.0) * omega);
        a2 = decay * decay;
        BiquadCoefficients test { 1.0, 0.0, 0.0, a1, a2 };
        return test.hasStablePoles();
    }

    static DesignResult designMatchedCut (bool highPass,
                                          double frequencyHz,
                                          double qValue,
                                          double sampleRate) noexcept
    {
        const auto rate = validSampleRate (sampleRate);
        const auto frequency = std::clamp (frequencyHz,
                                           kMinimumFrequencyHz,
                                           maximumUsableFrequency (rate));
        const auto Q = std::clamp (qValue, kMinimumQ, kMaximumQ);
        double a1 = 0.0, a2 = 0.0;
        if (! impulseInvariantPoles (frequency, 1.0 / (2.0 * Q), rate, a1, a2))
            return { designRbj (highPass ? FilterShape::LowCut
                                         : FilterShape::HighCut,
                                frequency, Q, 0.0, rate), false, true };

        const auto omega = 2.0 * kPi * frequency / rate;
        const auto phi1 = std::pow (std::sin (omega * 0.5), 2.0);
        const auto phi0 = 1.0 - phi1;
        const auto phi2 = 4.0 * phi0 * phi1;
        const auto A0 = std::pow (1.0 + a1 + a2, 2.0);
        const auto A1 = std::pow (1.0 - a1 + a2, 2.0);
        const auto A2 = -4.0 * a2;
        const auto denominatorAtCut = A0 * phi0 + A1 * phi1 + A2 * phi2;

        if (phi1 < 1.0e-12 || denominatorAtCut <= 0.0)
            return { designRbj (highPass ? FilterShape::LowCut
                                         : FilterShape::HighCut,
                                frequency, Q, 0.0, rate), false, true };

        BiquadCoefficients result;
        if (highPass)
        {
            const auto b0 = std::sqrt (denominatorAtCut) * Q / (4.0 * phi1);
            result = { b0, -2.0 * b0, b0, a1, a2 };
        }
        else
        {
            const auto B0 = A0;
            const auto R1 = denominatorAtCut * Q * Q;
            const auto B1 = (R1 - B0 * phi0) / phi1;
            if (B0 < 0.0 || B1 < 0.0)
                return { designRbj (FilterShape::HighCut, frequency, Q, 0.0, rate),
                         false, true };
            const auto rootB0 = std::sqrt (B0);
            const auto rootB1 = std::sqrt (B1);
            const auto b0 = 0.5 * (rootB0 + rootB1);
            const auto b1 = rootB0 - b0;
            result = { b0, b1, 0.0, a1, a2 };
        }

        if (! result.hasStablePoles() || ! result.isFinite())
            return { designRbj (highPass ? FilterShape::LowCut
                                         : FilterShape::HighCut,
                                frequency, Q, 0.0, rate), false, true };
        return { result, true, false };
    }

    static DesignResult designPeaking (double frequencyHz, double qValue,
                                       double gainDb, double sampleRate,
                                       DesignMode mode) noexcept
    {
        const auto realtime = designRbj (FilterShape::Bell, frequencyHz,
                                         qValue, gainDb, sampleRate);
        if (mode != DesignMode::AnalogMatched || std::abs (gainDb) < 1.0e-9)
            return { realtime, false, false };

        const auto rate = validSampleRate (sampleRate);
        const auto frequency = std::clamp (frequencyHz,
                                           kMinimumFrequencyHz,
                                           maximumUsableFrequency (rate));
        const auto Q = std::clamp (qValue, kMinimumQ, kMaximumQ);
        const auto G = std::pow (10.0, std::clamp (gainDb,
                                                  kMinimumGainDb,
                                                  kMaximumGainDb) / 20.0);
        const auto rootG = std::sqrt (G);

        double a1 = 0.0, a2 = 0.0;
        if (! impulseInvariantPoles (frequency,
                                     1.0 / (2.0 * rootG * Q),
                                     rate, a1, a2))
            return { realtime, false, true };

        const auto omega = 2.0 * kPi * frequency / rate;
        const auto phi1 = std::pow (std::sin (omega * 0.5), 2.0);
        if (phi1 < 1.0e-8) // Avoid cancellation in the phi1^2 denominator.
            return { realtime, false, true };
        const auto phi0 = 1.0 - phi1;
        const auto phi2 = 4.0 * phi0 * phi1;
        const auto A0 = std::pow (1.0 + a1 + a2, 2.0);
        const auto A1 = std::pow (1.0 - a1 + a2, 2.0);
        const auto A2 = -4.0 * a2;
        const auto denominator = A0 * phi0 + A1 * phi1 + A2 * phi2;
        const auto derivativeTerm = -A0 + A1 + 4.0 * (phi0 - phi1) * A2;
        const auto B0 = A0;
        const auto R1 = denominator * G * G;
        const auto R2 = derivativeTerm * G * G;
        const auto B2 = (R1 - R2 * phi1 - B0) / (4.0 * phi1 * phi1);
        const auto B1 = R2 + B0 + 4.0 * (phi1 - phi0) * B2;

        if (B0 < -1.0e-10 || B1 < -1.0e-10)
            return { realtime, false, true };
        const auto rootB0 = std::sqrt (std::max (0.0, B0));
        const auto rootB1 = std::sqrt (std::max (0.0, B1));
        const auto W = 0.5 * (rootB0 + rootB1);
        const auto discriminant = W * W + B2;
        if (discriminant < -1.0e-10)
            return { realtime, false, true };
        const auto b0 = 0.5 * (W + std::sqrt (std::max (0.0, discriminant)));
        if (std::abs (b0) < 1.0e-20)
            return { realtime, false, true };
        const auto b1 = 0.5 * (rootB0 - rootB1);
        const auto b2 = -B2 / (4.0 * b0);
        BiquadCoefficients result { b0, b1, b2, a1, a2 };
        if (! result.hasStablePoles() || ! result.isFinite())
            return { realtime, false, true };
        return { result, true, false };
    }

    static DesignResult designBandPass (double frequencyHz, double qValue,
                                        double sampleRate,
                                        DesignMode mode) noexcept
    {
        const auto realtime = designRbj (FilterShape::BandPass, frequencyHz,
                                         qValue, 0.0, sampleRate);
        if (mode != DesignMode::AnalogMatched)
            return { realtime, false, false };

        const auto rate = validSampleRate (sampleRate);
        const auto frequency = std::clamp (frequencyHz,
                                           kMinimumFrequencyHz,
                                           maximumUsableFrequency (rate));
        const auto Q = std::clamp (qValue, kMinimumQ, kMaximumQ);
        double a1 = 0.0, a2 = 0.0;
        if (! impulseInvariantPoles (frequency, 1.0 / (2.0 * Q), rate, a1, a2))
            return { realtime, false, true };

        const auto omega = 2.0 * kPi * frequency / rate;
        const auto phi1 = std::pow (std::sin (omega * 0.5), 2.0);
        if (phi1 < 1.0e-8)
            return { realtime, false, true };
        const auto phi0 = 1.0 - phi1;
        const auto phi2 = 4.0 * phi0 * phi1;
        const auto A0 = std::pow (1.0 + a1 + a2, 2.0);
        const auto A1 = std::pow (1.0 - a1 + a2, 2.0);
        const auto A2 = -4.0 * a2;
        const auto R1 = A0 * phi0 + A1 * phi1 + A2 * phi2;
        const auto R2 = -A0 + A1 + 4.0 * (phi0 - phi1) * A2;
        const auto B2 = (R1 - R2 * phi1) / (4.0 * phi1 * phi1);
        const auto B1 = R2 + 4.0 * (phi1 - phi0) * B2;
        if (B1 < -1.0e-10 || B2 + 0.25 * B1 < -1.0e-10)
            return { realtime, false, true };
        const auto b1 = -0.5 * std::sqrt (std::max (0.0, B1));
        const auto b0 = 0.5 * (std::sqrt (std::max (0.0, B2 + b1 * b1)) - b1);
        const auto b2 = -b0 - b1;
        BiquadCoefficients result { b0, b1, b2, a1, a2 };
        if (! result.hasStablePoles() || ! result.isFinite())
            return { realtime, false, true };
        return { result, true, false };
    }

    static CascadeCoefficients designTilt (const BandSettings& settings,
                                           double sampleRate) noexcept
    {
        CascadeCoefficients result;
        // APEX Tilt: linked shelves with equal and opposite half gains.  It
        // keeps the pivot near unity while allowing Q to shape the knee.
        result.sections[0] = designRbj (FilterShape::LowShelf,
                                        settings.frequencyHz,
                                        settings.q,
                                        -0.5 * settings.gainDb,
                                        sampleRate);
        result.sections[1] = designRbj (FilterShape::HighShelf,
                                        settings.frequencyHz,
                                        settings.q,
                                        0.5 * settings.gainDb,
                                        sampleRate);
        result.count = 2;
        return result.isValid() ? result : CascadeCoefficients {};
    }

    static CascadeCoefficients designFlatTilt (const BandSettings& settings,
                                               double sampleRate) noexcept
    {
        CascadeCoefficients result;
        // APEX Flat Tilt: one minimum-phase shelf plus an exact scalar centres
        // the endpoint gains at -gain/2 and +gain/2.  Unlike Tilt, it has one
        // broad monotonic transition and no overlapping shelf knees.
        result.sections[0] = designRbj (FilterShape::LowShelf,
                                        settings.frequencyHz,
                                        settings.q,
                                        -settings.gainDb,
                                        sampleRate);
        result.count = 1;
        result.outputGain = std::pow (10.0, settings.gainDb / 40.0);
        return result.isValid() ? result : CascadeCoefficients {};
    }
};

} // namespace APEX::ParametricEQ
