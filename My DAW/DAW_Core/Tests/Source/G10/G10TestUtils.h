#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include "../../../Source/G10Core/G10Processor.h"
#include "../../../Source/G10Core/G10NativePluginFormat.h"
#include "../../../Source/EQCore/ApexInteractiveEQSurface.h" // explicit dependency for the using-declarations below

// ============================================================================
// G10TestUtils — test-only helpers for the APEX.G10 suite.
//
// Self-contained radix-2 FFT (no juce_dsp), impulse-response measurement,
// magnitude-at-frequency, processor preparation, parameter lookup, finite
// verification, mono/stereo comparison, and deterministic timing.
// ============================================================================

namespace G10Test
{

using APEX::G10::G10Processor;
using APEX::G10::G10Parameter;
using APEX::G10::G10CurveEngineCore;
using APEX::G10::kNumBands;
using APEX::G10::kNumParams;
using APEX::G10::kBandInfos;
using APEX::G10::G10ParamIndex;

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

    /** In-place complex FFT. real/imag must each have size() entries. */
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

/** Prepare a processor in the given host mode. The non-realtime flag is set
    BEFORE prepareToPlay, exactly like the real host's offline sequence
    (PluginInstanceCore::prepareForOffline: releaseResources -> setNonRealtime
    -> prepare). The canonical quality policy reads isNonRealtime() at
    prepareToPlay, so this helper is the correct way to build an offline
    (HQ 4x) processor in tests. */
inline std::unique_ptr<G10Processor> makePreparedProcessor (double sampleRate,
                                                            int blockSize,
                                                            bool nonRealtime)
{
    auto proc = std::make_unique<G10Processor>();
    proc->setNonRealtime (nonRealtime);
    proc->prepareToPlay (sampleRate, blockSize);
    return proc;
}

inline std::unique_ptr<G10Processor> makePreparedProcessor (double sampleRate,
                                                            int blockSize)
{
    return makePreparedProcessor (sampleRate, blockSize, false);
}

// ---------------------------------------------------------------------------
// Parameter lookup by stable ID.
// ---------------------------------------------------------------------------

inline G10Parameter* findParam (juce::AudioProcessor& proc, const juce::String& id)
{
    for (auto* p : proc.getParameters())
        if (auto* pwid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (pwid->paramID == id)
                return dynamic_cast<G10Parameter*> (pwid);
    return nullptr;
}

inline G10Parameter* findParam (G10Processor& proc, const juce::String& id)
{
    return findParam (static_cast<juce::AudioProcessor&> (proc), id);
}

// ---------------------------------------------------------------------------
// Deterministic test signal (LCG, no allocation in the hot loop).
// ---------------------------------------------------------------------------

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

/** Process a unit impulse through the processor and capture the impulse
    response. The processor must already be prepared. The impulse is injected
    at the start of the capture; the response is the steady-state filter
    response because parameter smoothing is settled before capture. */
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

/** Measure the steady-state magnitude response (dB) of a prepared processor
    at `freq` by settling, capturing the impulse response, and FFT. */
inline float measureGainDb (juce::AudioProcessor& proc, double sampleRate,
                            int blockSize, double freq,
                            int irLength = 8192, int fftSize = 16384)
{
    settleProcessor (proc, sampleRate, blockSize, 1.0);
    auto ir = measureImpulseResponse (proc, irLength, blockSize);
    return magnitudeDbAtFrequency (ir, sampleRate, freq, fftSize);
}

/** Measure the steady-state gain (dB) of a prepared processor at `freq`
    using a sine at the SAME operating level as the signal under test. The
    canonical analog chain (D1B + I1) is level-dependent, so an impulse
    (0 dBFS) is compressed differently than a steady sine: an impulse-derived
    static gain cannot predict the nonlinear steady-state response. Measuring
    at the operating level makes both quantities describe the same nonlinear
    operating condition. The processor is settled first (parameter smoothing,
    D1B DC blocker, I1 flux), then the RMS gain of the sine is measured. */
inline float measureGainDbAtLevel (juce::AudioProcessor& proc, double sampleRate,
                                   int blockSize, double freq, float level,
                                   double seconds = 0.5)
{
    settleProcessor (proc, sampleRate, blockSize, 1.0);

    double sumSq = 0.0;
    int total = 0;
    juce::AudioBuffer<float> buf (1, blockSize);
    juce::MidiBuffer midi;
    const int n = (int) (sampleRate * seconds);
    int pos = 0;
    while (pos < n)
    {
        const int cnt = juce::jmin (blockSize, n - pos);
        for (int i = 0; i < cnt; ++i)
            buf.setSample (0, i, level * (float) std::sin (
                2.0 * juce::MathConstants<double>::pi * freq * (pos + i) / sampleRate));
        proc.processBlock (buf, midi);
        for (int i = 0; i < cnt; ++i)
        {
            const float v = buf.getSample (0, i);
            sumSq += (double) v * v;
            ++total;
        }
        pos += cnt;
    }
    const double outRms = std::sqrt (sumSq / std::max (1, total));
    const double inRms = (double) level / std::sqrt (2.0);
    return (float) (20.0 * std::log10 (std::max (1e-12, outRms / inRms)));
}

/** Measure the steady-state magnitude response (dB) of a bare G10 curve
    engine at `freq` by settling, capturing the impulse response, and FFT.
    The curve-shape contract (Q laws, boost/cut asymmetry, shelf morphs)
    belongs to the curve engine itself: the canonical processor path now
    includes the level-dependent D1B/I1 color stages, so processor-level
    impulse measurements can no longer isolate the EQ-curve response. The
    frozen clean engine is the regression authority for the curve contract. */
inline float measureEngineGainDb (G10CurveEngineCore& engine, double sampleRate,
                                  int blockSize, double freq,
                                  int irLength = 8192, int fftSize = 16384)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    float* chans[1] = { buf.getWritePointer (0) };

    // Settle the engine's parameter smoothing with silence.
    const int settle = (int) (sampleRate * 1.0);
    int done = 0;
    while (done < settle)
    {
        buf.clear();
        engine.processBlock (chans, 1, blockSize);
        done += blockSize;
    }

    // Impulse response.
    std::vector<float> ir (irLength, 0.0f);
    bool impulseSent = false;
    int written = 0;
    while (written < irLength)
    {
        buf.clear();
        if (! impulseSent)
        {
            buf.setSample (0, 0, 1.0f);
            impulseSent = true;
        }
        engine.processBlock (chans, 1, blockSize);
        const int n = juce::jmin (blockSize, irLength - written);
        for (int i = 0; i < n; ++i)
            ir[written + i] = buf.getSample (0, i);
        written += n;
    }
    return magnitudeDbAtFrequency (ir, sampleRate, freq, fftSize);
}

/** Measure the steady-state magnitude response (dB) of a bare G10 curve
    engine at `freq` using a low-level steady sine. The engine is linear, so
    the RMS output/RMS input ratio equals the transfer magnitude exactly.

    Why steady-sine instead of impulse-FFT: the curve families include
    low-frequency compound structures (DEEP 31 Hz bell + 45 Hz shelf, PUNCH
    63 Hz + 120 Hz support). A short impulse-FFT at 31/63 Hz is sensitive to
    window truncation, FFT-bin resolution, and leakage; a steady sine with a
    long measurement window has none of those artifacts. The engine is
    settled (parameter smoothing) with silence first, then the sine runs for
    `settleSeconds` (transient discarded — 31 Hz needs ~1.5 s to reach
    steady state) and RMS is accumulated over `measureSeconds`. */
inline float measureEngineGainDbSteady (G10CurveEngineCore& engine, double sampleRate,
                                        int blockSize, double freq,
                                        double settleSeconds = 2.0,
                                        double measureSeconds = 1.0)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    float* chans[1] = { buf.getWritePointer (0) };

    // Settle parameter smoothing with silence (band ~20 ms, trims ~15 ms).
    const int settleSamples = (int) (sampleRate * 1.0);
    int done = 0;
    while (done < settleSamples)
    {
        buf.clear();
        engine.processBlock (chans, 1, blockSize);
        done += blockSize;
    }

    const double inPeak = 0.01; // low level: the engine is linear, so the
                                // amplitude does not matter; keep it small
                                // to stay far from any denormal flush.
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

/** Process the same mono signal through a mono processor and a stereo
    processor; returns true if the mono output matches the stereo processor's
    left channel sample-for-sample. */
inline bool monoMatchesStereoLeft (G10Processor& monoProc, G10Processor& stereoProc,
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

} // namespace G10Test
// Namespace conveniences for G10 test translation units (global scope).
// These bind the Mini Clean EQ core and the reusable interactive surface so
// test classes at global scope can use the unqualified names.
using APEX::G10::G10MiniEqCore;
using APEX::ApexInteractiveEQSurface;
