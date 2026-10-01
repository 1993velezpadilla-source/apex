#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include "../../../Source/C4Core/C4Processor.h"
#include "../../../Source/C4Core/C4NativePluginFormat.h"

// ============================================================================
// C4TestUtils — test-only helpers for the APEX.C4 suite.
//
// Deliberately SELF-CONTAINED (namespace C4Test, no global using-declarations
// that could collide with the G10 suite's shared headers): a radix-2 FFT,
// impulse-response measurement, magnitude-at-frequency, processor
// preparation, parameter lookup, finite verification, mono/stereo comparison
// and deterministic timing.
//
// Measurement contract: C4 Phase 1 is fully linear, so settled impulse-FFT
// magnitude equals the true transfer response. Engine-level steady-sine RMS
// is provided for low-frequency accuracy (window truncation immunity), same
// rationale as the G10 suite.
// ============================================================================

namespace C4Test
{

using APEX::C4::C4Processor;
using APEX::C4::C4Parameter;
using APEX::C4::C4EngineCore;
using APEX::C4::C4TuningProfile;
using APEX::C4::kNumBands;
using APEX::C4::kNumParams;
using APEX::C4::C4ParamIndex;

// ---------------------------------------------------------------------------
// Self-contained radix-2 decimation-in-time FFT (test-only; no juce_dsp).
// ---------------------------------------------------------------------------

class Radix2Fft
{
public:
    explicit Radix2Fft (int size)
        : size_ (size)
    {
        jassert (size > 0 && (size & (size - 1)) == 0); // power of two
        int numBits = 0;
        for (int s = size; s > 1; s >>= 1)
            ++numBits;

        rev_.resize (size);
        for (int i = 0; i < size; ++i)
        {
            int rev = 0;
            for (int b = 0; b < numBits; ++b)
                if (i & (1 << b))
                    rev |= (1 << (numBits - 1 - b));
            rev_[i] = rev;
        }

        cos_.resize (size / 2);
        sin_.resize (size / 2);
        for (int i = 0; i < size / 2; ++i)
        {
            const double angle = -2.0 * juce::MathConstants<double>::pi * i / size;
            cos_[i] = (float) std::cos (angle);
            sin_[i] = (float) std::sin (angle);
        }
    }

    void transform (float* real, float* imag) const
    {
        for (int i = 0; i < size_; ++i)
        {
            const int j = rev_[i];
            if (j > i)
            {
                std::swap (real[i], real[j]);
                std::swap (imag[i], imag[j]);
            }
        }

        for (int len = 2; len <= size_; len <<= 1)
        {
            const int half = len >> 1;
            const int step = size_ / len;
            for (int i = 0; i < size_; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const int tw = j * step;
                    const float wr = cos_[tw];
                    const float wi = sin_[tw];
                    const int a = i + j;
                    const int b = a + half;
                    const float tr = real[b] * wr - imag[b] * wi;
                    const float ti = real[b] * wi + imag[b] * wr;
                    real[b] = real[a] - tr;
                    imag[b] = imag[a] - ti;
                    real[a] += tr;
                    imag[a] += ti;
                }
            }
        }
    }

    int getSize() const noexcept { return size_; }

private:
    int size_;
    std::vector<int> rev_;
    std::vector<float> cos_, sin_;
};

// ---------------------------------------------------------------------------
// Processor preparation.
// ---------------------------------------------------------------------------

inline std::unique_ptr<C4Processor> makePreparedProcessor (double sampleRate,
                                                           int blockSize,
                                                           const C4TuningProfile& profile =
                                                               APEX::C4::makeProfilePhase1Reference())
{
    auto proc = std::make_unique<C4Processor> (profile);
    proc->prepareToPlay (sampleRate, blockSize);
    return proc;
}

// ---------------------------------------------------------------------------
// Parameter lookup by stable ID.
// ---------------------------------------------------------------------------

