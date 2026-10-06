#pragma once
#include <JuceHeader.h>
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
        blockMeans_.clear();
        integratedEnergy_ = 0.0;
        integratedSamples_ = 0;
        blockAccum_ = 0.0;
        blockSamples_ = 0;
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
                const double mean = blockAccum_ / (double)blockSamples_;
                if (lufsFromMean(mean) > -70.0)
                    blockMeans_.push_back(mean);
                blockAccum_ = 0.0;
                blockSamples_ = 0;
            }
        }

        momentary_.process(scratch_.data(), numSamples);
        shortTerm_.process(scratch_.data(), numSamples);
        recomputeIntegrated();
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
            if (std::abs(fs - 48000.0) < 1.0)
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
            }
            else if (shelf)
            {
                const double f0 = 1500.0, gainDb = 4.0, q = 0.70710678118;
                const double A = std::pow(10.0, gainDb / 40.0);
                const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
                const double alpha = std::sin(w0) / (2.0 * q);
                const double c = std::cos(w0);
                const double beta = 2.0 * std::sqrt(A) * alpha;
                double B0 = A * ((A + 1.0) + (A - 1.0) * c + beta);
                double B1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
                double B2 = A * ((A + 1.0) + (A - 1.0) * c - beta);
                double A0 = (A + 1.0) - (A - 1.0) * c + beta;
                double A1 = 2.0 * ((A - 1.0) - (A + 1.0) * c);
                double A2 = (A + 1.0) - (A - 1.0) * c - beta;
                b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
            }
            else
            {
                const double f0 = 38.0, q = 0.5;
                const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
                const double alpha = std::sin(w0) / (2.0 * q);
                const double c = std::cos(w0);
                double B0 = (1.0 + c) * 0.5;
                double B1 = -(1.0 + c);
                double B2 = (1.0 + c) * 0.5;
                double A0 = 1.0 + alpha;
                double A1 = -2.0 * c;
                double A2 = 1.0 - alpha;
                b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
            }
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
        if (blockMeans_.empty())
        {
            integratedEnergy_ = 0.0;
            integratedSamples_ = 0;
            return;
        }

        double ungated = 0.0;
        for (double m : blockMeans_) ungated += m;
        ungated /= (double)blockMeans_.size();
        const double relGate = lufsFromMean(ungated) - 10.0;

        integratedEnergy_ = 0.0;
        integratedSamples_ = 0;
        for (double m : blockMeans_)
        {
            const double lufs = lufsFromMean(m);
            if (lufs > -70.0 && lufs > relGate)
            {
                integratedEnergy_ += m;
                ++integratedSamples_;
            }
        }
    }

    double sampleRate_ = 44100.0;
    Biquad shelf_, rlb_;
    WindowMeanSquare momentary_, shortTerm_;
    std::vector<float> scratch_;
    std::vector<double> blockMeans_;
    double blockAccum_ = 0.0;
    int blockSamples_ = 0;
    double integratedEnergy_ = 0.0;
    int64_t integratedSamples_ = 0;
};

} // namespace DAW
