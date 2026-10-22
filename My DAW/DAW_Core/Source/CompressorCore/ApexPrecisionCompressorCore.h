#pragma once

// APEX Precision C3 - original clean-room dynamics DSP, not FabFilter source,
// not a bit-identical/sonically calibrated Pro-C 3 model.
// C++17 portable; all audio-callback work is allocation-free and lock-free.
// The 14 mode names describe musical goals; precise proprietary transfer
// curves, TTM multiband and hardware characters require independent validation.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace APEX::PrecisionC3
{
struct Parameters
{
    double thresholdDb = -18.0;
    double ratio = 4.0;
    double kneeDb = 12.0;
    double rangeDb = 60.0;
    double attackMs = 10.0;
    double releaseMs = 120.0;
    double holdMs = 0.0;
    double inputDb = 0.0;
    double wetDb = 0.0;
    double dryPercent = 0.0;
    double outputDb = 0.0;
    double sidechainHighpassHz = 0.0;  // 0 disables
    double sidechainLowpassHz = 20000.0;
    double stereoLinkPercent = 100.0;
    double drive = 0.0; // 0..100%
    int style = 0;      // see kStyleNames below
    int character = 0;  // Off / Tube / Diode / Bright
    bool autoThreshold = false;
    bool autoRelease = false;
    bool autoGain = false;
    bool bypass = false;

    static Parameters sanitise (Parameters p) noexcept
    {
        auto bounded = [] (double value, double lo, double hi, double fallback) noexcept
        {
            return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
        };
        p.thresholdDb = bounded(p.thresholdDb, -60.0, 0.0, -18.0);
        p.ratio = bounded(p.ratio, 1.0, 40.0, 4.0);
        p.kneeDb = bounded(p.kneeDb, 0.0, 72.0, 12.0);
        p.rangeDb = bounded(p.rangeDb, 0.0, 60.0, 60.0);
        p.attackMs = bounded(p.attackMs, 0.005, 250.0, 10.0);
        p.releaseMs = bounded(p.releaseMs, 5.0, 3000.0, 120.0);
        p.holdMs = bounded(p.holdMs, 0.0, 500.0, 0.0);
        p.inputDb = bounded(p.inputDb, -24.0, 24.0, 0.0);
        p.wetDb = bounded(p.wetDb, -24.0, 24.0, 0.0);
        p.dryPercent = bounded(p.dryPercent, 0.0, 100.0, 0.0);
        p.outputDb = bounded(p.outputDb, -24.0, 24.0, 0.0);
        p.sidechainHighpassHz = bounded(p.sidechainHighpassHz, 0.0, 2000.0, 0.0);
        p.sidechainLowpassHz = bounded(p.sidechainLowpassHz, 100.0, 20000.0, 20000.0);
        p.stereoLinkPercent = bounded(p.stereoLinkPercent, 0.0, 100.0, 100.0);
        p.drive = bounded(p.drive, 0.0, 100.0, 0.0);
        p.style = std::clamp(p.style, 0, 13);
        p.character = std::clamp(p.character, 0, 3);
        return p;
    }
};

static constexpr std::array<const char*, 14> kStyleNames {{
    "Clean", "Versatile", "Smooth", "Punch", "Upward", "TTM",
    "Op-El", "Vari-Mu", "Classic", "Opto", "Vocal", "Mastering",
    "Bus", "Pumping"
}};

class Core final
{
public:
    void prepare (double rate) noexcept
    {
        sampleRate_ = std::isfinite(rate) && rate >= 8000.0 ? rate : 48000.0;
        smoothingCoeff_ = coefficient(0.012);
        thresholdTrackingCoeff_ = coefficient(1.5);
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : channels_) s = {};
        for (auto& h : holdRemaining_) h = 0;
        smoothedInput_ = dbGain(parameters_.inputDb);
        smoothedWet_ = dbGain(parameters_.wetDb);
        smoothedOutput_ = dbGain(parameters_.outputDb);
        smoothedDry_ = parameters_.dryPercent * 0.01;
        smoothedBypass_ = parameters_.bypass ? 0.0 : 1.0;
        smoothedThreshold_ = parameters_.thresholdDb;
        trackedLevelDb_ = -18.0;
        gainReductionDb_ = 0.0;
        inputPeakDb_ = outputPeakDb_ = -120.0;
        initialised_ = false;
    }

    void setParameters (const Parameters& value) noexcept
    {
        parameters_ = Parameters::sanitise(value);
        const auto nyquist = 0.499 * sampleRate_;
        const auto hpf = std::min(parameters_.sidechainHighpassHz, nyquist);
        const auto lpf = std::min(parameters_.sidechainLowpassHz, nyquist);
        hpCoeff_ = hpf <= 0.0 ? 0.0 : coefficient(1.0 / (6.283185307179586 * hpf));
        lpCoeff_ = lpf >= nyquist ? 0.0 : coefficient(1.0 / (6.283185307179586 * lpf));
        if (!initialised_)
        {
            smoothedInput_ = dbGain(parameters_.inputDb);
            smoothedWet_ = dbGain(parameters_.wetDb);
            smoothedOutput_ = dbGain(parameters_.outputDb);
            smoothedDry_ = parameters_.dryPercent * 0.01;
            smoothedBypass_ = parameters_.bypass ? 0.0 : 1.0;
            smoothedThreshold_ = parameters_.thresholdDb;
        }
    }

    Parameters getParameters() const noexcept { return parameters_; }
    double getGainReductionDb() const noexcept { return gainReductionDb_; }
    double getInputPeakDb() const noexcept { return inputPeakDb_; }
    double getOutputPeakDb() const noexcept { return outputPeakDb_; }
    static constexpr int latencySamples() noexcept { return 0; }

    // Separate SC pointers are optional and must be valid for the whole block.
    // The native wrapper currently uses the internal path. This port is ready
    // for externally routed APEX sidechain when its bus contract is validated.
    void process (float* left, float* right, int samples,
                  const float* sideLeft = nullptr,
                  const float* sideRight = nullptr) noexcept
    {
        if (left == nullptr || samples <= 0) return;
        initialised_ = true;
        const bool stereo = right != nullptr && right != left;
        const double attackScale = styleAttackScale(parameters_.style);
        const double releaseScale = styleReleaseScale(parameters_.style);
        const double attackCoeff = coefficient(parameters_.attackMs * 0.001 * attackScale);
        const double baseRelease = parameters_.releaseMs * 0.001 * releaseScale;
        const auto inputTarget = dbGain(parameters_.inputDb);
        const auto wetTarget = dbGain(parameters_.wetDb);
        const auto outputTarget = dbGain(parameters_.outputDb);
        const auto dryTarget = parameters_.dryPercent * 0.01;
        const auto activeTarget = parameters_.bypass ? 0.0 : 1.0;
        const double link = parameters_.stereoLinkPercent * 0.01;
        double inputPeak = 0.0, outputPeak = 0.0;

        for (int i = 0; i < samples; ++i)
        {
            smoothedInput_ = smooth(smoothedInput_, inputTarget);
            smoothedWet_ = smooth(smoothedWet_, wetTarget);
            smoothedDry_ = smooth(smoothedDry_, dryTarget);
            smoothedOutput_ = smooth(smoothedOutput_, outputTarget);
            smoothedBypass_ = smooth(smoothedBypass_, activeTarget);
            smoothedThreshold_ = smooth(smoothedThreshold_, parameters_.thresholdDb);

            const double dryL = safe(left[i]);
            const double dryR = stereo ? safe(right[i]) : dryL;
            inputPeak = std::max(inputPeak, std::max(std::abs(dryL), std::abs(dryR)));
            const double x[2] = {dryL * smoothedInput_, dryR * smoothedInput_};
            const double trigger[2] = {
                sideLeft ? safe(sideLeft[i]) : x[0],
                sideRight ? safe(sideRight[i]) : (sideLeft ? safe(sideLeft[i]) : x[1])
            };
            double detectorDb[2] {};
            for (int c = 0; c < (stereo ? 2 : 1); ++c)
            {
                auto& state = channels_[static_cast<std::size_t>(c)];
                state.hpLow = hpCoeff_ * state.hpLow + (1.0 - hpCoeff_) * trigger[c];
                double filtered = parameters_.sidechainHighpassHz > 0.0
                                    ? trigger[c] - state.hpLow : trigger[c];
                state.lpLow = lpCoeff_ * state.lpLow + (1.0 - lpCoeff_) * filtered;
                if (parameters_.sidechainLowpassHz < 19999.0)
                    filtered = state.lpLow;
                // Smooth / opto-like modes use an RMS-ish detector.
                const bool rms = parameters_.style == 2 || parameters_.style == 6
                              || parameters_.style == 9 || parameters_.style == 11;
                const double energy = filtered * filtered;
                state.rms = coefficient(0.010) * state.rms
                          + (1.0 - coefficient(0.010)) * energy;
                const double level = rms ? std::sqrt(std::max(0.0, state.rms))
                                         : std::abs(filtered);
                const double feedback = (parameters_.style == 7 || parameters_.style == 8)
                    ? state.lastGain : 1.0;
                detectorDb[c] = amplitudeDb(level * feedback);
            }
            if (!stereo) detectorDb[1] = detectorDb[0];
            const auto louder = std::max(detectorDb[0], detectorDb[1]);
            if (louder > -70.0)
                trackedLevelDb_ = thresholdTrackingCoeff_ * trackedLevelDb_
                                + (1.0 - thresholdTrackingCoeff_) * louder;
            const double correction = parameters_.autoThreshold
                ? std::clamp(trackedLevelDb_ + 18.0, -18.0, 18.0) : 0.0;
            const double threshold = std::clamp(smoothedThreshold_ + correction, -72.0, 18.0);
            double gr[2] {};
            for (int c = 0; c < (stereo ? 2 : 1); ++c)
            {
                auto& state = channels_[static_cast<std::size_t>(c)];
                const auto linkedDb = detectorDb[c] + (louder - detectorDb[c]) * link;
                const double targetDb = desiredReduction(linkedDb, threshold);
                if (targetDb > state.reductionDb)
                {
                    state.reductionDb = attackCoeff * state.reductionDb
                                      + (1.0 - attackCoeff) * targetDb;
                    holdRemaining_[static_cast<std::size_t>(c)] = static_cast<int>(
                        sampleRate_ * parameters_.holdMs * 0.001);
                }
                else if (holdRemaining_[static_cast<std::size_t>(c)] > 0)
                    --holdRemaining_[static_cast<std::size_t>(c)];
                else
                {
                    // Longer gain reductions recover more slowly; stable at
                    // all host block sizes (the state advances per sample).
                    const double adaptive = parameters_.autoRelease
                        ? (1.0 + std::clamp(state.reductionDb / 12.0, 0.0, 3.0)) : 1.0;
                    const double release = coefficient(baseRelease * adaptive);
                    state.reductionDb = release * state.reductionDb
                                      + (1.0 - release) * targetDb;
                }
                if (state.reductionDb < 1.0e-9) state.reductionDb = 0.0;
                gr[c] = std::clamp(state.reductionDb, 0.0, parameters_.rangeDb);
                state.lastGain = dbGain(-gr[c]);
            }
            if (!stereo) gr[1] = gr[0];
            gainReductionDb_ = std::max(gr[0], gr[1]);
            const double makeup = parameters_.autoGain
                ? std::clamp((-threshold) * (1.0 - 1.0 / parameters_.ratio) * 0.15,
                             0.0, 12.0) : 0.0;
            const double wetGain = smoothedWet_ * dbGain(makeup);
            double yL = colour(x[0] * dbGain(-gr[0]), parameters_.character,
                               parameters_.drive) * wetGain
                      + x[0] * smoothedDry_;
            double yR = colour(x[1] * dbGain(-gr[1]), parameters_.character,
                               parameters_.drive) * wetGain
                      + x[1] * smoothedDry_;
            yL *= smoothedOutput_;
            yR *= smoothedOutput_;
            // Bypass recovers the exact original input, with no discontinuous
            // replacement of a playing waveform on button press.
            yL = dryL + (yL - dryL) * smoothedBypass_;
            yR = dryR + (yR - dryR) * smoothedBypass_;
            if (!std::isfinite(yL)) yL = 0.0;
            if (!std::isfinite(yR)) yR = 0.0;
            left[i] = static_cast<float>(std::clamp(yL, -32.0, 32.0));
            if (stereo)
                right[i] = static_cast<float>(std::clamp(yR, -32.0, 32.0));
            outputPeak = std::max(outputPeak, std::max(std::abs(yL), std::abs(yR)));
        }
        inputPeakDb_ = amplitudeDb(inputPeak);
        outputPeakDb_ = amplitudeDb(outputPeak);
    }

private:
    struct Channel
    {
        double hpLow = 0.0, lpLow = 0.0, rms = 0.0;
        double reductionDb = 0.0, lastGain = 1.0;
    };

    static double safe (float value) noexcept
    {
        return std::isfinite(value) ? std::clamp(static_cast<double>(value), -32.0, 32.0) : 0.0;
    }
    static double dbGain (double db) noexcept { return std::exp(db * 0.11512925464970229); }
    static double amplitudeDb (double amplitude) noexcept
    {
        return 20.0 * std::log10(std::max(1.0e-9, amplitude));
    }
    double coefficient (double seconds) const noexcept
    {
        return std::exp(-1.0 / (sampleRate_ * std::max(0.000001, seconds)));
    }
    double smooth (double previous, double target) const noexcept
    {
        return smoothingCoeff_ * previous + (1.0 - smoothingCoeff_) * target;
    }
    static double styleAttackScale (int style) noexcept
    {
        constexpr double factors[] = {1.0,0.9,1.8,0.65,0.75,0.8,2.8,1.1,
                                       1.3,3.5,0.85,0.55,1.4,0.65};
        return factors[style];
    }
    static double styleReleaseScale (int style) noexcept
    {
        constexpr double factors[] = {1.0,0.85,1.7,0.75,0.8,1.2,2.0,1.9,
                                       2.2,3.0,0.9,0.8,1.65,2.7};
        return factors[style];
    }
    double desiredReduction (double levelDb, double threshold) const noexcept
    {
        double ratio = parameters_.ratio;
        if (parameters_.style == 10) ratio = std::max(ratio, 3.0); // vocal assist
        if (parameters_.style == 7) ratio = 1.0 + (ratio - 1.0)
            * (0.5 + 0.5 * std::tanh((levelDb - threshold) * 0.08));
        const double amount = 1.0 - 1.0 / ratio;
        double knee = parameters_.kneeDb;
        if (parameters_.style == 9 || parameters_.style == 6)
            knee = std::max(knee, 24.0);
        const double above = levelDb - threshold;
        auto transfer = [amount, knee](double over) noexcept
        {
            if (knee <= 0.0) return amount * std::max(0.0, over);
            if (over <= -knee * 0.5) return 0.0;
            if (over >= knee * 0.5) return amount * over;
            const double x = over + knee * 0.5;
            return amount * x * x / (2.0 * knee);
        };
        if (parameters_.style == 4) // upward: boost quiet material, don't attenuate loud
            return -std::min(parameters_.rangeDb, transfer(-above));
        if (parameters_.style == 5) // broadband upward/downward hybrid; NOT FabFilter multiband TTM
            return std::clamp(transfer(above) - 0.35 * transfer(-above),
                              -parameters_.rangeDb, parameters_.rangeDb);
        return std::min(parameters_.rangeDb, transfer(above));
    }
    static double colour (double input, int mode, double drive) noexcept
    {
        if (mode == 0 || drive <= 0.0) return input;
        const double amount = drive * 0.01;
        const double x = input * (1.0 + 2.0 * amount);
        if (mode == 1) // symmetric soft saturation
            return (1.0 - amount) * input + amount * std::tanh(x) / (1.0 + amount);
        if (mode == 2) // asymmetric harmonic shape, zero crossing preserved
            return (1.0 - amount) * input + amount * (x / (1.0 + std::abs(x)));
        // 'Bright': restrained soft clip with increasing high-level edge.
        return (1.0 - amount) * input + amount * (x - (x*x*x) / (3.0 + x*x));
    }

    Parameters parameters_ {};
    double sampleRate_ = 48000.0, smoothingCoeff_ = 0.0;
    double thresholdTrackingCoeff_ = 0.0, trackedLevelDb_ = -18.0;
    double hpCoeff_ = 0.0, lpCoeff_ = 0.0;
    std::array<Channel,2> channels_ {};
    std::array<int,2> holdRemaining_ {};
    double smoothedInput_ = 1.0, smoothedWet_ = 1.0, smoothedOutput_ = 1.0;
    double smoothedDry_ = 0.0, smoothedBypass_ = 1.0, smoothedThreshold_ = -18.0;
    double gainReductionDb_ = 0.0, inputPeakDb_ = -120.0, outputPeakDb_ = -120.0;
    bool initialised_ = false;
};
} // namespace APEX::PrecisionC3