inline C4Parameter* findParam (juce::AudioProcessor& proc, const juce::String& id)
{
    for (auto* p : proc.getParameters())
        if (auto* pwid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (pwid->paramID == id)
                return dynamic_cast<C4Parameter*> (pwid);
    return nullptr;
}

inline C4Parameter* findParam (C4Processor& proc, const juce::String& id)
{
    return findParam (static_cast<juce::AudioProcessor&> (proc), id);
}

// ---------------------------------------------------------------------------
// Deterministic test signal (LCG, no allocation in the hot loop).
// ---------------------------------------------------------------------------

/** Explicit zero-fill for audio buffers in DIRECT-ENGINE tests.
    CRITICAL CONTRACT (measured root cause of the C4 growing-IR investigation):
    JUCE's AudioSampleBuffer::clear() early-returns when the buffer is marked
    cleared (isClear == true). The flag is set by clear() itself and is reset
    ONLY by JUCE write-accessors (setSample / getWritePointer /
    getArrayOfWritePointers / ...). A DSP engine that writes through raw
    float** pointers (the correct realtime pattern) never resets the flag, so
    the NEXT flagged clear() is silently skipped and the previous output
    feeds back as input (exponential growth at the loop gain).
    Direct-engine tests MUST therefore clear with this explicit zero-fill,
    which unconditionally writes zeros and is immune to the flag. */
inline void clearBufferExplicit (juce::AudioBuffer<float>& buffer) noexcept
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        std::fill_n (buffer.getWritePointer (ch), buffer.getNumSamples(), 0.0f);
}

inline void fillDeterministic (juce::AudioBuffer<float>& buffer, juce::uint32 seed)
{
    juce::uint32 state = seed;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            state = state * 1664525u + 1013904223u;
            buffer.setSample (ch, i, (float) (state / 4294967296.0) * 2.0f - 1.0f);
        }
}

// ---------------------------------------------------------------------------
// Finite-sample verification.
// ---------------------------------------------------------------------------

inline bool allFinite (const juce::AudioBuffer<float>& buf)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int i = 0; i < buf.getNumSamples(); ++i)
            if (! std::isfinite (buf.getSample (ch, i)))
                return false;
    return true;
}

inline bool allFinite (const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite (x))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Impulse-response measurement of the real processor.
// ---------------------------------------------------------------------------

inline std::vector<float> measureImpulseResponse (juce::AudioProcessor& proc,
                                                  int irLength,
                                                  int blockSize)
{
    std::vector<float> ir (irLength, 0.0f);
    juce::AudioBuffer<float> buffer (1, blockSize);
    juce::MidiBuffer midi;
    bool impulseSent = false;
    int written = 0;

    while (written < irLength)
    {
        buffer.clear();
        if (! impulseSent)
        {
            buffer.setSample (0, 0, 1.0f);
            impulseSent = true;
        }
        proc.processBlock (buffer, midi);
        const int n = juce::jmin (blockSize, irLength - written);
        for (int i = 0; i < n; ++i)
            ir[written + i] = buffer.getSample (0, i);
        written += n;
    }
    return ir;
}

/** Settle parameter smoothing by processing silence for `seconds`. */
inline void settleProcessor (juce::AudioProcessor& proc, double sampleRate,
                             int blockSize, double seconds = 1.0)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const int total = (int) (sampleRate * seconds);
    int done = 0;
    while (done < total)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
        done += blockSize;
    }
}

// ---------------------------------------------------------------------------
// Magnitude-at-frequency (dB) from an impulse response via the test FFT.
// ---------------------------------------------------------------------------

