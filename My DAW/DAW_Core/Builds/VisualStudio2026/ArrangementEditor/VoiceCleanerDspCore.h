// APEX Voice Cleaner (noise-reduction stage, offline only).
// This is spectral noise suppression, NOT BandLab's proprietary AI,
// and does not claim to perform dereverberation or automatic EQ.
// Pure standard C++17: no JUCE modules or realtime allocations involved.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

namespace ArrangementEditor
{
class VoiceCleanerDspCore
{
public:
    static constexpr int kFftSize = 2048;
    static constexpr int kHop = kFftSize / 4;

    // strength: 0 = bit-identical bypass, 1 = strongest conservative removal.
    // Preserves input size, sample rate and channel alignment; no hard gate.
    static bool process(const float* input, float* output, int samples,
                        double sampleRate, float strength = 0.68f)
    {
        if (!input || !output || samples < 0 || sampleRate <= 0.0) return false;
        if (strength <= 0.0f || samples < kFftSize)
        {
            std::copy_n(input, samples, output);
            return true;
        }
        strength = std::max(0.f, std::min(1.f, strength));

        const int frames = (samples + kHop - 1) / kHop + 2;
        const int bins = kFftSize / 2 + 1;
        std::vector<float> window(kFftSize);
        for (int i = 0; i < kFftSize; ++i)
            window[i] = std::sqrt(std::max(0.f, 0.5f - 0.5f
                              * std::cos(6.283185307179586f * (float)i / (float)kFftSize)));

        struct QuietFrame { float rms; int index; };
        std::vector<QuietFrame> quiet;
        quiet.reserve(frames);
        std::vector<std::complex<float>> spectrum(kFftSize);
        for (int frame = 0; frame < frames; ++frame)
        {
            const int offset = frame * kHop - kFftSize / 2;
            double sum = 0.0;
            for (int i = 0; i < kFftSize; ++i)
            {
                const int at = offset + i;
                const double v = (at >= 0 && at < samples) ? input[at] : 0.0;
                sum += v * v;
            }
            const float rms = (float)std::sqrt(sum / kFftSize);
            // Ignore pure digital silence when learning room noise.
            if (rms > 1.0e-5f && std::isfinite(rms))
                quiet.push_back({rms, frame});
        }
        if (quiet.empty())
        {
            std::copy_n(input, samples, output);
            return true;
        }
        std::sort(quiet.begin(), quiet.end(),
            [](const QuietFrame& a, const QuietFrame& b) { return a.rms < b.rms; });
        // Learn cricket/hiss spectrum from the quietest usable ~18% of frames.
        // Limit the profile to at most 64 frames for bounded analysis time.
        const int learnCount = std::min<int>(64,
                              std::max<int>(1, (int)quiet.size() * 18 / 100));
        std::vector<float> noisePower(bins, 0.f);
        for (int q = 0; q < learnCount; ++q)
        {
            const int offset = quiet[(size_t)q].index * kHop - kFftSize / 2;
            loadWindowed(input, samples, offset, window, spectrum);
            fft(spectrum, false);
            for (int k = 0; k < bins; ++k)
                noisePower[(size_t)k] += std::norm(spectrum[(size_t)k]);
        }
        const float inv = 1.f / (float)learnCount;
        for (auto& value : noisePower) value *= inv;

        std::vector<float> overlap((size_t)samples + kFftSize, 0.f);
        std::vector<float> weight((size_t)samples + kFftSize, 0.f);
        std::vector<float> smoothGain(bins, 1.f);

        for (int frame = 0; frame < frames; ++frame)
        {
            const int offset = frame * kHop - kFftSize / 2;
            loadWindowed(input, samples, offset, window, spectrum);
            fft(spectrum, false);

            for (int k = 0; k < bins; ++k)
            {
                const float frequency = (float)(k * sampleRate / kFftSize);
                // Protect fundamental voice frequencies and DC.
                if (frequency < 110.f) continue;
                const float p = std::norm(spectrum[(size_t)k]);
                const float n = noisePower[(size_t)k];
                // Wiener-like soft mask with generous voice floor; reduce
                // tonal ambience without chopping vowels or breath endings.
                const float subtraction = strength * 1.65f * n;
                const float cleanRatio = std::max(0.f, (p - subtraction) /
                                                     std::max(p, 1.0e-12f));
                const float target = std::max(0.24f, std::sqrt(cleanRatio));
                // Slow attenuation release prevents rapid musical-noise chatter.
                const float previous = smoothGain[(size_t)k];
                const float smoothed = previous + (target - previous) *
                                                (target < previous ? 0.55f : 0.24f);
                smoothGain[(size_t)k] = smoothed;
                spectrum[(size_t)k] *= smoothed;
                if (k > 0 && k < kFftSize / 2)
                    spectrum[(size_t)(kFftSize - k)] *= smoothed;
            }
            fft(spectrum, true);
            for (int i = 0; i < kFftSize; ++i)
            {
                const int at = offset + i;
                if (at < 0 || at >= samples) continue;
                const float w = window[(size_t)i];
                overlap[(size_t)at] += spectrum[(size_t)i].real() * w;
                weight[(size_t)at] += w * w;
            }
        }
        for (int i = 0; i < samples; ++i)
        {
            const float value = weight[(size_t)i] > 1.e-6f
                ? overlap[(size_t)i] / weight[(size_t)i]
                : input[i];
            output[i] = std::isfinite(value) ? value : input[i];
        }
        return true;
    }

private:
    static void loadWindowed(const float* input, int length, int offset,
                             const std::vector<float>& window,
                             std::vector<std::complex<float>>& dst)
    {
        for (int i = 0; i < kFftSize; ++i)
        {
            const int at = offset + i;
            dst[(size_t)i] = { (at >= 0 && at < length ? input[at] : 0.f)
                                  * window[(size_t)i], 0.f };
        }
    }

    static void fft(std::vector<std::complex<float>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const float phase = (inverse ? 1.f : -1.f) * 6.283185307179586f / (float)len;
            const std::complex<float> wlen(std::cos(phase), std::sin(phase));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w(1.f, 0.f);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j];
                    const auto v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
        if (inverse)
        {
            const float scale = 1.f / (float)n;
            for (auto& value : a) value *= scale;
        }
    }
};
} // namespace ArrangementEditor
