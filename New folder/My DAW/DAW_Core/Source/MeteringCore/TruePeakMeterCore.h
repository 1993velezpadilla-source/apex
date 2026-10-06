#pragma once
#include <JuceHeader.h>
#include "SamplePeakMeterCore.h"
#include <vector>
#include <cmath>

namespace DAW {

class TruePeakMeterCore
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = sampleRate;
        blockSize_ = juce::jmax(1, blockSize);
        meter_.prepare(sampleRate_ * kPhases);
        oversampled_.assign((size_t)blockSize_ * kPhases, 0.0f);
    }

    void process(const float* data, int numSamples)
    {
        if (data == nullptr || numSamples <= 0) return;

        // Resize scratch buffer only when necessary (amortised alloc)
        const int outLen = numSamples * kPhases;
        if ((int)oversampled_.size() < outLen)
            oversampled_.resize((size_t)outLen);

        float* out = oversampled_.data();

        for (int i = 0; i < numSamples; ++i)
        {
            // Clamp tap window to valid input range — branch-free, auto-vectoriser friendly
            const int loTap = juce::jmax(0,            kRadius - i);
            const int hiTap = juce::jmin(kTaps - 1,   kRadius + (numSamples - 1 - i));

            for (int k = 0; k < kPhases; ++k)
            {
                const float* c = kCoeffs.data[k];
                float y = 0.0f;
                for (int ti = loTap; ti <= hiTap; ++ti)
                    y += data[i + ti - kRadius] * c[ti];
                out[(size_t)i * kPhases + (size_t)k] = y;
            }
        }
        meter_.process(out, outLen);
    }

    float getPeak() const noexcept { return meter_.getPeak(); }
    float getPeakDb() const noexcept { return meter_.getPeakDb(); }

private:
    static constexpr int kRadius = 8;
    static constexpr int kTaps   = 2 * kRadius + 1;  // 17
    static constexpr int kPhases = 4;

    // Precomputed, pre-normalised Hann-windowed sinc coefficient table.
    // Built once at static initialisation — eliminates all std::sin / std::cos
    // / std::abs from the audio-thread hot path entirely.
    struct CoeffTable
    {
        float data[kPhases][kTaps];

        CoeffTable() noexcept
        {
            constexpr double kPi = 3.14159265358979323846;
            for (int k = 0; k < kPhases; ++k)
            {
                double norm = 0.0;
                for (int ti = 0; ti < kTaps; ++ti)
                {
                    const int    tap  = ti - kRadius;
                    const double x    = (double)k / (double)kPhases - (double)tap;
                    const double pix  = kPi * x;
                    const double sinc = (x > -1.0e-8 && x < 1.0e-8)
                                            ? 1.0
                                            : std::sin(pix) / pix;
                    const double w    = 0.5 + 0.5 * std::cos(kPi * (double)tap
                                                              / (double)(kRadius + 1));
                    data[k][ti] = (float)(sinc * w);
                    norm += sinc * w;
                }
                // Pre-normalise so the inner loop never needs a division
                if (norm > 1.0e-9)
                    for (int ti = 0; ti < kTaps; ++ti)
                        data[k][ti] = (float)((double)data[k][ti] / norm);
            }
        }
    };

    // One shared table for all instances — constructed before main()
    inline static const CoeffTable kCoeffs{};

    double              sampleRate_ = 44100.0;
    int                 blockSize_  = 512;
    SamplePeakMeterCore meter_;
    std::vector<float>  oversampled_;
};

} // namespace DAW