inline float magnitudeDbAtFrequency (const std::vector<float>& ir,
                                     double sampleRate,
                                     double freq,
                                     int fftSize)
{
    Radix2Fft fft (fftSize);
    std::vector<float> real (fftSize, 0.0f);
    std::vector<float> imag (fftSize, 0.0f);
    const int n = std::min ((int) ir.size(), fftSize);
    for (int i = 0; i < n; ++i)
        real[i] = ir[i];
    fft.transform (real.data(), imag.data());

    const double binHz = sampleRate / fftSize;
    const int bin = (int) std::lround (freq / binHz);
    if (bin < 0 || bin >= fftSize)
        return -300.0f;

    const double mag = std::sqrt ((double) real[bin] * real[bin]
                                + (double) imag[bin] * imag[bin]);
    return (float) (20.0 * std::log10 (std::max (1e-12, mag)));
}

/** Phase (radians) of the response at `freq` from an impulse response. */
inline float phaseRadiansAtFrequency (const std::vector<float>& ir,
                                      double sampleRate,
                                      double freq,
                                      int fftSize)
{
    Radix2Fft fft (fftSize);
    std::vector<float> real (fftSize, 0.0f);
    std::vector<float> imag (fftSize, 0.0f);
    const int n = std::min ((int) ir.size(), fftSize);
    for (int i = 0; i < n; ++i)
        real[i] = ir[i];
    fft.transform (real.data(), imag.data());

    const double binHz = sampleRate / fftSize;
    const int bin = (int) std::lround (freq / binHz);
    if (bin < 0 || bin >= fftSize)
        return 0.0f;

    return (float) std::atan2 (imag[bin], real[bin]);
}

/** COMPLEX transfer response (real + imaginary) at `freq` from an impulse
    response. The composition audit needs the complex response — magnitude-
    only superposition is wrong whenever neighboring skirts carry phase. */
inline std::complex<float> complexResponseAtFrequency (const std::vector<float>& ir,
                                                       double sampleRate,
                                                       double freq,
                                                       int fftSize)
{
    Radix2Fft fft (fftSize);
    std::vector<float> real (fftSize, 0.0f);
    std::vector<float> imag (fftSize, 0.0f);
    const int n = std::min ((int) ir.size(), fftSize);
    for (int i = 0; i < n; ++i)
        real[i] = ir[i];
    fft.transform (real.data(), imag.data());

    const double binHz = sampleRate / fftSize;
    const int bin = (int) std::lround (freq / binHz);
    if (bin < 0 || bin >= fftSize)
        return { 0.0f, 0.0f };

    return { real[bin], imag[bin] };
}

/** Steady-state magnitude response (dB) of a prepared linear processor at
    `freq`: settle, impulse, FFT. */
inline float measureGainDb (juce::AudioProcessor& proc, double sampleRate,
                            int blockSize, double freq,
                            int irLength = 8192, int fftSize = 16384)
{
    settleProcessor (proc, sampleRate, blockSize, 1.0);
    auto ir = measureImpulseResponse (proc, irLength, blockSize);
    return magnitudeDbAtFrequency (ir, sampleRate, freq, fftSize);
}

/** Steady-state gain (dB) of a prepared processor using a low-level steady
    sine — engine-level linearity makes RMS ratio exact; immune to FFT window
    truncation at low frequencies. The processor is settled first. */
inline float measureGainDbSteady (juce::AudioProcessor& proc, double sampleRate,
                                  int blockSize, double freq,
                                  double settleSeconds = 1.5,
                                  double measureSeconds = 1.0)
{
    settleProcessor (proc, sampleRate, blockSize, 1.0);

    const double inPeak = 0.01;
    const int total = (int) (sampleRate * (settleSeconds + measureSeconds));
    const int discard = (int) (sampleRate * settleSeconds);

    double sumSq = 0.0;
    int count = 0;
    juce::AudioBuffer<float> buf (1, blockSize);
    juce::MidiBuffer midi;
    int pos = 0;
    while (pos < total)
    {
        const int cnt = juce::jmin (blockSize, total - pos);
        for (int i = 0; i < cnt; ++i)
            buf.setSample (0, i, (float) (inPeak * std::sin (
                2.0 * juce::MathConstants<double>::pi * freq * (pos + i) / sampleRate)));
        proc.processBlock (buf, midi);
        for (int i = 0; i < cnt; ++i)
        {
            const int idx = pos + i;
            if (idx >= discard && idx < total)
            {
                const float v = buf.getSample (0, i);
                sumSq += (double) v * v;
                ++count;
            }
        }
        pos += cnt;
    }

    const double outRms = std::sqrt (sumSq / std::max (1, count));
    const double inRms = inPeak / std::sqrt (2.0);
    return (float) (20.0 * std::log10 (std::max (1e-12, outRms / inRms)));
}

