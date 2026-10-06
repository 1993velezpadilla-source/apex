// ===========================================================================
// GranularStretchCore.h
// Motor granular para Mode 5 (Texture/Creative).
//
// POR QUÉ GRANULAR:
//   Divide el audio en "granos" pequeños (20-120ms).
//   Reproduce granos solapados para cambiar duración.
//   A diferencia de WSOLA, acepta artefactos creativos:
//     - Chorus suave en stretch 1:1
//     - Dreamlike smear en stretch 2:1+
//     - Freeze-like con stretch >4:1
//
//   Usado en: Ableton "Texture" warp mode, granular synths, creative FX.
//
// ALGORITMO:
//   Para cada grano de output:
//     1. Lee grainSize samples desde src en grainPos.
//     2. Aplica envelope Hann (attack/sustain/release).
//     3. Suma con overlap en el output.
//     4. grainPos avanza grainHopAna = grainHopSyn / stretchRatio.
//     5. Con jitter: grainPos += random(-jitter, +jitter).
//
// AUDIO THREAD SAFETY:
//   Sin allocations por bloque. Todo pre-alocado en prepare().
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <atomic>
#include <random>

namespace ArrangementEditor
{

class GranularStretchCore
{
public:
    GranularStretchCore() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_  = sampleRate;
        // Default: 40ms grains, 4x overlap = hop 10ms
        grainSize_  = (int)(sampleRate * 0.040);
        grainHopSyn_= grainSize_ / 4;
        buildHann(grainSize_);

        for (int ch = 0; ch < 2; ++ch)
        {
            const int cap = grainSize_ * 8 + maxBlockSize * 4;
            accum_[ch].assign(cap, 0.f);
            accumNorm_[ch].assign(cap, 0.f);
        }
        accumReadPos_[0]  = accumReadPos_[1]  = 0;
        accumWritePos_[0] = accumWritePos_[1] = 0;
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            grainReadPos_[ch]  = 0.0;
            accumReadPos_[ch]  = 0;
            accumWritePos_[ch] = 0;
            std::fill(accum_[ch].begin(), accum_[ch].end(), 0.f);
            std::fill(accumNorm_[ch].begin(), accumNorm_[ch].end(), 0.f);
        }
    }

    // Granular latency = half grain size (standard granular pre-fill)
    int latencySamples() const noexcept { return grainSize_ / 2; }

    void setStretchRatio(double ratio) { stretchRatio_.store(juce::jlimit(0.1, 16.0, ratio)); }
    void setJitter(float j)           { jitter_.store(juce::jlimit(0.f, 1.f, j)); }
    // Grain size in milliseconds (20..120)
    void setGrainSizeMs(float ms)
    {
        const int gs = juce::jlimit(64, (int)(sampleRate_ * 0.12),
                                    (int)(sampleRate_ * ms / 1000.0));
        grainSize_   = gs;
        grainHopSyn_ = gs / 4;
        buildHann(gs);
    }

    // -----------------------------------------------------------------------
    // renderSegment — genera dstNumSamples en dst desde src[srcStart..srcEnd]
    // -----------------------------------------------------------------------
    void renderSegment(const float* src, int srcTotal,
                       int64_t srcStart, int64_t srcEnd,
                       float* dst, int dstNumSamples,
                       int /*channel*/ ch)
    {
        const double ratio   = stretchRatio_.load();
        const float  jit     = jitter_.load();
        const int    srcAvail= (int)std::min((int64_t)srcTotal, srcEnd - srcStart);
        if (srcAvail <= 0) { std::fill(dst, dst + dstNumSamples, 0.f); return; }

        const int cap = (int)accum_[ch].size();
        const double hopAna = (double)grainHopSyn_ / ratio;

        // Generate enough grains
        int generated = 0;
        while (generated < dstNumSamples + grainSize_)
        {
            int64_t grainStart = srcStart + (int64_t)grainReadPos_[ch];
            // Apply jitter in source domain
            if (jit > 0.f)
            {
                const int jitterSamples = (int)(jit * grainSize_ * 0.5f);
                if (jitterSamples > 0)
                {
                    std::uniform_int_distribution<int> dist(-jitterSamples, jitterSamples);
                    grainStart += dist(rng_);
                }
            }
            grainStart = std::max((int64_t)srcStart, std::min((int64_t)(srcEnd - grainSize_), grainStart));

            // OLA grain into accumulator
            for (int i = 0; i < grainSize_; ++i)
            {
                const int64_t si = grainStart + i;
                const float s = (si >= 0 && si < srcTotal) ? src[(int)si] : 0.f;
                const float w = hann_[i % (int)hann_.size()];
                const int   ai = (accumWritePos_[ch] + i) % cap;
                accum_[ch][ai]     += s * w;
                accumNorm_[ch][ai] += w;
            }

            accumWritePos_[ch]  = (accumWritePos_[ch] + grainHopSyn_) % cap;
            generated          += grainHopSyn_;
            grainReadPos_[ch]  += hopAna;

            if (grainReadPos_[ch] >= (double)srcAvail)
            {
                grainReadPos_[ch] = (double)srcAvail;
                break;
            }
        }

        // Read output
        for (int i = 0; i < dstNumSamples; ++i)
        {
            const int   ai = (accumReadPos_[ch] + i) % cap;
            const float n  = accumNorm_[ch][ai];
            dst[i]         = (n > 0.001f) ? accum_[ch][ai] / n : 0.f;
        }
        // Clear consumed
        for (int i = 0; i < dstNumSamples; ++i)
        {
            const int ai = (accumReadPos_[ch] + i) % cap;
            accum_[ch][ai]     = 0.f;
            accumNorm_[ch][ai] = 0.f;
        }
        accumReadPos_[ch] = (accumReadPos_[ch] + dstNumSamples) % cap;
    }

private:
    void buildHann(int size)
    {
        hann_.resize(size);
        for (int i = 0; i < size; ++i)
            hann_[i] = 0.5f * (1.f - std::cos(
                2.f * juce::MathConstants<float>::pi * i / (float)(size - 1)));
    }

    double sampleRate_    = 44100.0;
    int    grainSize_     = 1764;
    int    grainHopSyn_   = 441;

    std::atomic<double> stretchRatio_ { 1.0 };
    std::atomic<float>  jitter_       { 0.0f };

    // Bug 41 fix: per-channel state. grainReadPos_, accumWritePos_, and
    // accumReadPos_ were previously shared across channels. When renderSegment
    // was called for ch=0 then ch=1, both write and read positions were advanced
    // twice — producing wrong grain positions and double-advancing the read
    // pointer, which caused silence / phase corruption on channel 1.
    double grainReadPos_[2]  = { 0.0, 0.0 };
    int    accumReadPos_[2]  = { 0, 0 };
    int    accumWritePos_[2] = { 0, 0 };

    std::vector<float> hann_;
    std::vector<float> accum_[2];
    std::vector<float> accumNorm_[2];

    std::default_random_engine rng_ { 42 };
};

} // namespace ArrangementEditor
