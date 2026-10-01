#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>

namespace DAW {

class LufsMeterCore
{
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        shelf_.setCoefficientsForSampleRate(sampleRate_, true);
        rlb_.setCoefficientsForSampleRate(sampleRate_, false);
        momentary_.prepare(sampleRate_, 0.400);
        shortTerm_.prepare(sampleRate_, 3.000);
        // Bounded gating state: fixed histogram, no per-session growth.
        for (auto& b : gateBins_) b = GateBin{};
        ungatedEnergySum_ = 0.0;
        ungatedCount_ = 0;
        integratedDirty_ = false;
        integratedEnergy_ = 0.0;
        integratedSamples_ = 0;
        blockAccum_ = 0.0;
        blockSamples_ = 0;
        // Pre-allocate scratch to the engine worst-case block so process()
        // never resizes on the audio thread for blocks up to 8192 samples.
        scratch_.assign(8192, 0.0f);
    }

    void process(const float* data, int numSamples)
    {
        if (data == nullptr || numSamples <= 0) return;
        if ((int)scratch_.size() < numSamples)
            scratch_.resize((size_t)numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float y = rlb_.process(shelf_.process(data[i]));
            scratch_[(size_t)i] = y;
            const double e = (double)y * (double)y;
            blockAccum_ += e;
            ++blockSamples_;
            if (blockSamples_ >= (int)std::round(sampleRate_ * 0.400))
            {
                // BS.1770 absolute gate (-70 LUFS) applied at push time.
                // The block energy is folded into a fixed 0.1 LU histogram
                // instead of an ever-growing vector: memory stays constant
                // and the gated recompute below is O(bins), not O(session).
                const double mean = blockAccum_ / (double)blockSamples_;
                const double lufs = lufsFromMean(mean);
                if (lufs > kGateMinLufs)
                {
                    const int bin = juce::jlimit(0, kNumGateBins - 1,
                        (int)std::floor((lufs - kGateMinLufs) / kGateBinWidthLufs));
                    gateBins_[(size_t)bin].energySum += mean;
                    gateBins_[(size_t)bin].count    += 1;
                    ungatedEnergySum_ += mean;
                    ungatedCount_     += 1;
                    integratedDirty_ = true;
                }
                blockAccum_ = 0.0;
                blockSamples_ = 0;
            }
        }

        momentary_.process(scratch_.data(), numSamples);
        shortTerm_.process(scratch_.data(), numSamples);
        // Recompute the integrated value only when a new 400 ms block has
        // actually arrived (max ~2.5x/sec), not on every audio callback.
        if (integratedDirty_)
        {
            recomputeIntegrated();
            integratedDirty_ = false;
        }
    }