/** Steady-state gain (dB) of a bare C4 engine (linear) at `freq` using a
    low-level steady sine — used when the processor-level path is not needed
    (curve contract tests). The engine is prepared and settled first. */
inline float measureEngineGainDbSteady (C4EngineCore& engine, double sampleRate,
                                        int blockSize, double freq,
                                        double settleSeconds = 2.0,
                                        double measureSeconds = 1.0)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    float* chans[1] = { buf.getWritePointer (0) };

    // Settle parameter smoothing with silence.
    const int settleSamples = (int) (sampleRate * 1.0);
    int done = 0;
    while (done < settleSamples)
    {
        clearBufferExplicit (buf); // explicit: raw-pointer engine writes
                                   // defeat JUCE's flagged clear()
        engine.processBlock (chans, 1, blockSize);
        done += blockSize;
    }

    const double inPeak = 0.01;
    const int total = (int) (sampleRate * (settleSeconds + measureSeconds));
    const int discard = (int) (sampleRate * settleSeconds);

    double sumSq = 0.0;
    int count = 0;
    int pos = 0;
    while (pos < total)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const int idx = pos + i;
            buf.setSample (0, i, (float) (inPeak * std::sin (
                2.0 * juce::MathConstants<double>::pi * freq * idx / sampleRate)));
        }
        engine.processBlock (chans, 1, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const int idx = pos + i;
            if (idx >= discard && idx < total)
            {
                const float v = buf.getSample (0, i);
                sumSq += (double) v * v;
                ++count;
            }
        }
        pos += blockSize;
    }

    const double outRms = std::sqrt (sumSq / std::max (1, count));
    const double inRms = inPeak / std::sqrt (2.0);
    return (float) (20.0 * std::log10 (std::max (1e-12, outRms / inRms)));
}

// ---------------------------------------------------------------------------
// Mono/stereo comparison.
// ---------------------------------------------------------------------------

inline bool monoMatchesStereoLeft (C4Processor& monoProc, C4Processor& stereoProc,
                                   const std::vector<float>& signal, int blockSize)
{
    juce::AudioBuffer<float> monoBuf (1, blockSize);
    juce::AudioBuffer<float> stereoBuf (2, blockSize);
    juce::MidiBuffer midi;

    const int total = (int) signal.size();
    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (blockSize, total - pos);
        monoBuf.clear();
        stereoBuf.clear();
        for (int i = 0; i < n; ++i)
        {
            monoBuf.setSample (0, i, signal[pos + i]);
            stereoBuf.setSample (0, i, signal[pos + i]);
            stereoBuf.setSample (1, i, signal[pos + i]);
        }
        monoProc.processBlock (monoBuf, midi);
        stereoProc.processBlock (stereoBuf, midi);
        for (int i = 0; i < n; ++i)
        {
            if (monoBuf.getSample (0, i) != stereoBuf.getSample (0, i))
                return false;
            if (stereoBuf.getSample (0, i) != stereoBuf.getSample (1, i))
                return false;
        }
        pos += n;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Deterministic timing.
// ---------------------------------------------------------------------------

inline double timeSeconds (const std::function<void()>& fn, int iterations)
{
    const auto start = juce::Time::getHighResolutionTicks();
    for (int i = 0; i < iterations; ++i)
        fn();
    const auto end = juce::Time::getHighResolutionTicks();
    return juce::Time::highResolutionTicksToSeconds (end - start);
}

} // namespace C4Test