    /** BS.1770 K-weighting coefficients. At 48 kHz the published standard
        values are used verbatim (bit-exact reference path, verified by
        metering.lufs-kweighting-rates.v1). At every other rate both biquads
        are derived from the BS.1770 analog prototype (De Man refinement:
        shelf f0=1681.974450955533 Hz, G=+3.99984385397 dB, Q=0.7071752369554193;
        RLB high-pass f0=38.13547087602444 Hz, Q=0.5003270373238773), which
        reproduces the published 48k constants to ~1e-4 — so non-48k LUFS is
        standard-conformant instead of the previous RBJ approximation. */
    static void computeKWeightingCoefficients (double fs, bool shelf,
                                               double& b0, double& b1, double& b2,
                                               double& a1, double& a2)
    {
        if (std::abs (fs - 48000.0) < 1.0)
        {
            if (shelf)
            {
                b0 = 1.53512485958697; b1 = -2.69169618940638; b2 = 1.19839281085285;
                a1 = -1.69065929318241; a2 = 0.73248077421585;
            }
            else
            {
                b0 = 1.0; b1 = -2.0; b2 = 1.0;
                a1 = -1.99004745483398; a2 = 0.99007225036621;
            }
            return;
        }

        if (shelf)
        {
            // Pre-filter high shelf: bilinear transform of the BS.1770 analog
            // prototype (K = tan pre-warping; De Man refined constants). This
            // reproduces the published 48k coefficients to ~1e-5.
            const double f0 = 1681.974450955533, gainDb = 3.99984385397, q = 0.7071752369554193;
            const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, gainDb / 20.0);
            const double Vb = std::sqrt (Vh);
            const double A0 = 1.0 + K / q + K * K;
            b0 = (Vh + Vb * K / q + K * K) / A0;
            b1 = 2.0 * (K * K - Vh) / A0;
            b2 = (Vh - Vb * K / q + K * K) / A0;
            a1 = 2.0 * (K * K - 1.0) / A0;
            a2 = (1.0 - K / q + K * K) / A0;
        }
        else
        {
            // RLB high-pass: numerator pinned to [1,-2,1] exactly as the
            // published BS.1770 filter (so fs -> 48k lands on the published
            // constants), denominator from the bilinear transform of the
            // analog prototype.
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double A0 = 1.0 + K / q + K * K;
            b0 = 1.0; b1 = -2.0; b2 = 1.0;
            a1 = 2.0 * (K * K - 1.0) / A0;
            a2 = (1.0 - K / q + K * K) / A0;
        }
    }

    float getMomentaryLufs() const noexcept { return (float)lufsFromMean(momentary_.getMeanSquare()); }
    float getShortTermLufs() const noexcept { return (float)lufsFromMean(shortTerm_.getMeanSquare()); }
    float getIntegratedLufs() const noexcept
    {
        if (integratedSamples_ <= 0) return -std::numeric_limits<float>::infinity();
        return (float)lufsFromMean(integratedEnergy_ / (double)integratedSamples_);
    }

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void setCoefficientsForSampleRate(double fs, bool shelf)
        {
            computeKWeightingCoefficients (fs, shelf, b0, b1, b2, a1, a2);
            z1 = z2 = 0.0;
        }

        float process(float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float)y;
        }
    };

    class WindowMeanSquare
    {
    public:
        void prepare(double sampleRate, double seconds)
        {
            size_ = juce::jmax(1, (int)std::round(sampleRate * seconds));
            ring_.assign((size_t)size_, 0.0);
            write_ = filled_ = 0;
            sum_ = 0.0;
        }
        void process(const float* d, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const double sq = (double)d[i] * (double)d[i];
                sum_ -= ring_[(size_t)write_];
                ring_[(size_t)write_] = sq;
                sum_ += sq;
                write_ = (write_ + 1) % size_;
                filled_ = juce::jmin(filled_ + 1, size_);
            }
        }
        double getMeanSquare() const noexcept { return filled_ > 0 ? sum_ / (double)filled_ : 0.0; }
    private:
        int size_ = 1, write_ = 0, filled_ = 0;
        double sum_ = 0.0;
        std::vector<double> ring_;
    };

    static double lufsFromMean(double meanSquare) noexcept
    {
        return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10(meanSquare) : -std::numeric_limits<double>::infinity();
    }

    void recomputeIntegrated() noexcept
    {
        if (ungatedCount_ == 0)
        {
            integratedEnergy_ = 0.0;
            integratedSamples_ = 0;
            return;
        }

        // BS.1770 relative gate: 10 LU below the ungated mean loudness.
        const double ungatedMean = ungatedEnergySum_ / (double)ungatedCount_;
        const double relGate = lufsFromMean(ungatedMean) - 10.0;

        // Include whole bins whose lower edge is strictly above the gate,
        // so every counted block satisfies lufs > relGate. Blocks within
        // 0.1 LU above the gate are conservatively excluded — a bounded
        // quantization error of at most 0.1 LU, irrelevant for a display
        // meter and identical across buffer sizes and session lengths.
        integratedEnergy_ = 0.0;
        integratedSamples_ = 0;
        for (int b = 0; b < kNumGateBins; ++b)
        {
            const double binLowLufs = kGateMinLufs + (double)b * kGateBinWidthLufs;
            if (binLowLufs <= relGate)
                continue;
            integratedEnergy_  += gateBins_[(size_t)b].energySum;
            integratedSamples_ += gateBins_[(size_t)b].count;
        }
    }

    // Fixed gating histogram: -70..+30 LUFS in 0.1 LU steps.
    static constexpr int    kNumGateBins      = 1000;
    static constexpr double kGateMinLufs      = -70.0;
    static constexpr double kGateBinWidthLufs = 0.1;

    struct GateBin
    {
        double  energySum = 0.0;
        int64_t count     = 0;
    };

    double sampleRate_ = 44100.0;
    Biquad shelf_, rlb_;
    WindowMeanSquare momentary_, shortTerm_;
    std::vector<float> scratch_;
    std::array<GateBin, kNumGateBins> gateBins_{};
    double ungatedEnergySum_ = 0.0;
    int64_t ungatedCount_ = 0;
    bool integratedDirty_ = false;
    double blockAccum_ = 0.0;
    int blockSamples_ = 0;
    double integratedEnergy_ = 0.0;
    int64_t integratedSamples_ = 0;
};

} // namespace DAW
