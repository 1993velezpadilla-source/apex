#include <JuceHeader.h>
#include "G10TestUtils.h"

#include <algorithm>
#include <cmath>
#include <vector>

// ============================================================================
// G10AnalogTests — Phase 2 analog engine characterization.
//
// Written BEFORE the analog engine was tuned: these tests define the
// acceptance contracts for the Discrete input stage, the Iron output stage,
// the 2x/4x oversampling anti-alias behavior, the frequency-response safety
// envelope, the transition/latency contract, and the frozen Phase 1 clean
// baseline regression (analog OFF must remain bit-exact).
//
// All measurements go through the real production code: the stages directly
// (isolation characterization) or the real G10Processor (alias, frequency,
// transitions, baseline). No juce_dsp; the FFT is G10Test::Radix2Fft.
// ============================================================================

namespace
{

using APEX::G10::G10Processor;
using APEX::G10::G10Parameter;
using APEX::G10::G10CurveEngineCore;
using APEX::G10::G10DiscreteInputStageCore;
using APEX::G10::G10IronOutputStageCore;
using APEX::G10::G10ButterworthAA;
using APEX::G10::G10OversamplerCore;
using APEX::G10::G10AnalogChainCore;
using APEX::G10::kNumBands;

void setDb (G10Parameter* p, float db)
{
    jassert (p != nullptr);
    p->setValue (p->getValueForText (juce::String (db, 1)));
}

// ---------------------------------------------------------------------------
// Distortion measurement (FFT-based, exact-bin sine windows -> no leakage).
// ---------------------------------------------------------------------------

struct DistortionResult
{
    float fundamentalDb = -300.0f; // measured fundamental relative to input amp
    float h2Db = -300.0f, h3Db = -300.0f, h4Db = -300.0f, h5Db = -300.0f;
    float dcDb = -300.0f;    // DC bin, dB relative to fundamental
    float thdDb = -300.0f;   // H2..H5 combined, dB relative to fundamental (negative)
    float totalDb = -300.0f; // all non-fundamental, non-DC bins (incl. aliases)
    float worstBinDb = -300.0f; // strongest non-fundamental, non-mirror bin
    float worstBinFreq = 0.0f;  // its frequency (Hz)
    float aliasDb = -300.0f;    // strongest bin that is NOT a real harmonic
                                // (n*f below Nyquist): the true alias floor
    float aliasFreq = 0.0f;     // its frequency (Hz)
};

/** Choose a frequency that lands exactly on an FFT bin (no leakage). */
double exactBinFreq (double rate, int fftSize, double target)
{
    const double binHz = rate / fftSize;
    const int bin = (int) std::lround (target / binHz);
    return bin * binHz;
}

DistortionResult measureDistortion (const std::vector<float>& signal, double rate,
                                    double freq, int fftSize, float inputAmp)
{
    DistortionResult result;
    G10Test::Radix2Fft fft (fftSize);
    std::vector<float> real (fftSize, 0.0f);
    std::vector<float> imag (fftSize, 0.0f);
    const int n = std::min ((int) signal.size(), fftSize);
    for (int i = 0; i < n; ++i)
        real[i] = signal[i];
    fft.transform (real.data(), imag.data());

    const double binHz = rate / fftSize;
    auto magAt = [&] (double f) -> double
    {
        const int bin = (int) std::lround (f / binHz);
        if (bin < 0 || bin >= fftSize) return 0.0;
        return std::sqrt ((double) real[bin] * real[bin] + (double) imag[bin] * imag[bin]);
    };

    const double fund = magAt (freq);
    if (fund <= 1e-12)
        return result;

    // Normalize by the expected FFT-bin magnitude for a full-amplitude sine
    // of `inputAmp` over fftSize samples: amp * fftSize / 2.
    const double expectedBin = (double) inputAmp * fftSize / 2.0;
    result.fundamentalDb = (float) (20.0 * std::log10 (std::max (1e-12, fund / expectedBin)));
    result.h2Db = (float) (20.0 * std::log10 (std::max (1e-12, magAt (2.0 * freq) / fund)));
    result.h3Db = (float) (20.0 * std::log10 (std::max (1e-12, magAt (3.0 * freq) / fund)));
    result.h4Db = (float) (20.0 * std::log10 (std::max (1e-12, magAt (4.0 * freq) / fund)));
    result.h5Db = (float) (20.0 * std::log10 (std::max (1e-12, magAt (5.0 * freq) / fund)));
    result.dcDb = (float) (20.0 * std::log10 (std::max (1e-12, magAt (0.0) / fund)));

    const double h2 = magAt (2.0 * freq), h3 = magAt (3.0 * freq);
    const double h4 = magAt (4.0 * freq), h5 = magAt (5.0 * freq);
    const double thd = std::sqrt (h2 * h2 + h3 * h3 + h4 * h4 + h5 * h5);
    result.thdDb = (float) (20.0 * std::log10 (std::max (1e-12, thd / fund)));

    const int fundBin = (int) std::lround (freq / binHz);
    const int mirrorBin = fftSize - fundBin; // conjugate mirror of the fundamental
    const double nyquist = rate * 0.5;
    double total = 0.0;
    double worst = 0.0;
    double worstFreq = 0.0;
    double worstAlias = 0.0;
    double worstAliasFreq = 0.0;
    for (int b = 1; b < fftSize; ++b)
    {
        // Exclude the fundamental AND its conjugate mirror: for a real signal
        // the mirror bin is not an independent component (it carries the same
        // energy as the fundamental), so including it would floor totalDb at
        // ~0 dB regardless of actual distortion/aliasing.
        if (b == fundBin || b == mirrorBin) continue;
        const double m = std::sqrt ((double) real[b] * real[b] + (double) imag[b] * imag[b]);
        total += m * m;
        if (m > worst)
        {
            worst = m;
            worstFreq = b * binHz;
        }
        // Alias-only metric: exclude REAL harmonics (n*f below Nyquist) AND
        // their conjugate mirrors (rate - n*f). A real harmonic appears in
        // the FFT at both +n*f and rate-n*f; the mirror carries the same
        // energy and is not an alias. The alias floor is the strongest bin
        // that is NOT one of these.
        const double bf = b * binHz;
        bool realHarmonic = false;
        for (int h = 2; h * freq < nyquist; ++h)
        {
            if (std::abs (bf - h * freq) < binHz * 0.5
                || std::abs (bf - (rate - h * freq)) < binHz * 0.5)
            {
                realHarmonic = true;
                break;
            }
        }
        if (! realHarmonic && m > worstAlias)
        {
            worstAlias = m;
            worstAliasFreq = bf;
        }
    }
    result.totalDb = (float) (20.0 * std::log10 (std::max (1e-12, std::sqrt (total) / fund)));
    result.worstBinDb = (float) (20.0 * std::log10 (std::max (1e-12, worst / fund)));
    result.worstBinFreq = (float) worstFreq;
    result.aliasDb = (float) (20.0 * std::log10 (std::max (1e-12, worstAlias / fund)));
    result.aliasFreq = (float) worstAliasFreq;
    return result;
}

/** Process a continuous sine through a stage and measure the steady-state
    distortion of the last fftSize samples (an exact integer number of
    cycles, so the FFT window has no leakage). */
template <typename Stage>
DistortionResult measureStageDistortion (Stage& stage, double rate, int blockSize,
                                         double freq, float amp, int fftSize,
                                         double settleSeconds = 0.5)
{
    const int settle = (int) (rate * settleSeconds);
    const int total = settle + fftSize;
    juce::AudioBuffer<float> buf (1, blockSize);
    std::vector<float> out;
    out.reserve (total);
    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (blockSize, total - pos);
        buf.clear();
        for (int i = 0; i < n; ++i)
            buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * freq * (pos + i) / rate));
        stage.process (buf, 1, n);
        for (int i = 0; i < n; ++i)
            out.push_back (buf.getSample (0, i));
        pos += n;
    }
    std::vector<float> window (out.end() - fftSize, out.end());
    return measureDistortion (window, rate, freq, fftSize, amp);
}

/** Same, through a real processor (mono). */
DistortionResult measureProcessorDistortion (juce::AudioProcessor& proc, double rate,
                                             int blockSize, double freq, float amp,
                                             int fftSize, double settleSeconds = 0.5)
{
    const int settle = (int) (rate * settleSeconds);
    const int total = settle + fftSize;
    juce::AudioBuffer<float> buf (1, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;
    out.reserve (total);
    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (blockSize, total - pos);
        buf.clear();
        for (int i = 0; i < n; ++i)
            buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * freq * (pos + i) / rate));
        proc.processBlock (buf, midi);
        for (int i = 0; i < n; ++i)
            out.push_back (buf.getSample (0, i));
        pos += n;
    }
    std::vector<float> window (out.end() - fftSize, out.end());
    return measureDistortion (window, rate, freq, fftSize, amp);
}

/** Process a mono signal through a stage, returning the output samples. */
template <typename Stage>
std::vector<float> processStageSignal (Stage& stage, const std::vector<float>& in, int blockSize)
{
    juce::AudioBuffer<float> buf (1, blockSize);
    std::vector<float> out;
    out.reserve (in.size());
    int pos = 0;
    while (pos < (int) in.size())
    {
        const int n = std::min (blockSize, (int) in.size() - pos);
        buf.clear();
        for (int i = 0; i < n; ++i)
            buf.setSample (0, i, in[pos + i]);
        stage.process (buf, 1, n);
        for (int i = 0; i < n; ++i)
            out.push_back (buf.getSample (0, i));
        pos += n;
    }
    return out;
}

} // namespace

// ============================================================================
// Isolation characterization of the Discrete and Iron stages.
// ============================================================================

class G10AnalogIsolationTests final : public juce::UnitTest
{
public:
    G10AnalogIsolationTests() : juce::UnitTest ("G10.Analog.Isolation", "APEX.G10") {}

    void runTest() override
    {
        testDiscrete();
        testIron();
    }

private:
    static constexpr double kRate = 48000.0;
    static constexpr int kBlock = 512;
    static constexpr int kFft = 8192;

    void testDiscrete()
    {
        beginTest ("Discrete stage isolation");

        G10DiscreteInputStageCore stage;
        stage.prepare (kRate);

        // 1. Near-identity at low drive: -40 dBFS sine -> ~0 dB gain, no THD.
        //    Phase 2B contract: totalDb < -65 (measured -69.0 at -40 dBFS;
        //    the floor is the intentional H2 from the controlled asymmetry,
        //    -72 dBc. 4 dB margin: catches a kAsymmetry doubling or any
        //    low-level nonlinear regression; -60 would hide a 6 dB one).
        {
            const double freq = exactBinFreq (kRate, kFft, 1000.0);
            auto r = measureStageDistortion (stage, kRate, kBlock, freq, 0.01f, kFft);
            expectWithinAbsoluteError (r.fundamentalDb, 0.0f, 0.5f, "low-drive gain ~ 0 dB");
            expect (r.totalDb < -65.0f, "low-drive distortion negligible");
        }

        // 2. Transfer curve: monotonic, continuous, bounded, finite, with
        //    BOUNDED asymmetry (controlled musical asymmetry, not perfect
        //    odd symmetry). The residual-only DC blocker removes the DC of
        //    the even residual, so the DC-sweep transfer is near-symmetric;
        //    the asymmetry lives in the harmonic character (checked below).
        {
            std::vector<float> in;
            for (int i = -100; i <= 100; ++i)
                in.push_back (i / 100.0f);
            auto out = processStageSignal (stage, in, kBlock);
            bool monotonic = true;
            bool finite = true;
            bool continuous = true;
            for (int i = 1; i < (int) out.size(); ++i)
            {
                if (out[i] < out[i - 1]) monotonic = false;
                if (! std::isfinite (out[i])) finite = false;
                // No crossover-distortion discontinuity: the transfer must
                // not jump by more than the input step (0.01) + margin.
                if (std::abs (out[i] - out[i - 1]) > 0.02f) continuous = false;
            }
            expect (monotonic, "transfer curve must be monotonic");
            expect (continuous, "transfer curve must be continuous (no crossover-distortion discontinuity)");
            expect (finite, "transfer curve must be finite");
            expect (std::abs (out[200]) <= 1.0f + 1.0e-6f, "output bounded by 1.0");

            // Bounded asymmetry: the residual-only DC blocker removes the DC
            // of the even residual, so the SETTLED transfer is exactly
            // y = x for a constant input: no DC offset, no one-sided
            // clipping, no foldback. (The transient sweep would measure the
            // blocker's lag, not the transfer: near zero the output is
            // dominated by the accumulated dc state, which is a test
            // artifact. The musical asymmetry lives in the harmonics and is
            // checked in test 4 below.)
            double maxAsymDb = 0.0;
            for (float a : { 0.01f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                std::vector<float> holdP (48000, a);
                auto yp = processStageSignal (stage, holdP, kBlock);
                std::vector<float> holdN (48000, -a);
                auto yn = processStageSignal (stage, holdN, kBlock);
                // Settled transfer is exactly linear: the blocker has
                // converged to the residual DC (tau ~ 79.6 ms; a 1 s hold
                // leaves < 1e-3 residual error).
                expectWithinAbsoluteError (yp.back(), a, 1.0e-3f,
                                           "settled transfer must be exactly linear (no DC offset)");
                const double ypS = std::abs (yp.back());
                const double ynS = std::abs (yn.back());
                if (ypS > 1e-12 && ynS > 1e-12)
                    maxAsymDb = std::max (maxAsymDb,
                                          std::abs (20.0 * std::log10 (ypS / ynS)));
            }
            expect (maxAsymDb < 1.5, "settled transfer asymmetry must be bounded (controlled musical asymmetry)");
        }

        // 3. THD vs level: progressive, monotonic, no discontinuities.
        {
            const double freq = exactBinFreq (kRate, kFft, 1000.0);
            float prevThd = -300.0f;
            for (float db : { -20.0f, -12.0f, -6.0f, 0.0f })
            {
                const float amp = std::pow (10.0f, db / 20.0f);
                auto r = measureStageDistortion (stage, kRate, kBlock, freq, amp, kFft);
                expect (r.thdDb > prevThd, "THD must increase with drive");
                prevThd = r.thdDb;
            }
        }

        // 4. Controlled musical asymmetry at strong drive (0 dBFS, measured
        //    D1B: H2=-33.6, H3=-30.5, H4=-57.3, H5=-52.1 dBc): H2 and H3
        //    intentionally exist; neither is excessively strong; H4/H5 stay
        //    clearly subordinate to the primary low-order character (>= 10 dB
        //    below the stronger of H2/H3); NO universal "H3 > H2" law.
        {
            const double freq = exactBinFreq (kRate, kFft, 1000.0);
            auto r = measureStageDistortion (stage, kRate, kBlock, freq, 1.0f, kFft);
            expect (r.h2Db > -60.0f, "H2 must intentionally exist (controlled musical asymmetry)");
            expect (r.h3Db > -60.0f, "H3 must intentionally exist");
            expect (r.h2Db < -20.0f, "H2 must not be excessively strong");
            expect (r.h3Db < -20.0f, "H3 must not be excessively strong");
            const float primary = std::max (r.h2Db, r.h3Db);
            expect (r.h4Db < primary - 10.0f, "H4 must stay clearly subordinate to the primary character");
            expect (r.h5Db < primary - 10.0f, "H5 must stay clearly subordinate to the primary character");
        }

        // 5. DC: the residual-only 2 Hz blocker strongly suppresses DC
        //    (measured: -95.1 dBc at 0 dBFS steady sine).
        {
            const double freq = exactBinFreq (kRate, kFft, 1000.0);
            auto r = measureStageDistortion (stage, kRate, kBlock, freq, 1.0f, kFft);
            expect (r.dcDb < -60.0f, "DC must be strongly suppressed by the residual blocker");
        }

        // 6. Silence -> silence (reset first: the residual DC blocker
        //    retains state from the previous 0 dBFS measurement, which
        //    would otherwise leak a decaying DC into the output).
        {
            stage.reset();
            std::vector<float> in (4096, 0.0f);
            auto out = processStageSignal (stage, in, kBlock);
            bool silent = true;
            for (float v : out)
                if (v != 0.0f) silent = false;
            expect (silent, "silence in -> silence out");
        }

        // 7. Denormal input -> finite output.
        {
            std::vector<float> in (4096, 1.0e-30f);
            auto out = processStageSignal (stage, in, kBlock);
            bool finite = true;
            for (float v : out)
                if (! std::isfinite (v)) finite = false;
            expect (finite, "denormal input must produce finite output");
        }

        // 8. Extreme inputs -> finite, compressed (|y| <= |x|: the saturation-
        //    excess form compresses but is not bounded by 1.0).
        {
            std::vector<float> in;
            for (int i = 0; i < 4096; ++i)
                in.push_back ((i % 2 == 0) ? 10.0f : -10.0f);
            auto out = processStageSignal (stage, in, kBlock);
            bool finite = true;
            for (float v : out)
                if (! std::isfinite (v) || std::abs (v) > 10.0f + 1.0e-6f) finite = false;
            expect (finite, "extreme input must produce finite bounded output");
        }

        // 9. Mono/stereo determinism. The stage is stateful (residual DC
        //    blocker), so both runs must start from the canonical reset
        //    state: the mono run leaves dcState[0] nonzero, which would
        //    otherwise make stereo channel 0 diverge from channel 1.
        {
            std::vector<float> in;
            for (int i = 0; i < 8192; ++i)
                in.push_back (0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate));

            stage.reset();
            auto mono = processStageSignal (stage, in, kBlock);

            stage.reset();
            juce::AudioBuffer<float> stereoBuf (2, kBlock);
            std::vector<float> stereoOut;
            int pos = 0;
            while (pos < (int) in.size())
            {
                const int n = std::min (kBlock, (int) in.size() - pos);
                stereoBuf.clear();
                for (int i = 0; i < n; ++i)
                {
                    stereoBuf.setSample (0, i, in[pos + i]);
                    stereoBuf.setSample (1, i, in[pos + i]);
                }
                stage.process (stereoBuf, 2, n);
                for (int i = 0; i < n; ++i)
                {
                    stereoOut.push_back (stereoBuf.getSample (0, i));
                    if (stereoBuf.getSample (0, i) != stereoBuf.getSample (1, i))
                        expect (false, "stereo channels must match");
                }
                pos += n;
            }
            bool match = (mono.size() == stereoOut.size());
            if (match)
                for (int i = 0; i < (int) mono.size(); ++i)
                    if (mono[i] != stereoOut[i]) { match = false; break; }
            expect (match, "mono output must match stereo left channel");
        }
    }

    void testIron()
    {
        beginTest ("Iron stage isolation");

        G10IronOutputStageCore stage;
        stage.prepare (kRate);

        // 1. Near-identity at low drive.
        //    Phase 2B contract: totalDb < -100 (measured -104.6 at -40 dBFS;
        //    the floor is the intentional H3, -107.6 dBc. 4.6 dB margin:
        //    catches a kSaturation doubling; the old -80 bound would let a
        //    24 dB regression pass unnoticed).
        {
            const double freq = exactBinFreq (kRate, kFft, 1000.0);
            auto r = measureStageDistortion (stage, kRate, kBlock, freq, 0.01f, kFft);
            expectWithinAbsoluteError (r.fundamentalDb, 0.0f, 0.5f, "low-drive gain ~ 0 dB");
            expect (r.totalDb < -100.0f, "low-drive distortion negligible");
        }

        // 2. Transfer curve: monotonic, odd-symmetric, finite.
        {
            std::vector<float> in;
            for (int i = -100; i <= 100; ++i)
                in.push_back (i / 100.0f);
            auto out = processStageSignal (stage, in, kBlock);
            bool monotonic = true;
            bool finite = true;
            for (int i = 1; i < (int) out.size(); ++i)
            {
                if (out[i] < out[i - 1]) monotonic = false;
                if (! std::isfinite (out[i])) finite = false;
            }
            expect (monotonic, "transfer curve must be monotonic");
            expect (finite, "transfer curve must be finite");
        }

        // 3. THD vs level: progressive, monotonic.
        {
            const double freq = exactBinFreq (kRate, kFft, 1000.0);
            float prevThd = -300.0f;
            for (float db : { -20.0f, -12.0f, -6.0f, 0.0f })
            {
                const float amp = std::pow (10.0f, db / 20.0f);
                auto r = measureStageDistortion (stage, kRate, kBlock, freq, amp, kFft);
                expect (r.thdDb > prevThd, "THD must increase with drive");
                prevThd = r.thdDb;
            }
        }

        // 4. Odd symmetry: H2 suppressed.
        {
            const double freq = exactBinFreq (kRate, kFft, 1000.0);
            auto r = measureStageDistortion (stage, kRate, kBlock, freq, 1.0f, kFft);
            expect (r.h2Db < -80.0f, "even harmonics must be suppressed");
        }

        // 5. Frequency dependence: the low-frequency emphasis means the
        //    saturation is stronger at 100 Hz than at 10 kHz.
        {
            const double fLow = exactBinFreq (kRate, kFft, 100.0);
            const double fHigh = exactBinFreq (kRate, kFft, 10000.0);
            auto rLow = measureStageDistortion (stage, kRate, kBlock, fLow, 1.0f, kFft);
            auto rHigh = measureStageDistortion (stage, kRate, kBlock, fHigh, 1.0f, kFft);
            expect (rLow.thdDb > rHigh.thdDb, "low-frequency saturation must exceed high-frequency");
        }

        // 6. Silence -> silence (reset first: the iron lowpass retains
        //    state from the previous test block).
        {
            stage.reset();
            std::vector<float> in (4096, 0.0f);
            auto out = processStageSignal (stage, in, kBlock);
            bool silent = true;
            for (float v : out)
                if (v != 0.0f) silent = false;
            expect (silent, "silence in -> silence out");
        }

        // 7. Finite extremes.
        {
            std::vector<float> in (4096, 10.0f);
            auto out = processStageSignal (stage, in, kBlock);
            bool finite = true;
            for (float v : out)
                if (! std::isfinite (v)) finite = false;
            expect (finite, "extreme input must produce finite output");
        }
    }
};

static G10AnalogIsolationTests g10AnalogIsolationTests;

// ============================================================================
// Aliasing: 1x diagnostic vs 2x NORMAL vs 4x HQ.
// ============================================================================

class G10AnalogAliasTests final : public juce::UnitTest
{
public:
    G10AnalogAliasTests() : juce::UnitTest ("G10.Analog.Alias", "APEX.G10") {}

    void runTest() override
    {
        beginTest ("Alias suppression: 1x diagnostic, 2x NORMAL, 4x HQ");

        const double rate = 48000.0;
        const int block = 512;
        const int fft = 16384;
        const float amp = 0.5f; // -6 dBFS: strong saturation, strong harmonics

        // Contract note (updated for the xL unity-gain normalization, the
        // measured alias floor, and the Phase 2 factor-specific AA design):
        //   - 1x diagnostic: strong aliasing (aliasDb > -45 dB) proves the
        //     test is meaningful (the stages really do fold without AA).
        //   - 2x and 4x: aliasDb < -30 dB at every test frequency (the gate).
        //   - Mid band (10 kHz): 2x must beat 1x by >= 10 dB (the
        //     oversampling's real work: the folded H3 lands in the down-AA
        //     stopband).
        //   - 10 kHz: 4x HQ must NOT regress against 2x NORMAL. The HQ AA is
        //     a 10th-order Butterworth at 1.0208333x base Nyquist (24.5 kHz
        //     at 48 kHz base) vs the frozen 2x 8th-order at 1.1x; the
        //     bilinear tan-warp makes the SAME absolute cutoff ~6.5 dB
        //     weaker at 30 kHz at 4x, so the HQ design compensates with a
        //     lower cutoff and higher order. Measured: 4x -59.5 dB vs 2x
        //     -57.5 dB at 10 kHz.
        //   - 20 kHz: the residual is dominated by up-AA image
        //     intermodulation (the 28 kHz zero-stuff image mixes with the
        //     fundamental's cubic to land at 12 kHz INSIDE the OS passband,
        //     where the down-AA cannot remove it), not by high-order
        //     harmonic wrap (H9/H15 wraps are ~-140 dB). The HQ design
        //     suppresses the 28 kHz image harder, so HQ must improve over
        //     NORMAL here. Measured: 4x -45.0 dB vs 2x -40.8 dB.
        //   - 5 kHz: H5 = 25 kHz sits in the down-AA passband, so its fold
        //     is intrinsic passband-edge alias, essentially floor-limited
        //     and identical at 1x/2x/4x (sanity case, no artificial
        //     improvement required).
        const double testFreqs[] = { 5000.0, 10000.0, 20000.0 };

        for (double target : testFreqs)
        {
            const double freq = exactBinFreq (rate, fft, target);

            // 1x diagnostic: the saturation stages at the base rate (no AA).
            DistortionResult r1x;
            {
                G10DiscreteInputStageCore discrete;
                G10IronOutputStageCore iron;
                discrete.prepare (rate);
                iron.prepare (rate);

                const int settle = (int) (rate * 0.5);
                const int total = settle + fft;
                juce::AudioBuffer<float> buf (1, block);
                std::vector<float> out;
                out.reserve (total);
                int pos = 0;
                while (pos < total)
                {
                    const int n = std::min (block, total - pos);
                    buf.clear();
                    for (int i = 0; i < n; ++i)
                        buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                     * freq * (pos + i) / rate));
                    discrete.process (buf, 1, n);
                    iron.process (buf, 1, n);
                    for (int i = 0; i < n; ++i)
                        out.push_back (buf.getSample (0, i));
                    pos += n;
                }
                std::vector<float> window (out.end() - fft, out.end());
                r1x = measureDistortion (window, rate, freq, fft, amp);
            }
            logMessage (juce::String ("f=") + juce::String (target, 0)
                        + " 1x diagnostic: total=" + juce::String (r1x.totalDb, 1)
                        + " dB  alias=" + juce::String (r1x.aliasDb, 1) + " dB @ "
                        + juce::String (r1x.aliasFreq, 0) + " Hz");
            // 1x diagnostic: strong aliasing where the stages' harmonics
            // fold into the audible band (10 kHz: H3 -> 18 kHz; 20 kHz:
            // H9 -> 12 kHz). At 5 kHz the harmonics are weak (H5 = 25 kHz
            // at ~-71 dB), so the 1x floor is ~-73 dB there — the intrinsic
            // passband-edge fold, identical at 1x/2x/4x (see contract note).
            if (target != 5000.0)
                expect (r1x.aliasDb > -45.0f, "1x diagnostic must show strong aliasing");

            // 2x NORMAL and 4x HQ through the real processor. Quality is
            // internal: NORMAL is the realtime mode, HQ is the offline mode
            // (the host sets the non-realtime flag before prepareToPlay).
            auto proc2x = G10Test::makePreparedProcessor (rate, block, false);
            auto r2x = measureProcessorDistortion (*proc2x, rate, block, freq, amp, fft);

            auto proc4x = G10Test::makePreparedProcessor (rate, block, true);
            auto r4x = measureProcessorDistortion (*proc4x, rate, block, freq, amp, fft);

            logMessage (juce::String ("f=") + juce::String (target, 0)
                        + " 2x NORMAL: total=" + juce::String (r2x.totalDb, 1)
                        + " dB  alias=" + juce::String (r2x.aliasDb, 1) + " dB @ "
                        + juce::String (r2x.aliasFreq, 0) + " Hz  fund="
                        + juce::String (r2x.fundamentalDb, 1));
            logMessage (juce::String ("f=") + juce::String (target, 0)
                        + " 4x HQ: total=" + juce::String (r4x.totalDb, 1)
                        + " dB  alias=" + juce::String (r4x.aliasDb, 1) + " dB @ "
                        + juce::String (r4x.aliasFreq, 0) + " Hz  fund="
                        + juce::String (r4x.fundamentalDb, 1));

            // New contract: both rates must hold the ALIAS floor below -30 dB
            // (real harmonics and their mirrors are excluded: they are
            // legitimate saturation products present at any rate).
            expect (r2x.aliasDb < -30.0f, "2x alias floor must stay below -30 dB");
            expect (r4x.aliasDb < -30.0f, "4x alias floor must stay below -30 dB");
            // Mid band: the oversampling must do its real work (2x beats 1x).
            if (target == 10000.0)
            {
                expect (r2x.aliasDb < r1x.aliasDb - 10.0f,
                        "2x must beat 1x by >= 10 dB at 10 kHz");
                // 4x HQ must NOT regress against 2x NORMAL at the 10 kHz
                // stress case (measured: -59.5 vs -57.5 dB; +0.5 dB
                // engineering tolerance).
                expect (r4x.aliasDb < r2x.aliasDb + 0.5f,
                        "4x HQ must not regress vs 2x NORMAL at 10 kHz");
            }
            // 20 kHz: HQ must improve over NORMAL (the up-AA image
            // intermodulation case, measured -45.0 vs -40.8 dB; assert at
            // least 1 dB of improvement as the engineering margin).
            if (target == 20000.0)
                expect (r4x.aliasDb < r2x.aliasDb - 1.0f,
                        "4x HQ must improve over 2x NORMAL at 20 kHz");
        }
    }
};

static G10AnalogAliasTests g10AnalogAliasTests;

// ============================================================================
// Frequency response safety (analog ON, NORMAL and HQ).
// ============================================================================

class G10AnalogFreqSafetyTests final : public juce::UnitTest
{
public:
    G10AnalogFreqSafetyTests() : juce::UnitTest ("G10.Analog.FreqSafety", "APEX.G10") {}

    void runTest() override
    {
        beginTest ("Frequency response safety: analog ON, NORMAL and HQ");

        const double rate = 48000.0;
        const int block = 1024;
        const int fft = 16384;
        const float amp = 0.01f; // -40 dBFS: linear region, measures the AA response

        const double freqs[] = { 20.0, 31.0, 63.0, 125.0, 250.0, 500.0,
                                 1000.0, 2000.0, 4000.0, 8000.0, 16000.0, 20000.0 };

        for (int quality = 0; quality <= 1; ++quality)
        {
            // Quality is internal: 0 = realtime NORMAL (2x), 1 = offline HQ
            // (4x), selected by the host's non-realtime flag.
            auto proc = G10Test::makePreparedProcessor (rate, block, quality == 1);

            for (double f : freqs)
            {
                const double freq = exactBinFreq (rate, fft, f);
                auto r = measureProcessorDistortion (*proc, rate, block, freq, amp, fft);
                expect (std::isfinite (r.fundamentalDb), "response must be finite at " + juce::String (f));
                expectWithinAbsoluteError (r.fundamentalDb, 0.0f, 3.0f,
                                           "response within +-3 dB at " + juce::String (f)
                                           + " (quality " + juce::String (quality) + ")");
            }
        }
    }
};

static G10AnalogFreqSafetyTests g10AnalogFreqSafetyTests;

// ============================================================================
// Transitions (analog on/off, quality) and latency.
// ============================================================================

class G10AnalogTransitionTests final : public juce::UnitTest
{
public:
    G10AnalogTransitionTests() : juce::UnitTest ("G10.Analog.Transition", "APEX.G10") {}

    void runTest() override
    {
        testLatency();
        testLegacyAnalogInert();
        testQualityModeSwitchNoClick();
    }

private:
    void testLatency()
    {
        beginTest ("Latency is zero in every mode");

        auto proc = G10Test::makePreparedProcessor (48000.0, 512);
        expectEquals (proc->getLatencySamples(), 0, "latency zero (canonical realtime)");
        // Offline (HQ 4x) mode: the host sequence is releaseResources ->
        // setNonRealtime(true) -> prepareToPlay.
        proc->releaseResources();
        proc->setNonRealtime (true);
        proc->prepareToPlay (48000.0, 512);
        expectEquals (proc->getLatencySamples(), 0, "latency zero (offline HQ)");
        proc->releaseResources();
        proc->setNonRealtime (false);
        proc->prepareToPlay (48000.0, 512);
        expectEquals (proc->getLatencySamples(), 0, "latency zero (back to realtime)");
    }

    void testLegacyAnalogInert()
    {
        beginTest ("Legacy analog value is inert (no click, no change)");

        const double rate = 48000.0;
        const int block = 64;
        auto proc = G10Test::makePreparedProcessor (rate, block);
        auto ref = G10Test::makePreparedProcessor (rate, block);
        auto* param = G10Test::findParam (*proc, "g10.analog");

        const int total = (int) (rate * 0.5);
        const int toggleAt = (int) (rate * 0.25);
        juce::AudioBuffer<float> bufA (1, block);
        juce::AudioBuffer<float> bufB (1, block);
        juce::MidiBuffer midi;
        bool identical = true;
        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (block, total - pos);
            bufA.clear();
            bufB.clear();
            for (int i = 0; i < n; ++i)
            {
                const float v = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * 440.0 * (pos + i) / rate);
                bufA.setSample (0, i, v);
                bufB.setSample (0, i, v);
            }
            if (pos >= toggleAt && pos < toggleAt + block)
                param->setValue (1.0f);
            proc->processBlock (bufA, midi);
            ref->processBlock (bufB, midi);
            for (int i = 0; i < n; ++i)
                if (bufA.getSample (0, i) != bufB.getSample (0, i))
                    identical = false;
            pos += n;
        }
        expect (identical, "legacy analog toggle must not change the output");
    }

    void testQualityModeSwitchNoClick()
    {
        beginTest ("Quality mode switch (realtime<->offline) is click-free");

        const double rate = 48000.0;
        const int block = 64;
        auto proc = G10Test::makePreparedProcessor (rate, block);

        const int total = (int) (rate * 0.5);
        const int offlineAt = (int) (rate * 0.25);
        const int realtimeAt = (int) (rate * 0.4);
        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        float prevOut = 0.0f;
        float maxDelta = 0.0f;
        bool finite = true;
        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (block, total - pos);
            buf.clear();
            for (int i = 0; i < n; ++i)
                buf.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                               * 440.0 * (pos + i) / rate));
            if (pos >= offlineAt && pos < offlineAt + block)
                proc->setNonRealtime (true);  // host: offline render begins
            if (pos >= realtimeAt && pos < realtimeAt + block)
                proc->setNonRealtime (false); // host: back to realtime
            proc->processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample (0, i);
                if (! std::isfinite (v)) finite = false;
                maxDelta = std::max (maxDelta, std::abs (v - prevOut));
                prevOut = v;
            }
            pos += n;
        }
        expect (finite, "quality mode switch output must be finite");
        expect (maxDelta < 0.05f, "quality mode switch must not click");
    }
};

static G10AnalogTransitionTests g10AnalogTransitionTests;

// ============================================================================
// Canonical identity baseline: the APEX color is ALWAYS active. The legacy
// analog=0 default must NOT disable the canonical path, and the legacy
// analog/quality values must be inert (bit-exact to the defaults).
// ============================================================================

class G10AnalogCleanBaselineTests final : public juce::UnitTest
{
public:
    G10AnalogCleanBaselineTests() : juce::UnitTest ("G10.Analog.CleanBaseline", "APEX.G10") {}

    void runTest() override
    {
        beginTest ("Canonical identity: legacy analog=0 must NOT disable the APEX color");

        const double rate = 48000.0;
        const int block = 1024;

        // The canonical processor (legacy defaults: analog=0, quality=0)
        // must run the analog chain: its output must DIFFER from the frozen
        // clean engine (the color is active even with legacy analog=0).
        auto proc = G10Test::makePreparedProcessor (rate, block);
        setDb (G10Test::findParam (*proc, "g10.band31"), 6.0f);
        setDb (G10Test::findParam (*proc, "g10.band1k"), -3.0f);
        setDb (G10Test::findParam (*proc, "g10.input"), 2.0f);
        setDb (G10Test::findParam (*proc, "g10.output"), -1.0f);

        // Bare frozen engine with identical targets.
        G10CurveEngineCore engine;
        engine.prepare (rate, block, 2);
        engine.setInputTargetDb (2.0f);
        engine.setBandTargetGainDb (0, 6.0f);
        engine.setBandTargetGainDb (5, -3.0f);
        engine.setOutputTargetDb (-1.0f);
        engine.setBypassTarget (false);

        // Settle both identically (silence, same sample count).
        G10Test::settleProcessor (*proc, rate, block, 0.5);
        {
            juce::AudioBuffer<float> buf (2, block);
            float* chans[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
            for (int i = 0; i < (int) (rate * 0.5); i += block)
            {
                buf.clear();
                engine.processBlock (chans, 2, block);
            }
        }

        juce::AudioBuffer<float> bufA (2, block);
        juce::AudioBuffer<float> bufB (2, block);
        juce::MidiBuffer midi;
        bool differs = false;
        float maxDelta = 0.0f;
        int firstCh = -1, firstIdx = -1;
        for (int pos = 0; pos < 8192; pos += block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = (float) (0.5 + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                                   * 440.0 * (pos + i) / rate));
                    bufA.setSample (ch, i, v);
                    bufB.setSample (ch, i, v);
                }
            proc->processBlock (bufA, midi);
            float* chans[2] = { bufB.getWritePointer (0), bufB.getWritePointer (1) };
            engine.processBlock (chans, 2, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float d = std::abs (bufA.getSample (ch, i) - bufB.getSample (ch, i));
                    if (d > maxDelta) { maxDelta = d; firstCh = ch; firstIdx = pos + i; }
                    if (bufA.getSample (ch, i) != bufB.getSample (ch, i))
                        differs = true;
                }
        }
        if (! differs)
            logMessage ("DIAG: canonical output matched the frozen clean engine — the color is NOT active");
        else
            logMessage (juce::String ("DIAG canonical-vs-clean: firstCh=") + juce::String (firstCh)
                        + " firstIdx=" + juce::String (firstIdx)
                        + " maxDelta=" + juce::String (maxDelta, 8));
        expect (differs, "legacy analog=0 must NOT disable the canonical APEX color");

        // Legacy values are inert: analog=1/quality=1 must be bit-exact to
        // the legacy defaults (both run the same canonical realtime path).
        // A FRESH canonical processor is used for this comparison: the D1B
        // DC blocker (2 Hz, tau ~79.6 ms) retains state across blocks, so
        // `proc` above carries history that a 0.5 s settle cannot fully
        // erase. Both fresh processors are settled identically, so the
        // comparison isolates the parameter policy.
        auto canonical2 = G10Test::makePreparedProcessor (rate, block);
        setDb (G10Test::findParam (*canonical2, "g10.band31"), 6.0f);
        setDb (G10Test::findParam (*canonical2, "g10.band1k"), -3.0f);
        setDb (G10Test::findParam (*canonical2, "g10.input"), 2.0f);
        setDb (G10Test::findParam (*canonical2, "g10.output"), -1.0f);
        auto legacy = G10Test::makePreparedProcessor (rate, block);
        setDb (G10Test::findParam (*legacy, "g10.band31"), 6.0f);
        setDb (G10Test::findParam (*legacy, "g10.band1k"), -3.0f);
        setDb (G10Test::findParam (*legacy, "g10.input"), 2.0f);
        setDb (G10Test::findParam (*legacy, "g10.output"), -1.0f);
        G10Test::findParam (*legacy, "g10.analog")->setValue (1.0f);
        G10Test::findParam (*legacy, "g10.quality")->setValue (1.0f);

        G10Test::settleProcessor (*canonical2, rate, block, 0.5);
        G10Test::settleProcessor (*legacy, rate, block, 0.5);

        bool identical = true;
        for (int pos = 0; pos < 8192; pos += block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = (float) (0.5 + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                                   * 440.0 * (pos + i) / rate));
                    bufA.setSample (ch, i, v);
                    bufB.setSample (ch, i, v);
                }
            canonical2->processBlock (bufA, midi);
            legacy->processBlock (bufB, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (bufA.getSample (ch, i) != bufB.getSample (ch, i))
                        identical = false;
        }
        expect (identical, "legacy analog/quality values must not change the sound");
    }
};

static G10AnalogCleanBaselineTests g10AnalogCleanBaselineTests;

// ============================================================================
// Chain spectrum and impulse-gain diagnostic (permanent regression evidence).
// ============================================================================

class G10AnalogDiagTests final : public juce::UnitTest
{
public:
    G10AnalogDiagTests() : juce::UnitTest ("G10.Analog.Diag", "APEX.G10") {}

    void runTest() override
    {
        beginTest ("Diagnostic: chain spectrum and impulse gain");

        const double rate = 48000.0;
        const int block = 1024;
        const int fft = 16384;

        for (int quality = 0; quality <= 1; ++quality)
        {
            for (double target : { 1000.0, 8000.0, 20000.0 })
            {
                // Quality is internal: 0 = realtime (2x), 1 = offline (4x).
                auto proc = G10Test::makePreparedProcessor (rate, block, quality == 1);

                const double freq = exactBinFreq (rate, fft, target);
                const float amp = 0.01f;
                const int settle = (int) (rate * 0.5);
                const int total = settle + fft;
                juce::AudioBuffer<float> buf (1, block);
                juce::MidiBuffer midi;
                std::vector<float> out;
                out.reserve (total);
                int pos = 0;
                while (pos < total)
                {
                    const int n = std::min (block, total - pos);
                    buf.clear();
                    for (int i = 0; i < n; ++i)
                        buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                     * freq * (pos + i) / rate));
                    proc->processBlock (buf, midi);
                    for (int i = 0; i < n; ++i)
                        out.push_back (buf.getSample (0, i));
                    pos += n;
                }

                G10Test::Radix2Fft f (fft);
                std::vector<float> real (fft, 0.0f), imag (fft, 0.0f);
                for (int i = 0; i < fft; ++i)
                    real[i] = out[settle + i];
                f.transform (real.data(), imag.data());

                const double binHz = rate / fft;
                const int fundBin = (int) std::lround (freq / binHz);
                const double fund = std::sqrt ((double) real[fundBin] * real[fundBin]
                                             + (double) imag[fundBin] * imag[fundBin]);
                const double ideal = amp * fft / 2.0;
                const double gainDb = 20.0 * std::log10 (std::max (1e-12, fund / ideal));

                // Top 6 non-fundamental bins. Only search the lower half
                // (b < fft/2): for a real signal the upper half is the
                // conjugate mirror of the lower half, so bins >= fft/2 are
                // never independent components.
                struct Bin { int b; double m; };
                std::vector<Bin> top;
                for (int b = 1; b < fft / 2; ++b)
                {
                    if (b == fundBin) continue;
                    const double m = std::sqrt ((double) real[b] * real[b] + (double) imag[b] * imag[b]);
                    top.push_back ({ b, m });
                }
                std::sort (top.begin(), top.end(), [] (const Bin& a, const Bin& b) { return a.m > b.m; });

                juce::String msg;
                msg << "q=" << quality << " f=" << target << "Hz gain=" << gainDb << " dB";
                for (int i = 0; i < 6 && i < (int) top.size(); ++i)
                {
                    const double db = 20.0 * std::log10 (std::max (1e-12, top[i].m / fund));
                    msg << " | bin=" << (int) top[i].b << " (" << (top[i].b * binHz) << " Hz) " << db << " dB";
                }
                logMessage (msg);
            }
        }

        // Impulse-based chain gain (correctly normalized).
        for (int quality = 0; quality <= 1; ++quality)
        {
            // Quality is internal: 0 = realtime (2x), 1 = offline (4x).
            auto proc = G10Test::makePreparedProcessor (rate, block, quality == 1);
            for (double target : { 1000.0, 8000.0, 20000.0 })
            {
                const float g = G10Test::measureGainDb (*proc, rate, block, target);
                logMessage (juce::String ("impulse q=") + juce::String (quality)
                            + " f=" + juce::String (target) + " gain=" + juce::String (g) + " dB");
            }
        }
    }
};

static G10AnalogDiagTests g10AnalogDiagTests;

// ============================================================================
// Oversampler diagnostic — AA coefficients, AA isolation, linear up/down
// round trip (no Discrete, no Iron, no engine), unity/drive contract,
// transitions, quality endpoints, and numerical safety (permanent
// regression).
// ============================================================================

class G10AnalogOsDiagTests final : public juce::UnitTest
{
public:
    G10AnalogOsDiagTests() : juce::UnitTest ("G10.Analog.OsDiag", "APEX.G10") {}

    void runTest() override
    {
        testAaCoefficients();
        testAaIsolation();
        testLinearRoundTrip();
        testFullChainFreshVsReused();
        testSameFreqReuse();
        testStageByStageGainTrace();
        testBareVsFullChain();
        testSelectiveStateReset();
        testChainPhaseAtHighFreq();
        testTransitionDuration();
        testQualityEndpoints();
        testNumericalSafety();
    }

private:
    static constexpr double kRate = 48000.0;
    static constexpr int kBlock = 1024;
    static constexpr int kFft = 16384;
    static constexpr float kAmp = 0.01f;

    // -----------------------------------------------------------------------
    // Shared measurement helpers.
    // -----------------------------------------------------------------------

    /** Steady-state gain (dB) of an exact-bin sine through a prepared
        processor: 0.5 s settle, then an fft-sample FFT window. */
    double measureProcessorGain (juce::AudioProcessor& proc, double freq) const
    {
        const int settle = (int) (kRate * 0.5);
        const int total = settle + kFft;
        juce::AudioBuffer<float> buf (1, kBlock);
        juce::MidiBuffer midi;
        std::vector<float> out;
        out.reserve (total);
        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (kBlock, total - pos);
            buf.clear();
            for (int i = 0; i < n; ++i)
                buf.setSample (0, i, kAmp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                              * freq * (pos + i) / kRate));
            proc.processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
                out.push_back (buf.getSample (0, i));
            pos += n;
        }
        G10Test::Radix2Fft f (kFft);
        std::vector<float> real (kFft, 0.0f), imag (kFft, 0.0f);
        for (int i = 0; i < kFft; ++i)
            real[i] = out[settle + i];
        f.transform (real.data(), imag.data());
        const double binHz = kRate / kFft;
        const int fundBin = (int) std::lround (freq / binHz);
        const double fund = std::sqrt ((double) real[fundBin] * real[fundBin]
                                     + (double) imag[fundBin] * imag[fundBin]);
        const double expected = (double) kAmp * kFft / 2.0;
        return 20.0 * std::log10 (std::max (1e-12, fund / expected));
    }

    /** FFT-bin gain (dB) of `data` relative to a sine of amplitude `amp`
        over `nAnalyzed` samples (expected bin = amp * nAnalyzed / 2). */
    static double fftGainDb (const std::vector<float>& data, int nFft, int fundBin,
                             float amp, int nAnalyzed)
    {
        G10Test::Radix2Fft f (nFft);
        std::vector<float> real (nFft, 0.0f), imag (nFft, 0.0f);
        const int n = std::min ((int) data.size(), nFft);
        for (int i = 0; i < n; ++i)
            real[i] = data[i];
        f.transform (real.data(), imag.data());
        const double m = std::sqrt ((double) real[fundBin] * real[fundBin]
                                  + (double) imag[fundBin] * imag[fundBin]);
        const double expected = (double) amp * nAnalyzed / 2.0;
        return 20.0 * std::log10 (std::max (1e-12, m / expected));
    }

    /** Phase (degrees) of the fundamental bin of `data` (nFft FFT). */
    static double fftPhaseDegrees (const std::vector<float>& data, int nFft, int fundBin)
    {
        G10Test::Radix2Fft f (nFft);
        std::vector<float> real (nFft, 0.0f), imag (nFft, 0.0f);
        const int n = std::min ((int) data.size(), nFft);
        for (int i = 0; i < n; ++i)
            real[i] = data[i];
        f.transform (real.data(), imag.data());
        return std::atan2 ((double) imag[fundBin], (double) real[fundBin])
               * 180.0 / juce::MathConstants<double>::pi;
    }

    // -----------------------------------------------------------------------
    // Manual replica of G10AnalogChainCore::processBlock with individually
    // resettable components (diagnostic only; trims/bypass are unity/off).
    // -----------------------------------------------------------------------

    struct ManualChain
    {
        G10OversamplerCore up, down;
        G10DiscreteInputStageCore discrete;
        G10IronOutputStageCore iron;
        G10CurveEngineCore engine;
        int factor = 2;

        void prepare (double rate, int maxBlock, int factorIn)
        {
            factor = factorIn;
            const double osRate = rate * factor;
            up.prepare (rate, maxBlock, 1, factor);
            down.prepare (rate, maxBlock, 1, factor);
            discrete.prepare (osRate);
            iron.prepare (osRate);
            engine.prepare (osRate, maxBlock * factor, 1);
            engine.setInputTargetDb (0.0f);
            engine.setOutputTargetDb (0.0f);
            engine.setBypassTarget (false);
        }

        void resetAll()
        {
            up.reset();
            down.reset();
            discrete.reset();
            iron.reset();
            engine.reset();
        }

        void process (juce::AudioBuffer<float>& buf, int numSamples)
        {
            juce::AudioBuffer<float> os (1, numSamples * factor);
            up.processUp (buf, 1, numSamples, os);
            discrete.process (os, 1, numSamples * factor);
            float* osData[1] = { os.getWritePointer (0) };
            engine.processBlock (osData, 1, numSamples * factor);
            iron.process (os, 1, numSamples * factor);
            down.processDown (os, 1, numSamples, buf);
        }
    };

    void testAaCoefficients()
    {
        beginTest ("AA coefficients");

        // Production AA design policy (Phase 2, measured):
        //   NORMAL 2x: 8th order, fc = 1.1 x base Nyquist (frozen, bit-identical).
        //   HQ 4x: 10th order, fc = 1.0208333 x base Nyquist (24.5 kHz @ 48 kHz).
        // 2x Qs are the original hardcoded values; 4x Qs come from the
        // Butterworth pole-pair formula Q_i = 1 / (2*|cos(theta_i)|),
        // theta_i = pi * (2*i + n + 1) / (2*n).
        struct Design { int factor; int order; float fc; int sections; };
        const Design designs[] = {
            { 2, G10OversamplerCore::kAaOrder2x,
              (float) (G10OversamplerCore::kAaCutoffRatio2x * kRate * 0.5), 4 },
            { 4, G10OversamplerCore::kAaOrder4x,
              (float) (G10OversamplerCore::kAaCutoffRatio4x * kRate * 0.5), 5 },
        };

        // Expected section Qs (order 8: original hardcoded; order 10: pole formula).
        static constexpr float kQ8[4] = { 0.5098f, 0.6011f, 0.8999f, 2.5626f };
        static constexpr float kQ10[5] = { 3.1962f, 1.1013f, 0.70711f, 0.56116f, 0.50623f };

        for (const auto& d : designs)
        {
            const double osRate = kRate * d.factor;
            G10ButterworthAA aa;
            aa.setCoefficients (d.fc, osRate, d.order);

            for (int s = 0; s < d.sections; ++s)
            {
                const auto c = aa.getSectionCoeffs (s);
                logMessage (juce::String ("factor=") + juce::String (d.factor)
                            + " section=" + juce::String (s)
                            + " b0=" + juce::String (c.b0, 6)
                            + " b1=" + juce::String (c.b1, 6)
                            + " b2=" + juce::String (c.b2, 6)
                            + " a1=" + juce::String (c.a1, 6)
                            + " a2=" + juce::String (c.a2, 6));
                expect (std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2)
                        && std::isfinite (c.a1) && std::isfinite (c.a2),
                        "AA coefficients must be finite");
                expect (std::abs (c.b0) > 1.0e-6f || std::abs (c.b1) > 1.0e-6f
                        || std::abs (c.b2) > 1.0e-6f,
                        "AA coefficients must not be identity");
                // TDF2 stability: poles inside the unit circle.
                expect (std::abs (c.a2) < 1.0f && std::abs (c.a1) < 1.0f + c.a2,
                        "AA section must be stable");

                // Implied Q from the TDF2 coefficients (RBJ cookbook):
                //   alpha = (1 - a2) / (1 + a2),  Q = sin(w0) / (2*alpha).
                const double w0 = 2.0 * juce::MathConstants<double>::pi * d.fc / osRate;
                const double alpha = (1.0 - c.a2) / (1.0 + c.a2);
                const double qImplied = std::sin (w0) / (2.0 * alpha);
                const float qExpected = (d.order == 8) ? kQ8[s] : kQ10[s];
                expect (std::abs (qImplied - qExpected) < 1.0e-3,
                        "AA section Q must match the design");
            }

            // Unused sections must remain identity (exact section count).
            if (d.sections < G10ButterworthAA::kMaxSections)
            {
                const auto extra = aa.getSectionCoeffs (d.sections);
                expect (extra.b0 == 1.0f && extra.b1 == 0.0f && extra.b2 == 0.0f
                        && extra.a1 == 0.0f && extra.a2 == 0.0f,
                        "unused AA sections must stay identity");
            }
        }
    }

    void testAaIsolation()
    {
        beginTest ("AA filter isolation (zero-stuffed sine)");

        const int fft = 16384;
        const float amp = 0.01f;

        for (int factor : { 2, 4 })
        {
            const double osRate = kRate * factor;
            // Production AA design policy (Phase 2, measured): factor-specific
            // cutoff ratio and order (see G10OversamplerCore).
            const float fc = (float) (kRate * 0.5)
                           * (factor == 4 ? G10OversamplerCore::kAaCutoffRatio4x
                                          : G10OversamplerCore::kAaCutoffRatio2x);
            const int order = (factor == 4) ? G10OversamplerCore::kAaOrder4x
                                            : G10OversamplerCore::kAaOrder2x;
            G10ButterworthAA aa;
            aa.setCoefficients (fc, osRate, order);
            aa.reset();

            const double binHz = osRate / fft;
            const double freq = exactBinFreq (kRate, fft / factor, 1000.0);
            const int fundBin = (int) std::lround (freq / binHz);

            // Zero-stuffed sine at the base rate, then through the AA alone.
            std::vector<float> os (fft, 0.0f);
            for (int i = 0; i < fft / factor; ++i)
                os[i * factor] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * freq * i / kRate);
            for (int i = 0; i < fft; ++i)
                os[i] = aa.process (os[i]);

            G10Test::Radix2Fft f (fft);
            std::vector<float> real (fft, 0.0f), imag (fft, 0.0f);
            for (int i = 0; i < fft; ++i)
                real[i] = os[i];
            f.transform (real.data(), imag.data());

            // The zero-stuffed fundamental carries amplitude amp/factor, so
            // its expected FFT-bin magnitude is amp * fft / (2 * factor).
            const double expectedBin = (double) amp * fft / (2.0 * factor);
            auto magDb = [&] (int bin) -> double
            {
                const double m = std::sqrt ((double) real[bin] * real[bin]
                                          + (double) imag[bin] * imag[bin]);
                return 20.0 * std::log10 (std::max (1e-12, m / expectedBin));
            };

            const double passband = magDb (fundBin);

            // The REAL zero-stuff interpolation images sit at k*FsBase +- f
            // (k = 1..factor-1) within the oversampled Nyquist. The bin
            // fftSize - fundBin is the conjugate mirror of the fundamental,
            // NOT an interpolation image, and must not be measured as one.
            double worstImageDb = -300.0;
            int worstImageBin = -1;
            for (int k = 1; k < factor; ++k)
            {
                for (int sign : { -1, 1 })
                {
                    const double imageHz = k * kRate + sign * freq;
                    if (imageHz <= 0.0 || imageHz >= osRate * 0.5)
                        continue;
                    const int bin = (int) std::lround (imageHz / binHz);
                    if (bin <= 0 || bin >= fft)
                        continue;
                    const double db = magDb (bin);
                    if (db > worstImageDb)
                    {
                        worstImageDb = db;
                        worstImageBin = bin;
                    }
                }
            }

            logMessage (juce::String ("AA isolation factor=") + juce::String (factor)
                        + " passband=" + juce::String (passband, 2) + " dB"
                        + " worstImage=" + juce::String (worstImageDb, 2) + " dB"
                        + " (bin=" + juce::String (worstImageBin) + ")");
            expect (std::abs (passband) < 1.0, "AA passband within +-1 dB");
            expect (worstImageDb < -40.0, "AA must attenuate the zero-stuff images");
        }
    }

    void testFullChainFreshVsReused()
    {
        beginTest ("Full chain gain: fresh processor per frequency vs reused");

        const double freqs[] = { 20.0, 31.0, 63.0, 125.0, 250.0, 500.0,
                                 1000.0, 2000.0, 4000.0, 8000.0, 16000.0, 20000.0 };

        for (int quality = 0; quality <= 1; ++quality)
        {
            juce::String fresh = "fresh q=" + juce::String (quality);
            for (double f : freqs)
            {
                // Quality is internal: 0 = realtime (2x), 1 = offline (4x).
                auto proc = G10Test::makePreparedProcessor (kRate, kBlock, quality == 1);
                const double freq = exactBinFreq (kRate, kFft, f);
                fresh += " | " + juce::String (f) + "Hz "
                       + juce::String (measureProcessorGain (*proc, freq), 2) + "dB";
            }
            logMessage (fresh);

            auto proc = G10Test::makePreparedProcessor (kRate, kBlock, quality == 1);
            juce::String reused = "reused q=" + juce::String (quality);
            for (double f : freqs)
            {
                const double freq = exactBinFreq (kRate, kFft, f);
                reused += " | " + juce::String (f) + "Hz "
                         + juce::String (measureProcessorGain (*proc, freq), 2) + "dB";
            }
            logMessage (reused);
        }
    }

    // -----------------------------------------------------------------------
    // Step 2: same-frequency reuse — separates cumulative state drift from
    // frequency-change/transition behavior.
    // -----------------------------------------------------------------------

    void testSameFreqReuse()
    {
        beginTest ("Same-frequency reuse: fresh vs repeated vs transitions");

        const double f8k = exactBinFreq (kRate, kFft, 8000.0);

        for (int quality : { 0, 1 })
        {
            // Fresh reference.
            {
                auto proc = G10Test::makePreparedProcessor (kRate, kBlock, quality == 1);
                logMessage (juce::String ("q=") + juce::String (quality)
                            + " fresh 8k: "
                            + juce::String (measureProcessorGain (*proc, f8k), 2) + " dB");
            }

            // Same processor, same 8 kHz tone, four times.
            {
                auto proc = G10Test::makePreparedProcessor (kRate, kBlock, quality == 1);
                juce::String row = "q=" + juce::String (quality) + " repeated 8k:";
                for (int i = 0; i < 4; ++i)
                    row += " | " + juce::String (measureProcessorGain (*proc, f8k), 2) + " dB";
                logMessage (row);
            }

            // Single frequency changes into 8 kHz (fresh processor each).
            for (double from : { 1000.0, 4000.0, 16000.0 })
            {
                auto proc = G10Test::makePreparedProcessor (kRate, kBlock, quality == 1);
                const double fFrom = exactBinFreq (kRate, kFft, from);
                measureProcessorGain (*proc, fFrom); // settle on the previous tone
                logMessage (juce::String ("q=") + juce::String (quality)
                            + " " + juce::String (from) + "Hz -> 8k: "
                            + juce::String (measureProcessorGain (*proc, f8k), 2) + " dB");
            }
        }
    }

    // -----------------------------------------------------------------------
    // Step 3: stage-by-stage gain trace. Where does 1/factor return toward
    // unity? Measured, not inferred.
    // -----------------------------------------------------------------------

    void testStageByStageGainTrace()
    {
        beginTest ("Stage-by-stage gain trace (2x and 4x)");

        const int baseFft = 16384;
        const float amp = 0.001f; // -60 dBFS: nonlinear stages effectively unity
        const double freqs[] = { 100.0, 1000.0, 8000.0, 16000.0 };

        for (int factor : { 2, 4 })
        {
            const double osRate = kRate * factor;
            const int osFft = baseFft * factor;
            const float fc = (float) (1.1 * kRate * 0.5);

            G10OversamplerCore up, down;
            up.prepare (kRate, baseFft, 1, factor);
            down.prepare (kRate, baseFft, 1, factor);
            up.reset();
            down.reset();

            G10DiscreteInputStageCore discrete;
            discrete.prepare (osRate);
            G10IronOutputStageCore iron;
            iron.prepare (osRate);
            G10CurveEngineCore engine;
            engine.prepare (osRate, osFft, 1);
            engine.setInputTargetDb (0.0f);
            engine.setOutputTargetDb (0.0f);
            engine.setBypassTarget (false);

            for (double f : freqs)
            {
                const double freq = exactBinFreq (kRate, baseFft, f);
                const double binHz = kRate / baseFft; // == osRate / osFft
                const int fundBin = (int) std::lround (freq / binHz);

                const int nBase = baseFft;
                const int nOs = nBase * factor;

                std::vector<float> base (nBase);
                for (int i = 0; i < nBase; ++i)
                    base[i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                      * freq * i / kRate);

                // A. Input (sanity: ~0 dB).
                const double aDb = fftGainDb (base, nBase, fundBin, amp, nBase);

                // B. Zero-stuff only (no AA).
                std::vector<float> os (nOs, 0.0f);
                for (int i = 0; i < nBase; ++i)
                    os[i * factor] = base[i];
                const double bDb = fftGainDb (os, osFft, fundBin, amp, nOs);

                // C. Zero-stuff + interpolation AA + xL normalization (up).
                // This is the pre-Discrete amplitude: must equal the input.
                G10ButterworthAA aaUp;
                aaUp.setCoefficients (fc, osRate);
                aaUp.reset();
                for (int i = 0; i < nOs; ++i)
                    os[i] = aaUp.process (os[i]) * (float) factor;
                const double cDb = fftGainDb (os, osFft, fundBin, amp, nOs);

                // D. Discrete input stage.
                juce::AudioBuffer<float> osBuf (1, nOs);
                for (int i = 0; i < nOs; ++i)
                    osBuf.setSample (0, i, os[i]);
                discrete.process (osBuf, 1, nOs);
                for (int i = 0; i < nOs; ++i)
                    os[i] = osBuf.getSample (0, i);
                const double dDb = fftGainDb (os, osFft, fundBin, amp, nOs);

                // E. Frozen clean curve engine at 0 dB (oversampled rate).
                float* osData[1] = { os.data() };
                engine.processBlock (osData, 1, nOs);
                const double eDb = fftGainDb (os, osFft, fundBin, amp, nOs);

                // F. Iron output stage.
                for (int i = 0; i < nOs; ++i)
                    osBuf.setSample (0, i, os[i]);
                iron.process (osBuf, 1, nOs);
                for (int i = 0; i < nOs; ++i)
                    os[i] = osBuf.getSample (0, i);
                const double fDb = fftGainDb (os, osFft, fundBin, amp, nOs);

                // G. Decimation AA (full stream, before sample dropping).
                G10ButterworthAA aaDown;
                aaDown.setCoefficients (fc, osRate);
                aaDown.reset();
                for (int i = 0; i < nOs; ++i)
                    os[i] = aaDown.process (os[i]);
                const double gDb = fftGainDb (os, osFft, fundBin, amp, nOs);

                // H. Decimate (keep the last sample of each factor group).
                std::vector<float> out (nBase);
                for (int i = 0; i < nBase; ++i)
                    out[i] = os[i * factor + factor - 1];
                const double hDb = fftGainDb (out, nBase, fundBin, amp, nBase);

                logMessage (juce::String ("factor=") + juce::String (factor)
                            + " f=" + juce::String (f)
                            + " A=" + juce::String (aDb, 2)
                            + " B=" + juce::String (bDb, 2)
                            + " C=" + juce::String (cDb, 2)
                            + " D=" + juce::String (dDb, 2)
                            + " E=" + juce::String (eDb, 2)
                            + " F=" + juce::String (fDb, 2)
                            + " G=" + juce::String (gDb, 2)
                            + " H=" + juce::String (hDb, 2) + " dB");
            }
        }
    }

    // -----------------------------------------------------------------------
    // Step 4: bare oversampler (PATH A) vs full G10AnalogChainCore (PATH B).
    // -----------------------------------------------------------------------

    void testBareVsFullChain()
    {
        beginTest ("Bare oversampler vs full analog chain");

        const int fft = 16384;
        const float amp = 0.001f;
        const double freqs[] = { 100.0, 1000.0, 8000.0, 16000.0 };

        for (int factor : { 2, 4 })
        {
            G10OversamplerCore up, down;
            up.prepare (kRate, fft, 1, factor);
            down.prepare (kRate, fft, 1, factor);
            up.reset();
            down.reset();

            G10AnalogChainCore chain;
            chain.prepare (kRate, fft, 1, factor);
            chain.reset();

            for (double f : freqs)
            {
                const double freq = exactBinFreq (kRate, fft, f);
                const double binHz = kRate / fft;
                const int fundBin = (int) std::lround (freq / binHz);

                juce::AudioBuffer<float> base (1, fft);
                for (int i = 0; i < fft; ++i)
                    base.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * freq * i / kRate));

                // PATH A: bare up + down.
                juce::AudioBuffer<float> os (1, fft * factor);
                juce::AudioBuffer<float> outA (1, fft);
                up.processUp (base, 1, fft, os);
                down.processDown (os, 1, fft, outA);
                std::vector<float> a (fft);
                for (int i = 0; i < fft; ++i)
                    a[i] = outA.getSample (0, i);
                const double aDb = fftGainDb (a, fft, fundBin, amp, fft);

                // PATH B: full analog chain.
                juce::AudioBuffer<float> outB (1, fft);
                outB.copyFrom (0, 0, base, 0, 0, fft);
                chain.processBlock (outB, 1, fft);
                std::vector<float> b (fft);
                for (int i = 0; i < fft; ++i)
                    b[i] = outB.getSample (0, i);
                const double bDb = fftGainDb (b, fft, fundBin, amp, fft);

                logMessage (juce::String ("factor=") + juce::String (factor)
                            + " f=" + juce::String (f)
                            + " pathA=" + juce::String (aDb, 2) + " dB"
                            + " pathB=" + juce::String (bDb, 2) + " dB");
            }
        }
    }

    // -----------------------------------------------------------------------
    // Step 5: selective state reset. Reused 1k -> 8k, resetting ONE component
    // before the 8k run. Whichever restores the fresh response identifies the
    // accumulating state.
    // -----------------------------------------------------------------------

    void testSelectiveStateReset()
    {
        beginTest ("Selective state reset: 1k -> 8k droop localization");

        const int fft = 16384;
        const float amp = 0.001f;
        const double f1k = exactBinFreq (kRate, fft, 1000.0);
        const double f8k = exactBinFreq (kRate, fft, 8000.0);
        const double binHz = kRate / fft;
        const int fundBin = (int) std::lround (f8k / binHz);

        auto runTone = [&] (ManualChain& chain, double freq) -> double
        {
            const int settle = (int) (kRate * 0.5);
            const int total = settle + fft;
            juce::AudioBuffer<float> buf (1, kBlock);
            std::vector<float> out;
            out.reserve (total);
            int pos = 0;
            while (pos < total)
            {
                const int n = std::min (kBlock, total - pos);
                buf.clear();
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * freq * (pos + i) / kRate));
                chain.process (buf, n);
                for (int i = 0; i < n; ++i)
                    out.push_back (buf.getSample (0, i));
                pos += n;
            }
            std::vector<float> win (fft);
            for (int i = 0; i < fft; ++i)
                win[i] = out[settle + i];
            return fftGainDb (win, fft, fundBin, amp, fft);
        };

        for (int factor : { 2, 4 })
        {
            // Fresh 8k reference.
            {
                ManualChain chain;
                chain.prepare (kRate, fft, factor);
                chain.resetAll();
                logMessage (juce::String ("factor=") + juce::String (factor)
                            + " fresh 8k: " + juce::String (runTone (chain, f8k), 2) + " dB");
            }

            // Reused 1k -> 8k (no reset).
            {
                ManualChain chain;
                chain.prepare (kRate, fft, factor);
                chain.resetAll();
                runTone (chain, f1k);
                logMessage (juce::String ("factor=") + juce::String (factor)
                            + " reused 1k->8k: " + juce::String (runTone (chain, f8k), 2) + " dB");
            }

            // Selective resets before the 8k run.
            const char* names[] = { "upAA", "downAA", "discrete", "iron", "engine" };
            for (const char* what : names)
            {
                ManualChain chain;
                chain.prepare (kRate, fft, factor);
                chain.resetAll();
                runTone (chain, f1k);
                const juce::String compName (what);
                if (compName == "upAA")        chain.up.reset();
                else if (compName == "downAA") chain.down.reset();
                else if (compName == "discrete") chain.discrete.reset();
                else if (compName == "iron")   chain.iron.reset();
                else                           chain.engine.reset();
                logMessage (juce::String ("factor=") + juce::String (factor)
                            + " reset " + compName + ": "
                            + juce::String (runTone (chain, f8k), 2) + " dB");
            }
        }
    }

    /** Phase response of the full analog chain relative to the clean path at
        1k-16k. The reused-processor droop is clean-vs-chain cancellation
        during the (too slow) crossfade: at a bin where the chain is ~180
        degrees from the clean path, the residual collapses (reused q=1 16k
        measured -34.5 dB at crossfade state m ~ 0.785, which requires
        phi ~ 180 deg and g = 1/4). */
    void testChainPhaseAtHighFreq()
    {
        beginTest ("Chain phase at 1k-16k: cancellation check");

        const int fft = 16384;
        const float amp = 0.001f;
        const double binHz = kRate / fft;
        const double freqs[] = { 1000.0, 4000.0, 8000.0, 16000.0 };

        for (int factor : { 2, 4 })
        {
            for (double f : freqs)
            {
                const double freq = exactBinFreq (kRate, fft, f);
                const int fundBin = (int) std::lround (freq / binHz);

                ManualChain chain;
                chain.prepare (kRate, fft, factor);
                chain.resetAll();

                const int settle = (int) (kRate * 0.5);
                const int total = settle + fft;
                juce::AudioBuffer<float> buf (1, kBlock);
                std::vector<float> out;
                out.reserve (total);
                int pos = 0;
                while (pos < total)
                {
                    const int n = std::min (kBlock, total - pos);
                    buf.clear();
                    for (int i = 0; i < n; ++i)
                        buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                      * freq * (pos + i) / kRate));
                    chain.process (buf, n);
                    for (int i = 0; i < n; ++i)
                        out.push_back (buf.getSample (0, i));
                    pos += n;
                }

                // Ideal clean path over the same window. The window starts
                // mid-tone at sample `settle`, so the reference phase offset is
                // real and must be subtracted from the chain's phase.
                std::vector<float> ref (fft), chn (fft);
                for (int i = 0; i < fft; ++i)
                {
                    ref[i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * freq * (settle + i) / kRate);
                    chn[i] = out[settle + i];
                }
                double d = fftPhaseDegrees (chn, fft, fundBin)
                         - fftPhaseDegrees (ref, fft, fundBin);
                while (d > 180.0)  d -= 360.0;
                while (d <= -180.0) d += 360.0;

                // Crossfade residual predicted at the state observed during the
                // reused q=1 16k measurement: out = (1-m) + m*g*e^(j*phi).
                const double g = 1.0 / factor;
                const double rad = d * juce::MathConstants<double>::pi / 180.0;
                const double re = (1.0 - 0.785) + 0.785 * g * std::cos (rad);
                const double im = 0.785 * g * std::sin (rad);
                const double predDb = 20.0 * std::log10 (std::max (1e-12, std::sqrt (re * re + im * im)));

                logMessage (juce::String ("factor=") + juce::String (factor)
                            + " f=" + juce::String (f)
                            + " chainPhase=" + juce::String (d, 1) + " deg"
                            + " chainGain=" + juce::String (fftGainDb (chn, fft, fundBin, amp, fft), 2) + " dB"
                            + " pred@m=0.785=" + juce::String (predDb, 2) + " dB");
            }
        }
    }

    // -----------------------------------------------------------------------
    // Transition duration: the quality ramp (realtime 2x <-> offline 4x) must
    // be ~10 ms (480 samples at 48 kHz) regardless of block size. Measured
    // with a 64-sample block so the ramp spans several blocks; per-block
    // output RMS is logged until it settles within 0.2 dB of the chain gain.
    // The analog path is canonical (always on), so there is no analog ramp.
    // -----------------------------------------------------------------------

    void testTransitionDuration()
    {
        beginTest ("Transition duration: quality ramp (realtime<->offline)");

        const int smallBlock = 64;
        // 750 Hz at 48 kHz = 64 samples/period, so each 64-sample block is
        // exactly one period and the block RMS is exact (no window ripple).
        const double freq = exactBinFreq (kRate, kFft, 750.0);
        const float amp = 0.01f;
        const int settleBlocks = (int) (kRate * 0.5) / smallBlock;

        auto runQualityRamp = [&] (bool startOffline, const char* label)
        {
            auto proc = G10Test::makePreparedProcessor (kRate, smallBlock, startOffline);

            juce::AudioBuffer<float> buf (1, smallBlock);
            juce::MidiBuffer midi;
            auto fill = [&] (int blockIndex)
            {
                buf.clear();
                for (int i = 0; i < smallBlock; ++i)
                    buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * freq * (blockIndex * smallBlock + i) / kRate));
            };

            // Settle in the starting mode.
            for (int b = 0; b < settleBlocks; ++b)
            {
                fill (b);
                proc->processBlock (buf, midi);
            }

            // Switch the host mode mid-stream; the processor ramps the
            // quality crossfade over ~10 ms (the transition machinery).
            proc->setNonRealtime (! startOffline);
            juce::String row = juce::String (label) + " (block=64):";
            int settledBlock = -1;
            for (int b = 0; b < 200; ++b)
            {
                fill (settleBlocks + b);
                proc->processBlock (buf, midi);
                double sum = 0.0;
                for (int i = 0; i < smallBlock; ++i)
                {
                    const double v = buf.getSample (0, i);
                    sum += v * v;
                }
                const double rmsDb = 20.0 * std::log10 (std::max (1e-12, std::sqrt (sum / smallBlock) * std::sqrt (2.0) / amp));
                if (b < 16)
                    row += " " + juce::String (rmsDb, 1);
                if (settledBlock < 0 && std::abs (rmsDb - 0.0) < 0.2)
                    settledBlock = b + 1;
            }
            row += " settled@block" + juce::String (settledBlock)
                 + " (" + juce::String (settledBlock * smallBlock / kRate * 1000.0, 1) + " ms)";
            logMessage (row);
        };

        runQualityRamp (false, "realtime -> offline (2x -> 4x)");
        runQualityRamp (true,  "offline -> realtime (4x -> 2x)");
    }

    void testQualityEndpoints()
    {
        beginTest ("Quality endpoints: realtime exactly NORMAL (2x), offline exactly HQ (4x)");

        const double rate = kRate;
        const double freq = 1000.0;
        const float amp = kAmp;
        const int win = 8192;

        auto capture = [&] (juce::AudioProcessor& proc, int block, std::vector<float>& out)
        {
            const int settle = (int) (rate * 0.5);
            const int total = settle + win;
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            out.clear();
            out.reserve (total);
            int pos = 0;
            while (pos < total)
            {
                const int n = std::min (block, total - pos);
                buf.clear();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        buf.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                      * freq * (pos + i) / rate));
                proc.processBlock (buf, midi);
                for (int i = 0; i < n; ++i)
                    out.push_back (buf.getSample (0, i));
                pos += n;
            }
        };

        // Canonical processor in the given host mode: realtime = NORMAL (2x),
        // offline = HQ (4x). The analog color is always active.
        auto makeCanonical = [&] (int block, bool offline)
        {
            return G10Test::makePreparedProcessor (rate, block, offline);
        };

        auto lastWindowExact = [] (const std::vector<float>& a, const std::vector<float>& b, int w)
        {
            if ((int) a.size() < w || (int) b.size() < w)
                return false;
            for (int i = 0; i < w; ++i)
                if (a[a.size() - w + i] != b[b.size() - w + i])
                    return false;
            return true;
        };

        auto windowDiff = [] (const std::vector<float>& a, const std::vector<float>& b, int w,
                              float& maxAbs, float& rms)
        {
            maxAbs = 0.0f;
            double sumSq = 0.0;
            for (int i = 0; i < w; ++i)
            {
                const float d = std::abs (a[a.size() - w + i] - b[b.size() - w + i]);
                maxAbs = std::max (maxAbs, d);
                sumSq += (double) d * d;
            }
            rms = (float) std::sqrt (sumSq / w);
        };

        // Reference endpoints (settled NORMAL-only and settled HQ-only).
        auto refNormal = makeCanonical (1024, false);
        std::vector<float> refNormalOut;
        capture (*refNormal, 1024, refNormalOut);

        auto refHq = makeCanonical (1024, true);
        std::vector<float> refHqOut;
        capture (*refHq, 1024, refHqOut);

        // NORMAL -> HQ transition: after settling, output must be bit-exact
        // to the HQ endpoint (parallel routing; HQ never saw NORMAL's output).
        // The host mode switch mid-stream exercises the transition machinery.
        auto up = makeCanonical (1024, false);
        std::vector<float> upOut;
        capture (*up, 1024, upOut);
        up->setNonRealtime (true);
        std::vector<float> upAfter;
        capture (*up, 1024, upAfter);
        expect (lastWindowExact (upAfter, refHqOut, win),
                "NORMAL->HQ must settle bit-exact to the HQ endpoint");

        // HQ -> NORMAL transition: settled product contract for the stateful
        // chain. The endpoint is a deterministic function of the chain's
        // state history (the internal NORMAL chain is idle during the HQ
        // phase and processes the 10 ms transition ramps), so bit-exactness
        // is asserted against a STATE-EQUIVALENT reference (same history);
        // against the fresh NORMAL reference the contract is a bounded
        // residual, no click/pop, and no gain jump.
        auto down = makeCanonical (1024, true);
        std::vector<float> downOut;
        capture (*down, 1024, downOut);
        down->setNonRealtime (false);
        std::vector<float> downAfter;
        capture (*down, 1024, downAfter);

        // Deterministic convergence: bit-exact to a state-equivalent
        // reference (same HQ->NORMAL history -> identical chain state).
        auto refSame = makeCanonical (1024, true);
        std::vector<float> refSameOut;
        capture (*refSame, 1024, refSameOut);
        refSame->setNonRealtime (false);
        std::vector<float> refSameAfter;
        capture (*refSame, 1024, refSameAfter);
        expect (lastWindowExact (downAfter, refSameAfter, win),
                "HQ->NORMAL must settle deterministically to the state-equivalent NORMAL reference");

        // Bounded residual vs the fresh NORMAL reference (different state
        // history; measured bit-exact, theoretical sub-ULP ~1e-9).
        float maxAbsFresh = 0.0f, rmsFresh = 0.0f;
        windowDiff (downAfter, refNormalOut, win, maxAbsFresh, rmsFresh);
        logMessage ("HQ->NORMAL vs fresh NORMAL reference: maxAbs=" + juce::String (maxAbsFresh, 8)
                    + " rms=" + juce::String (rmsFresh, 8));
        expect (maxAbsFresh < 1.0e-6f,
                "HQ->NORMAL residual vs fresh NORMAL reference must be bounded (< 1e-6)");

        // No click/pop: the max sample-to-sample delta across the quality
        // transition (first 20 ms of the post-switch capture, continuous
        // input) must stay near the settled steady-state delta (a click
        // would jump by the full signal swing). The capture boundary itself
        // is excluded: each capture restarts the sine at phase 0, which is
        // a test artifact, not a transition artifact.
        float maxDeltaTransition = 0.0f;
        for (int i = 1; i < (int) (rate * 0.02); ++i)
            maxDeltaTransition = std::max (maxDeltaTransition,
                                           std::abs (downAfter[i] - downAfter[i - 1]));
        float maxDeltaSettled = 0.0f;
        for (int i = (int) downAfter.size() - win + 1; i < (int) downAfter.size(); ++i)
            maxDeltaSettled = std::max (maxDeltaSettled,
                                        std::abs (downAfter[i] - downAfter[i - 1]));
        logMessage ("HQ->NORMAL transition maxDelta=" + juce::String (maxDeltaTransition, 8)
                    + " settled maxDelta=" + juce::String (maxDeltaSettled, 8));
        expect (maxDeltaTransition < 4.0f * maxDeltaSettled,
                "HQ->NORMAL transition must not click (max sample delta bounded)");

        // No meaningful gain jump: settled RMS gain within 0.1 dB of the
        // fresh NORMAL reference.
        double sumDown = 0.0, sumRef = 0.0;
        for (int i = 0; i < win; ++i)
        {
            sumDown += (double) downAfter[downAfter.size() - win + i]
                     * (double) downAfter[downAfter.size() - win + i];
            sumRef += (double) refNormalOut[refNormalOut.size() - win + i]
                      * (double) refNormalOut[refNormalOut.size() - win + i];
        }
        const double gainDb = 10.0 * std::log10 (std::max (1e-30, sumDown)
                                                 / std::max (1e-30, sumRef));
        logMessage ("HQ->NORMAL settled gain vs fresh reference: "
                    + juce::String (gainDb, 4) + " dB");
        expect (std::abs (gainDb) < 0.1,
                "HQ->NORMAL settled gain must not jump vs the fresh NORMAL reference (< 0.1 dB)");

        // Block-size independence: settled HQ at block 64 must be bit-exact
        // to the block-1024 HQ reference.
        auto small = makeCanonical (64, true);
        std::vector<float> smallOut;
        capture (*small, 64, smallOut);
        expect (lastWindowExact (smallOut, refHqOut, win),
                "settled HQ must be bit-exact across block sizes (64 vs 1024)");
    }

    void testNumericalSafety()
    {
        beginTest ("Numerical safety: 0 dBFS, extreme trims, all bands +/-12");

        const int block = 1024;
        const int total = (int) (kRate * 0.5);

        struct Case { const char* name; float amp; float inputDb; float outputDb; float bandDb; };
        const Case cases[] = {
            { "typical -40dBFS", 0.01f,  0.0f,  0.0f,  0.0f },
            { "0dBFS sine",      1.0f,   0.0f,  0.0f,  0.0f },
            { "extreme +18/-18", 1.0f,  18.0f, -18.0f, 0.0f },
            { "all bands +12",   1.0f,   0.0f,  0.0f, 12.0f },
            { "all bands -12",   1.0f,   0.0f,  0.0f, -12.0f },
        };

        for (int quality : { 0, 1 })
        {
            for (const auto& c : cases)
            {
                // Quality is internal: 0 = realtime (2x), 1 = offline (4x).
                auto proc = G10Test::makePreparedProcessor (kRate, block, quality == 1);
                setDb (G10Test::findParam (*proc, "g10.input"), c.inputDb);
                setDb (G10Test::findParam (*proc, "g10.output"), c.outputDb);
                for (int b = 0; b < kNumBands; ++b)
                    setDb (G10Test::findParam (*proc, APEX::G10::kBandInfos[b].paramId), c.bandDb);

                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;
                bool finite = true;
                float peak = 0.0f;
                int pos = 0;
                while (pos < total)
                {
                    const int n = std::min (block, total - pos);
                    buf.clear();
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < n; ++i)
                            buf.setSample (ch, i, c.amp * (float) std::sin (
                                2.0 * juce::MathConstants<double>::pi * 1000.0 * (pos + i) / kRate));
                    proc->processBlock (buf, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < n; ++i)
                        {
                            const float v = buf.getSample (ch, i);
                            if (! std::isfinite (v))
                                finite = false;
                            peak = std::max (peak, std::abs (v));
                        }
                    pos += n;
                }
                logMessage (juce::String ("q=") + juce::String (quality)
                            + " " + juce::String (c.name)
                            + " finite=" + juce::String (finite ? "true" : "false")
                            + " peak=" + juce::String (peak, 3));
                expect (finite, juce::String ("no NaN/Inf: ") + c.name + " q=" + juce::String (quality));
            }
        }
    }

    void testLinearRoundTrip()
    {
        beginTest ("Linear oversampler round trip (up + down, no stages)");

        const int fft = 16384;
        const double binHz = kRate / fft;
        const double freqs[] = { 20.0, 100.0, 1000.0, 2000.0, 4000.0, 8000.0,
                                 12000.0, 16000.0, 20000.0 };
        const float amp = 0.01f;

        for (int factor : { 2, 4 })
        {
            G10OversamplerCore up, down;
            up.prepare (kRate, 512, 1, factor);
            down.prepare (kRate, 512, 1, factor);
            up.reset();
            down.reset();

            for (double f : freqs)
            {
                const double freq = exactBinFreq (kRate, fft, f);
                const int fundBin = (int) std::lround (freq / binHz);
                const int n = fft / 2; // base-rate samples
                juce::AudioBuffer<float> base (1, n);
                juce::AudioBuffer<float> os (1, n * factor);
                juce::AudioBuffer<float> out (1, n);
                for (int i = 0; i < n; ++i)
                    base.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * freq * i / kRate));
                up.processUp (base, 1, n, os);
                down.processDown (os, 1, n, out);

                G10Test::Radix2Fft fftObj (fft);
                std::vector<float> real (fft, 0.0f), imag (fft, 0.0f);
                for (int i = 0; i < n; ++i)
                    real[i] = out.getSample (0, i);
                fftObj.transform (real.data(), imag.data());
                const double m = std::sqrt ((double) real[fundBin] * real[fundBin]
                                          + (double) imag[fundBin] * imag[fundBin]);
                // The FFT analyzes n = fft/2 non-zero base-rate samples, so
                // the expected bin magnitude for a sine of amplitude `amp` is
                // amp * n / 2 = amp * fft / 4 (NOT amp * fft / 2).
                const double expectedBin = (double) amp * n / 2.0;
                const double gainDb = 20.0 * std::log10 (std::max (1e-12, m / expectedBin));
                // The linear round trip is zero-stuff (1/L) + unity-gain AA
                // + xL interpolation normalization, so the expected gain is
                // 0 dB (unity).
                const double expectedGainDb = 0.0;
                logMessage (juce::String ("roundtrip factor=") + juce::String (factor)
                            + " f=" + juce::String (f)
                            + " gain=" + juce::String (gainDb, 3) + " dB"
                            + " (expected " + juce::String (expectedGainDb, 3) + " dB)");
                expect (std::abs (gainDb - expectedGainDb) < 1.5,
                        "round trip gain must be unity after xL interpolation normalization");
            }
        }
    }
};

static G10AnalogOsDiagTests g10AnalogOsDiagTests;

// ============================================================================
// G10AnalogCharacterizationTests — per-frequency/per-level characterization
// of the Discrete and Iron stages (permanent regression; documents the
// voicing of the current production stages).
//
// Measurement conventions (documented; never mixed without labels):
//   - Level: sine PEAK amplitude, dBFS (0 dBFS = 1.0 peak).
//   - Fundamental: FFT bin magnitude vs the expected bin for a sine of the
//     input amplitude (amp * fftSize / 2), in dB (0 dB = unity gain).
//   - Harmonics H2..H5: dB relative to the measured fundamental (dBc).
//   - THD: RMS of H2..H5 relative to the fundamental (dB).
//   - Output peak / RMS: dBFS over the analyzed window (a 0 dBFS sine has
//     RMS = -3.01 dBFS).
//   - DC: FFT bin 0 relative to the fundamental (dBc).
//   - Asymmetry: static transfer |f(+a)| vs |f(-a)|, dB difference.
//   - Static transfer: DC transfer measured by holding each input value for
//     `hold` samples and taking the last output (the state has settled).
// ============================================================================

class G10AnalogCharacterizationTests final : public juce::UnitTest
{
public:
    G10AnalogCharacterizationTests() : juce::UnitTest ("G10.Analog.Characterize", "APEX.G10") {}

    void runTest() override
    {
        testDiscreteMatrix();
        testIronMatrix();
        testStaticTransfer();
        testMemorylessVsStateful();
    }

private:
    static constexpr double kRate = 48000.0;
    static constexpr int kBlock = 512;
    static constexpr int kFft = 16384;

    struct CharResult
    {
        double fundDb = -300.0; // fundamental gain vs input (dB)
        double h2Db = -300.0, h3Db = -300.0, h4Db = -300.0, h5Db = -300.0; // dBc
        double thdDb = -300.0;  // H2..H5 RMS, dBc
        double dcDb = -300.0;   // DC bin, dBc
        double peakDb = -300.0; // output peak, dBFS
        double rmsDb = -300.0;  // output RMS, dBFS
    };

    template <typename Stage>
    CharResult measureStage (Stage& stage, double freq, double amp) const
    {
        CharResult r;
        const int settle = (int) (kRate * 0.5);
        const int total = settle + kFft;
        juce::AudioBuffer<float> buf (1, kBlock);
        std::vector<float> out;
        out.reserve (total);
        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (kBlock, total - pos);
            buf.clear();
            for (int i = 0; i < n; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi
                                                              * freq * (pos + i) / kRate)));
            stage.process (buf, 1, n);
            for (int i = 0; i < n; ++i)
                out.push_back (buf.getSample (0, i));
            pos += n;
        }

        double peak = 0.0, sumSq = 0.0;
        for (int i = settle; i < total; ++i)
        {
            const double v = out[i];
            peak = std::max (peak, std::abs (v));
            sumSq += v * v;
        }
        r.peakDb = 20.0 * std::log10 (std::max (1e-12, peak));
        r.rmsDb = 20.0 * std::log10 (std::max (1e-12, std::sqrt (sumSq / kFft)));

        G10Test::Radix2Fft fft (kFft);
        std::vector<float> real (kFft, 0.0f), imag (kFft, 0.0f);
        for (int i = 0; i < kFft; ++i)
            real[i] = out[settle + i];
        fft.transform (real.data(), imag.data());

        const double binHz = kRate / kFft;
        auto magAt = [&] (double f) -> double
        {
            const int bin = (int) std::lround (f / binHz);
            if (bin < 0 || bin >= kFft) return 0.0;
            return std::sqrt ((double) real[bin] * real[bin] + (double) imag[bin] * imag[bin]);
        };

        const double fund = magAt (freq);
        if (fund <= 1e-12)
            return r;
        const double expectedBin = amp * kFft / 2.0;
        r.fundDb = 20.0 * std::log10 (std::max (1e-12, fund / expectedBin));
        r.h2Db = 20.0 * std::log10 (std::max (1e-12, magAt (2.0 * freq) / fund));
        r.h3Db = 20.0 * std::log10 (std::max (1e-12, magAt (3.0 * freq) / fund));
        r.h4Db = 20.0 * std::log10 (std::max (1e-12, magAt (4.0 * freq) / fund));
        r.h5Db = 20.0 * std::log10 (std::max (1e-12, magAt (5.0 * freq) / fund));
        const double h2 = magAt (2.0 * freq), h3 = magAt (3.0 * freq);
        const double h4 = magAt (4.0 * freq), h5 = magAt (5.0 * freq);
        r.thdDb = 20.0 * std::log10 (std::max (1e-12, std::sqrt (h2 * h2 + h3 * h3 + h4 * h4 + h5 * h5) / fund));
        r.dcDb = 20.0 * std::log10 (std::max (1e-12, magAt (0.0) / fund));
        return r;
    }

    void logRow (const char* stageName, double f, double db, const CharResult& r)
    {
        logMessage (juce::String (stageName) + " f=" + juce::String (f, 0)
                    + " in=" + juce::String (db, 0) + "dBFS"
                    + " gain=" + juce::String (r.fundDb, 2) + "dB"
                    + " H2=" + juce::String (r.h2Db, 1)
                    + " H3=" + juce::String (r.h3Db, 1)
                    + " H4=" + juce::String (r.h4Db, 1)
                    + " H5=" + juce::String (r.h5Db, 1)
                    + " THD=" + juce::String (r.thdDb, 1)
                    + " peak=" + juce::String (r.peakDb, 2) + "dBFS"
                    + " rms=" + juce::String (r.rmsDb, 2) + "dBFS"
                    + " DC=" + juce::String (r.dcDb, 1) + "dBc");
    }

    void testDiscreteMatrix()
    {
        beginTest ("Discrete matrix: 9 levels x 4 frequencies");

        const double freqs[] = { 100.0, 1000.0, 5000.0, 10000.0 };
        const double levels[] = { -36.0, -30.0, -24.0, -18.0, -12.0, -9.0, -6.0, -3.0, 0.0 };

        G10DiscreteInputStageCore stage;
        stage.prepare (kRate);

        for (double f : freqs)
        {
            const double freq = exactBinFreq (kRate, kFft, f);
            for (double db : levels)
            {
                const double amp = std::pow (10.0, db / 20.0);
                auto r = measureStage (stage, freq, amp);
                logRow ("DISCRETE", f, db, r);
                expect (std::isfinite (r.fundDb) && std::isfinite (r.thdDb),
                        "Discrete matrix must be finite");
            }
        }
    }

    void testIronMatrix()
    {
        beginTest ("Iron matrix: levels x 7 frequencies");

        const double freqs[] = { 30.0, 50.0, 100.0, 250.0, 1000.0, 5000.0, 10000.0 };
        const double levels[] = { -36.0, -30.0, -24.0, -18.0, -12.0, -9.0, -6.0, -3.0, 0.0 };

        G10IronOutputStageCore stage;
        stage.prepare (kRate);

        for (double f : freqs)
        {
            const double freq = exactBinFreq (kRate, kFft, f);
            for (double db : levels)
            {
                const double amp = std::pow (10.0, db / 20.0);
                auto r = measureStage (stage, freq, amp);
                logRow ("IRON", f, db, r);
                expect (std::isfinite (r.fundDb) && std::isfinite (r.thdDb),
                        "Iron matrix must be finite");
            }
        }
    }

    // -----------------------------------------------------------------------
    // Static transfer: DC ramp (each input held until the state settles),
    // then onset, knee progression, compression, DC mean, asymmetry.
    // -----------------------------------------------------------------------

    template <typename Stage>
    std::vector<float> measureDcTransfer (Stage& stage, const std::vector<float>& xs, int hold) const
    {
        std::vector<float> ys;
        ys.reserve (xs.size());
        juce::AudioBuffer<float> buf (1, hold);
        for (float x : xs)
        {
            buf.clear();
            for (int i = 0; i < hold; ++i)
                buf.setSample (0, i, x);
            stage.process (buf, 1, hold);
            ys.push_back (buf.getSample (0, hold - 1));
        }
        return ys;
    }

    void testStaticTransfer()
    {
        beginTest ("Static transfer: onset, knee, compression, DC, asymmetry");

        std::vector<float> xs;
        for (int i = -200; i <= 200; ++i)
            xs.push_back (i / 100.0f); // -2.0 .. 2.0

        struct TransferStats
        {
            double onset = 0.0;    // |x| where |y-x|/|x| first exceeds 0.1%
            double gainDb[5] = {}; // y/x at x = 0.25, 0.5, 0.75, 1.0, 2.0
            double asymDb = 0.0;   // max |20log10(|y(+a)|/|y(-a)|)|, a in {0.25,0.5,0.75,1.0}
            double dcMean = 0.0;   // mean of y over the symmetric sweep
        };

        auto analyze = [&] (const char* stageLabel, const std::vector<float>& ys) -> TransferStats
        {
            TransferStats s;
            for (int i = 200; i < (int) xs.size(); ++i) // scan from x=0 upward
            {
                const double x = xs[i], y = ys[i];
                if (x > 1e-6 && std::abs (y - x) / x > 0.001)
                {
                    s.onset = x;
                    break;
                }
            }
            const double probes[] = { 0.25, 0.5, 0.75, 1.0, 2.0 };
            for (int k = 0; k < 5; ++k)
            {
                const double x = probes[k];
                const int idx = (int) std::lround (x * 100.0) + 200;
                if (idx >= 0 && idx < (int) ys.size() && std::abs (x) > 1e-6)
                    s.gainDb[k] = 20.0 * std::log10 (std::max (1e-12, std::abs (ys[idx] / x)));
            }
            const double asymProbes[] = { 0.25, 0.5, 0.75, 1.0 };
            for (double a : asymProbes)
            {
                const int ip = (int) std::lround (a * 100.0) + 200;
                const int in = 200 - (int) std::lround (a * 100.0);
                if (ip < 0 || ip >= (int) ys.size() || in < 0 || in >= (int) ys.size())
                    continue;
                const double yp = std::abs (ys[ip]), yn = std::abs (ys[in]);
                if (yp > 1e-12 && yn > 1e-12)
                    s.asymDb = std::max (s.asymDb, std::abs (20.0 * std::log10 (yp / yn)));
            }
            double sum = 0.0;
            for (float y : ys)
                sum += y;
            s.dcMean = sum / ys.size();
            logMessage (juce::String (stageLabel)
                        + " onset=" + juce::String (s.onset, 4) + " (" + juce::String (20.0 * std::log10 (std::max (1e-12, s.onset)), 1) + " dBFS)"
                        + " gain@0.25=" + juce::String (s.gainDb[0], 2)
                        + " @0.5=" + juce::String (s.gainDb[1], 2)
                        + " @0.75=" + juce::String (s.gainDb[2], 2)
                        + " @1.0=" + juce::String (s.gainDb[3], 2)
                        + " @2.0=" + juce::String (s.gainDb[4], 2) + " dB"
                        + " asym=" + juce::String (s.asymDb, 2) + " dB"
                        + " dcMean=" + juce::String (s.dcMean, 6));
            return s;
        };

        {
            G10DiscreteInputStageCore stage;
            stage.prepare (kRate);
            auto ys = measureDcTransfer (stage, xs, 1);
            analyze ("DISCRETE static", ys);
        }
        {
            G10IronOutputStageCore stage;
            stage.prepare (kRate);
            auto ys = measureDcTransfer (stage, xs, 64);
            analyze ("IRON static", ys);
        }
    }

    // ------------------------------------------------------------------
    // Memoryless vs stateful: impulse response tail + block invariance.
    // ------------------------------------------------------------------

    void testMemorylessVsStateful()
    {
        beginTest ("Memoryless vs stateful: impulse tail and block invariance");

        // Impulse response: a memoryless stage returns a single scaled sample
        // (no tail); a stateful stage has a bounded decaying tail. Phase 2B:
        // Discrete is now stateful (the residual-only 2 Hz DC blocker). The
        // intended behavior is a BOUNDED sub-audio state: after a unit
        // impulse the DC estimate jumps to ~residual*dcCoeff (~2e-5) and
        // decays with tau ~79.6 ms. No runaway tail, no audible linear-path
        // coloration (an impulse excites only the residual path).
        {
            G10DiscreteInputStageCore stage;
            stage.prepare (kRate);
            std::vector<float> in (1024, 0.0f);
            in[0] = 1.0f;
            auto out = processStageSignal (stage, in, kBlock);
            double peak = 0.0;
            double maxTail = 0.0;
            for (int i = 0; i < (int) out.size(); ++i)
            {
                peak = std::max (peak, (double) std::abs (out[i]));
                if (i > 0) maxTail = std::max (maxTail, (double) std::abs (out[i]));
            }
            logMessage (juce::String ("DISCRETE impulse: peak=") + juce::String (peak, 4)
                        + " maxTail=" + juce::String (maxTail, 8));
            expect (peak <= 1.0 + 1.0e-6, "Discrete impulse peak must be bounded");
            expect (maxTail < 1.0e-3, "Discrete sub-audio state must be bounded (no runaway tail)");
            expect (maxTail > 0.0, "Discrete must be stateful (bounded residual-blocker tail)");
        }
        {
            G10IronOutputStageCore iron;
            iron.prepare (kRate);
            std::vector<float> in (1024, 0.0f);
            in[0] = 1.0f;
            auto out = processStageSignal (iron, in, kBlock);
            double peak = 0.0;
            for (int i = 0; i < (int) out.size(); ++i)
                peak = std::max (peak, (double) std::abs (out[i]));
            logMessage (juce::String ("IRON impulse: peak=") + juce::String (peak, 4));
            expect (peak <= 1.0 + 1.0e-6, "Iron impulse peak must be bounded (no overshoot)");

            // The iron flux modulates the nonlinear STRENGTH only: for a
            // zero input the nonlinear term (tanh(x) - x) is exactly zero,
            // so the impulse response has NO tail by construction. The
            // stateful character lives in the flux buffer: strong drive
            // charges it, silence decays it, and the charged flux boosts
            // the saturation of subsequent signals. Verified via the
            // documented flux accessor.
            std::vector<float> drive (24000, 1.0f);
            processStageSignal (iron, drive, kBlock);
            const float fluxCharged = std::abs (iron.getFlux (0));
            logMessage (juce::String ("IRON flux: |flux| after DC drive=") + juce::String (fluxCharged, 4));
            expect (fluxCharged > 0.5, "Iron must be stateful (flux charged by strong drive)");
            std::vector<float> silence (24000, 0.0f);
            processStageSignal (iron, silence, kBlock);
            const float fluxDecayed = std::abs (iron.getFlux (0));
            logMessage (juce::String ("IRON flux: |flux| after silence=") + juce::String (fluxDecayed, 8));
            expect (fluxDecayed < 0.1, "Iron flux must decay after silence (bounded memory)");
        }

        // Block invariance: identical input in blocks of 1 vs 512 must give
        // identical output (state must not reset at block boundaries).
        {
            std::vector<float> in;
            for (int i = 0; i < 8192; ++i)
                in.push_back (0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate)
                              + 0.25f * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * i / kRate));

            G10DiscreteInputStageCore d1, d2;
            d1.prepare (kRate); d2.prepare (kRate);
            auto o1 = processStageSignal (d1, in, 1);
            auto o2 = processStageSignal (d2, in, 512);
            bool match = (o1.size() == o2.size());
            if (match)
                for (int i = 0; i < (int) o1.size(); ++i)
                    if (o1[i] != o2[i]) { match = false; break; }
            expect (match, "Discrete block invariance (1 vs 512)");

            G10IronOutputStageCore i1, i2;
            i1.prepare (kRate); i2.prepare (kRate);
            auto o3 = processStageSignal (i1, in, 1);
            auto o4 = processStageSignal (i2, in, 512);
            match = (o3.size() == o4.size());
            if (match)
                for (int i = 0; i < (int) o3.size(); ++i)
                    if (o3[i] != o4[i]) { match = false; break; }
            expect (match, "Iron block invariance (1 vs 512)");
        }
    }
};

static G10AnalogCharacterizationTests g10AnalogCharacterizationTests;

// ============================================================================
// G10VoicingValidationTests — stage-level validation for the Phase 2B
// production stages (G10DiscreteInputStageCore = D1B, G10IronOutputStageCore
// = I1). Chain-level checks (NORMAL/HQ consistency, frozen alias contract,
// realtime allocations, CPU) run in the processor and engine suites.
// ============================================================================

class G10VoicingValidationTests final : public juce::UnitTest
{
public:
    G10VoicingValidationTests() : juce::UnitTest ("G10.Voicing.Validation", "APEX.G10") {}

    void runTest() override
    {
        validateStage<G10DiscreteInputStageCore> ("D1B");
        validateStage<G10IronOutputStageCore> ("I1");
    }

private:
    static constexpr double kRate = 48000.0;
    static constexpr int kBlock = 512;
    static constexpr int kFft = 16384;

    struct ImdResult
    {
        double fundDb = -300.0;  // HF tone gain vs input (dB)
        double imdDb = -300.0;   // RMS of measured products, dBc
        double lowH2Db = -300.0, lowH3Db = -300.0; // LF tone harmonics, dBc
        double dcDb = -300.0;    // DC bin, dBc
    };

    // Two-tone IMD measurement. All tones are exact FFT bins (the caller
    // passes exact-bin frequencies), so the products land on exact bins.
    // The sideband set skips the other input tone when it coincides with a
    // sideband position (CCIF case).
    template <typename Stage>
    ImdResult measureImd (Stage& stage, double fLow, double aLow, double fHigh, double aHigh) const
    {
        ImdResult r;
        const double binHz = kRate / kFft;
        const int settle = (int) (kRate * 0.5);
        const int total = settle + kFft;
        juce::AudioBuffer<float> buf (1, kBlock);
        std::vector<float> out;
        out.reserve (total);
        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (kBlock, total - pos);
            buf.clear();
            for (int i = 0; i < n; ++i)
            {
                const double t = 2.0 * juce::MathConstants<double>::pi * (pos + i) / kRate;
                buf.setSample (0, i, (float) (aLow * std::sin (fLow * t) + aHigh * std::sin (fHigh * t)));
            }
            stage.process (buf, 1, n);
            for (int i = 0; i < n; ++i)
                out.push_back (buf.getSample (0, i));
            pos += n;
        }

        G10Test::Radix2Fft fft (kFft);
        std::vector<float> real (kFft, 0.0f), imag (kFft, 0.0f);
        for (int i = 0; i < kFft; ++i)
            real[i] = out[settle + i];
        fft.transform (real.data(), imag.data());

        auto magAt = [&] (double f) -> double
        {
            const int bin = (int) std::lround (f / binHz);
            if (bin < 0 || bin >= kFft) return 0.0;
            return std::sqrt ((double) real[bin] * real[bin] + (double) imag[bin] * imag[bin]);
        };

        const double fund = magAt (fHigh);
        if (fund <= 1e-12)
            return r;
        r.fundDb = 20.0 * std::log10 (std::max (1e-12, fund / (aHigh * kFft / 2.0)));
        r.lowH2Db = 20.0 * std::log10 (std::max (1e-12, magAt (2.0 * fLow) / fund));
        r.lowH3Db = 20.0 * std::log10 (std::max (1e-12, magAt (3.0 * fLow) / fund));
        r.dcDb = 20.0 * std::log10 (std::max (1e-12, magAt (0.0) / fund));

        // Sidebands around the HF tone: fHigh +- k*fLow, k = 1..4. Skip the
        // lower k=1 sideband when it is the other input tone (fLow).
        double sumSq = 0.0;
        for (int k = 1; k <= 4; ++k)
        {
            double m = magAt (fHigh + k * fLow);
            const double lower = fHigh - k * fLow;
            if (std::abs (lower - fLow) > 0.5 * binHz)
                m += magAt (lower);
            sumSq += m * m;
        }
        r.imdDb = 20.0 * std::log10 (std::max (1e-12, std::sqrt (sumSq) / fund));
        return r;
    }

    void logImd (const char* name, const char* label, const ImdResult& r)
    {
        logMessage (juce::String (name) + " " + label
                    + " gain=" + juce::String (r.fundDb, 2) + "dB"
                    + " IMD=" + juce::String (r.imdDb, 1) + "dBc"
                    + " lowH2=" + juce::String (r.lowH2Db, 1)
                    + " lowH3=" + juce::String (r.lowH3Db, 1)
                    + " DC=" + juce::String (r.dcDb, 1) + "dBc");
    }

    template <typename Stage>
    void testImd (const char* name)
    {
        beginTest (juce::String (name) + " IMD: SMPTE 60+7k, CCIF 19+20k, APEX 100+5k");

        Stage stage;
        stage.prepare (kRate);

        // SMPTE: 60 Hz at 0.5 + 7 kHz at 0.125 (4:1). Exact bins: 58.59 Hz
        // (bin 20), 6998.5 Hz (bin 2389). Products at 6998.5 +- k*58.59.
        {
            const double fLow = exactBinFreq (kRate, kFft, 60.0);
            const double fHigh = exactBinFreq (kRate, kFft, 7000.0);
            auto r = measureImd (stage, fLow, 0.5, fHigh, 0.125);
            logImd (name, "SMPTE 60+7k", r);
            expect (std::isfinite (r.imdDb), "SMPTE IMD must be finite");
        }
        // CCIF: 19 kHz + 20 kHz, 1:1 at 0.5 each (peak 1.0). Exact bins:
        // 18999.0 Hz (bin 6485), 20001.0 Hz (bin 6827); products at
        // 1001.95 Hz (bin 342, 2nd), 17997 Hz (bin 6143, 3rd),
        // 22003 Hz (bin 7511, 3rd).
        {
            const double f1 = exactBinFreq (kRate, kFft, 19000.0);
            const double f2 = exactBinFreq (kRate, kFft, 20000.0);
            const double diff = f2 - f1;
            auto r = measureImd (stage, diff, 0.5, f2, 0.5);
            logImd (name, "CCIF 19+20k", r);
            expect (std::isfinite (r.imdDb), "CCIF IMD must be finite");
        }
        // APEX: 100 Hz (4:1) + 5 kHz. Exact bins: 99.6 Hz (bin 34),
        // 5001.0 Hz (bin 1707).
        {
            const double fLow = exactBinFreq (kRate, kFft, 100.0);
            const double fHigh = exactBinFreq (kRate, kFft, 5000.0);
            auto r = measureImd (stage, fLow, 0.5, fHigh, 0.125);
            logImd (name, "APEX 100+5k", r);
            expect (std::isfinite (r.imdDb), "APEX IMD must be finite");
        }
    }

    template <typename Stage>
    void testTransparency (const char* name)
    {
        beginTest (juce::String (name) + " low-level transparency 20 Hz - 20 kHz");

        Stage stage;
        stage.prepare (kRate);

        const double freqs[] = { 20.0, 50.0, 100.0, 250.0, 1000.0, 5000.0, 10000.0, 20000.0 };
        for (double f : freqs)
        {
            const double freq = exactBinFreq (kRate, kFft, f);
            auto r = measureStageAtRate (stage, kRate, kFft, freq, std::pow (10.0, -36.0 / 20.0));
            logMessage (juce::String (name) + " transparency f=" + juce::String (f, 0)
                        + " gain=" + juce::String (r.fundDb, 3) + "dB"
                        + " THD=" + juce::String (r.thdDb, 1) + "dBc");
            expect (std::abs (r.fundDb) < 0.05, "transparency: gain must be ~0 dB");
            expect (r.thdDb < -60.0, "transparency: THD must be below -60 dBc");
        }
    }

    template <typename Stage>
    void testDc (const char* name)
    {
        beginTest (juce::String (name) + " DC: steady sine, two-tone, asymmetric transient, silence after drive");

        Stage stage;
        stage.prepare (kRate);

        // Steady sine at 0 dBFS 100 Hz.
        {
            const double freq = exactBinFreq (kRate, kFft, 100.0);
            auto r = measureStageAtRate (stage, kRate, kFft, freq, 1.0);
            logMessage (juce::String (name) + " DC steady 0dBFS: " + juce::String (r.dcDb, 1) + " dBc");
        }
        // Two-tone 100 Hz + 1 kHz at -6 dBFS each.
        {
            const double f1 = exactBinFreq (kRate, kFft, 100.0);
            const double f2 = exactBinFreq (kRate, kFft, 1000.0);
            auto r = measureImd (stage, f1, 0.5, f2, 0.5);
            logMessage (juce::String (name) + " DC two-tone: " + juce::String (r.dcDb, 1) + " dBc");
        }
        // Asymmetric transient: half-wave rectified 100 Hz burst.
        {
            stage.reset();
            const int n = (int) (kRate * 0.2);
            std::vector<float> in (n);
            for (int i = 0; i < n; ++i)
            {
                const double v = std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * i / kRate);
                in[i] = (float) (0.5 * std::max (0.0, v));
            }
            auto out = processStageSignal (stage, in, kBlock);
            double mean = 0.0;
            for (int i = (int) (kRate * 0.1); i < n; ++i)
                mean += out[i];
            mean /= (n - (int) (kRate * 0.1));
            logMessage (juce::String (name) + " DC asymmetric transient: " + juce::String (mean, 6));
        }
        // Silence after heavy drive: 0 dBFS 30 Hz for 0.5 s, then silence.
        // Stateful stages (DC blocker) have a controlled exponential tail
        // (tau ~ 80 ms at 2 Hz); the contract is that the tail decays below
        // -100 dBFS within 0.75 s of silence (last 0.25 s of the window).
        {
            stage.reset();
            const int drive = (int) (kRate * 0.5);
            std::vector<float> in (drive + (int) kRate, 0.0f);
            for (int i = 0; i < drive; ++i)
                in[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 30.0 * i / kRate);
            auto out = processStageSignal (stage, in, kBlock);
            double maxSilence = 0.0;
            double maxTail = 0.0;
            const int tailStart = drive + (int) (kRate * 0.75);
            for (int i = drive; i < (int) out.size(); ++i)
            {
                maxSilence = std::max (maxSilence, (double) std::abs (out[i]));
                if (i >= tailStart)
                    maxTail = std::max (maxTail, (double) std::abs (out[i]));
            }
            logMessage (juce::String (name) + " DC silence after drive: peak=" + juce::String (maxSilence, 8)
                        + " tail(last 0.25s)=" + juce::String (maxTail, 8));
            expect (maxTail < 1e-5, "silence after heavy drive: tail must decay below -100 dBFS within 0.75 s");
        }
    }

    template <typename Stage>
    void testTransients (const char* s)
    {
        beginTest (juce::String (s) + " transients: impulse, bursts, kick, broadband");

        Stage stage;
        stage.prepare (kRate);

        // Impulse.
        {
            stage.reset();
            std::vector<float> in (4096, 0.0f);
            in[0] = 1.0f;
            auto out = processStageSignal (stage, in, kBlock);
            double peak = 0.0;
            for (float v : out) peak = std::max (peak, (double) std::abs (v));
            logMessage (juce::String (s) + " impulse peak=" + juce::String (peak, 4));
            expect (peak <= 1.0 + 1e-3, "impulse must not overshoot");
        }
        // 1 kHz burst: 10 cycles at 0 dBFS.
        {
            stage.reset();
            const int n = (int) (kRate * 10.0 / 1000.0);
            std::vector<float> in (n + (int) (kRate * 0.05), 0.0f);
            for (int i = 0; i < n; ++i)
                in[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / kRate);
            auto out = processStageSignal (stage, in, kBlock);
            double peak = 0.0;
            for (float v : out) peak = std::max (peak, (double) std::abs (v));
            logMessage (juce::String (s) + " 1k burst peak=" + juce::String (peak, 4));
            expect (peak <= 1.0 + 1e-3, "1k burst must not overshoot");
        }
        // 50 Hz burst, 5 cycles at 0 dBFS.
        {
            stage.reset();
            const int n = (int) (kRate * 5.0 / 50.0);
            std::vector<float> in (n + (int) (kRate * 0.1), 0.0f);
            for (int i = 0; i < n; ++i)
                in[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * i / kRate);
            auto out = processStageSignal (stage, in, kBlock);
            double peak = 0.0;
            for (float v : out) peak = std::max (peak, (double) std::abs (v));
            logMessage (juce::String (s) + " 50Hz burst peak=" + juce::String (peak, 4));
            expect (peak <= 1.0 + 1e-3, "50 Hz burst must not overshoot");
        }
        // Kick-like burst: 50 Hz, 8 cycles, exponential decay.
        {
            stage.reset();
            const int n = (int) (kRate * 8.0 / 50.0);
            std::vector<float> in (n + (int) (kRate * 0.1), 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const double env = std::exp (-3.0 * i / (double) n);
                in[i] = (float) (env * std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * i / kRate));
            }
            auto out = processStageSignal (stage, in, kBlock);
            double peak = 0.0;
            for (float v : out) peak = std::max (peak, (double) std::abs (v));
            logMessage (juce::String (s) + " kick peak=" + juce::String (peak, 4));
            expect (peak <= 1.0 + 1e-3, "kick burst must not overshoot");
        }
        // Broadband noise burst.
        {
            stage.reset();
            const int n = (int) (kRate * 0.1);
            std::vector<float> in (n + (int) (kRate * 0.1), 0.0f);
            juce::Random rng (0x5EED);
            for (int i = 0; i < n; ++i)
                in[i] = (float) (0.5 * (rng.nextFloat() * 2.0f - 1.0f));
            auto out = processStageSignal (stage, in, kBlock);
            double peak = 0.0;
            for (float v : out) peak = std::max (peak, (double) std::abs (v));
            logMessage (juce::String (s) + " noise peak=" + juce::String (peak, 4));
            expect (std::isfinite (peak), "noise burst must be finite");
        }
    }

    template <typename Stage>
    void testBlockPartitions (const char* s)
    {
        beginTest (juce::String (s) + " block-partition invariance 1..1024");

        std::vector<float> in;
        for (int i = 0; i < 8192; ++i)
            in.push_back (0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate)
                          + 0.25f * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * i / kRate));

        const int blocks[] = { 1, 16, 32, 64, 128, 333, 512, 1024 };
        std::vector<float> reference;
        {
            Stage s0;
            s0.prepare (kRate);
            reference = processStageSignal (s0, in, 1);
        }
        for (int b : blocks)
        {
            Stage stage;
            stage.prepare (kRate);
            auto out = processStageSignal (stage, in, b);
            bool match = (out.size() == reference.size());
            if (match)
                for (int i = 0; i < (int) out.size(); ++i)
                    if (out[i] != reference[i]) { match = false; break; }
            expect (match, juce::String (s) + " block invariance at " + juce::String (b));
        }
    }

    template <typename Stage>
    void testResetDeterminism (const char* s)
    {
        beginTest (juce::String (s) + " reset determinism");

        std::vector<float> in;
        for (int i = 0; i < 4096; ++i)
            in.push_back (0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate)
                          + 0.25f * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * i / kRate));

        Stage a, b;
        a.prepare (kRate); b.prepare (kRate);
        auto o1 = processStageSignal (a, in, kBlock);
        b.reset();
        auto o2 = processStageSignal (b, in, kBlock);
        bool match = (o1.size() == o2.size());
        if (match)
            for (int i = 0; i < (int) o1.size(); ++i)
                if (o1[i] != o2[i]) { match = false; break; }
        expect (match, juce::String (s) + " reset determinism (identical output after reset)");
    }

    template <typename Stage>
    void testSilenceRecovery (const char* s)
    {
        beginTest (juce::String (s) + " silence recovery after strong LF drive");

        Stage stage;
        stage.prepare (kRate);
        const int drive = (int) (kRate * 0.5);
        std::vector<float> in (drive + (int) (kRate * 1.0), 0.0f);
        for (int i = 0; i < drive; ++i)
            in[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 30.0 * i / kRate);
        auto out = processStageSignal (stage, in, kBlock);
        double maxTail = 0.0;
        const int tailStart = drive + (int) (kRate * 0.75);
        for (int i = drive; i < (int) out.size(); ++i)
            if (i >= tailStart)
                maxTail = std::max (maxTail, (double) std::abs (out[i]));
        logMessage (juce::String (s) + " silence tail max(last 0.25s)=" + juce::String (maxTail, 8));
        expect (maxTail < 1e-5, "no uncontrolled long memory after strong LF drive (tail must decay below -100 dBFS within 0.75 s)");
    }

    template <typename Stage>
    void testStereo (const char* s)
    {
        beginTest (juce::String (s) + " stereo integrity: L=R, L-only, no crosstalk");

        std::vector<float> in;
        for (int i = 0; i < 4096; ++i)
            in.push_back (0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate));

        // L = R -> L = R (bit-exact).
        {
            Stage st;
            st.prepare (kRate);
            juce::AudioBuffer<float> buf (2, kBlock);
            std::vector<float> l, r;
            int pos = 0;
            while (pos < (int) in.size())
            {
                const int n = std::min (kBlock, (int) in.size() - pos);
                buf.clear();
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample (0, i, in[pos + i]);
                    buf.setSample (1, i, in[pos + i]);
                }
                st.process (buf, 2, n);
                for (int i = 0; i < n; ++i)
                {
                    l.push_back (buf.getSample (0, i));
                    r.push_back (buf.getSample (1, i));
                }
                pos += n;
            }
            bool match = (l.size() == r.size());
            if (match)
                for (int i = 0; i < (int) l.size(); ++i)
                    if (l[i] != r[i]) { match = false; break; }
            expect (match, juce::String (s) + " L=R input must give identical L/R output");
        }

        // L-only -> R output exactly zero (no crosstalk, no shared state).
        {
            Stage b;
            b.prepare (kRate);
            juce::AudioBuffer<float> buf (2, kBlock);
            std::vector<float> r;
            int pos = 0;
            while (pos < (int) in.size())
            {
                const int n = std::min (kBlock, (int) in.size() - pos);
                buf.clear();
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, in[pos + i]);
                b.process (buf, 2, n);
                for (int i = 0; i < n; ++i)
                    r.push_back (buf.getSample (1, i));
                pos += n;
            }
            bool clean = true;
            for (float v : r)
                if (v != 0.0f) { clean = false; break; }
            expect (clean, juce::String (s) + " L-only must not leak into R");
        }
    }

    template <typename Stage>
    void testSampleRates (const char* s)
    {
        beginTest (juce::String (s) + " sample-rate consistency 44.1..192 kHz");

        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        for (double rate : rates)
        {
            Stage stage;
            stage.prepare (rate);
            const int fft = 16384;
            const double f1k = exactBinFreq (rate, fft, 1000.0);
            const double f30 = exactBinFreq (rate, fft, 30.0);
            auto r1 = measureStageAtRate (stage, rate, fft, f1k, 1.0);
            auto r30 = measureStageAtRate (stage, rate, fft, f30, 1.0);
            logMessage (juce::String (s) + " rate=" + juce::String (rate, 0)
                        + " 1k THD=" + juce::String (r1.thdDb, 1)
                        + " gain=" + juce::String (r1.fundDb, 2)
                        + " 30Hz THD=" + juce::String (r30.thdDb, 1)
                        + " gain=" + juce::String (r30.fundDb, 2));
            expect (std::isfinite (r1.thdDb) && std::isfinite (r30.thdDb),
                    "sample-rate matrix must be finite");
        }
    }

    template <typename Stage>
    void testExtremeSafety (const char* s)
    {
        beginTest (juce::String (s) + " extreme safety: silence, denormal, 0 dBFS, multitone, impulse, +/-18 dB");

        Stage stage;
        stage.prepare (kRate);

        // Silence -> exactly zero.
        {
            stage.reset();
            std::vector<float> in (4096, 0.0f);
            auto out = processStageSignal (stage, in, kBlock);
            bool clean = true;
            for (float v : out)
                if (v != 0.0f) { clean = false; break; }
            expect (clean, juce::String (s) + " silence must produce exactly zero");
        }
        // Denormal-level input -> no denormal accumulation, finite.
        {
            stage.reset();
            std::vector<float> in (4096, 1.0e-30f);
            auto out = processStageSignal (stage, in, kBlock);
            bool finite = true;
            for (float v : out)
                if (! std::isfinite (v)) { finite = false; break; }
            expect (finite, juce::String (s) + " denormal input must stay finite");
        }
        // 0 dBFS sine, multitone, impulse, +/-18 dB trim: finite, no NaN.
        {
            const double cases[][2] = { { 1000.0, 1.0 }, { 30.0, 1.0 }, { 1000.0, 7.94 }, { 1000.0, 0.126 } };
            for (auto& c : cases)
            {
                stage.reset();
                const int n = (int) (kRate * 0.1);
                std::vector<float> in (n);
                for (int i = 0; i < n; ++i)
                    in[i] = (float) (c[1] * std::sin (2.0 * juce::MathConstants<double>::pi * c[0] * i / kRate));
                auto out = processStageSignal (stage, in, kBlock);
                bool finite = true;
                for (float v : out)
                    if (! std::isfinite (v)) { finite = false; break; }
                expect (finite, juce::String (s) + " sine at amp " + juce::String (c[1], 3) + " must be finite");
            }
        }
        // Multitone: 8 tones at -12 dBFS each.
        {
            stage.reset();
            const int n = (int) (kRate * 0.1);
            std::vector<float> in (n);
            const double freqs[] = { 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0, 7040.0 };
            for (int i = 0; i < n; ++i)
            {
                double v = 0.0;
                for (double f : freqs)
                    v += 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / kRate);
                in[i] = (float) v;
            }
            auto out = processStageSignal (stage, in, kBlock);
            bool finite = true;
            for (float v : out)
                if (! std::isfinite (v)) { finite = false; break; }
            expect (finite, juce::String (s) + " multitone must be finite");
        }
        // Impulse at 1.0.
        {
            stage.reset();
            std::vector<float> in (4096, 0.0f);
            in[0] = 1.0f;
            auto out = processStageSignal (stage, in, kBlock);
            bool finite = true;
            for (float v : out)
                if (! std::isfinite (v)) { finite = false; break; }
            expect (finite, juce::String (s) + " impulse must be finite");
        }
        // No foldback: static DC transfer must be monotonic up to x = 8
        // (covers +18 dB trim).
        {
            std::vector<float> xs;
            for (int i = -800; i <= 800; ++i)
                xs.push_back (i / 100.0f); // -8.0 .. 8.0
            auto ys = measureDcTransfer (stage, xs, 1024);
            bool monotonic = true;
            for (int i = 1; i < (int) ys.size(); ++i)
                if (ys[i] < ys[i - 1]) { monotonic = false; break; }
            expect (monotonic, juce::String (s) + " static transfer must be monotonic (no foldback)");
        }
    }

    // ---- shared measurement helpers (rate-parameterized) ----

    struct StageResult
    {
        double fundDb = -300.0;
        double thdDb = -300.0;
        double dcDb = -300.0;
    };

    template <typename Stage>
    StageResult measureStageAtRate (Stage& stage, double rate, int fft, double freq, double amp) const
    {
        StageResult r;
        const int settle = (int) (rate * 0.5);
        const int total = settle + fft;
        juce::AudioBuffer<float> buf (1, kBlock);
        std::vector<float> out;
        out.reserve (total);
        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (kBlock, total - pos);
            buf.clear();
            for (int i = 0; i < n; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi
                                                              * freq * (pos + i) / rate)));
            stage.process (buf, 1, n);
            for (int i = 0; i < n; ++i)
                out.push_back (buf.getSample (0, i));
            pos += n;
        }
        G10Test::Radix2Fft f (fft);
        std::vector<float> real (fft, 0.0f), imag (fft, 0.0f);
        for (int i = 0; i < fft; ++i)
            real[i] = out[settle + i];
        f.transform (real.data(), imag.data());
        const double binHz = rate / fft;
        auto magAt = [&] (double fq) -> double
        {
            const int bin = (int) std::lround (fq / binHz);
            if (bin < 0 || bin >= fft) return 0.0;
            return std::sqrt ((double) real[bin] * real[bin] + (double) imag[bin] * imag[bin]);
        };
        const double fund = magAt (freq);
        if (fund <= 1e-12)
            return r;
        r.fundDb = 20.0 * std::log10 (std::max (1e-12, fund / (amp * fft / 2.0)));
        double sumSq = 0.0;
        for (int k = 2; k <= 5; ++k)
        {
            const double m = magAt (k * freq);
            sumSq += m * m;
        }
        r.thdDb = 20.0 * std::log10 (std::max (1e-12, std::sqrt (sumSq) / fund));
        r.dcDb = 20.0 * std::log10 (std::max (1e-12, magAt (0.0) / fund));
        return r;
    }

    template <typename Stage>
    void validateStage (const char* name)
    {
        testImd<Stage> (name);
        testTransparency<Stage> (name);
        testDc<Stage> (name);
        testTransients<Stage> (name);
        testBlockPartitions<Stage> (name);
        testResetDeterminism<Stage> (name);
        testSilenceRecovery<Stage> (name);
        testStereo<Stage> (name);
        testSampleRates<Stage> (name);
        testExtremeSafety<Stage> (name);
    }

    template <typename Stage>
    std::vector<float> measureDcTransfer (Stage& stage, const std::vector<float>& xs, int hold) const
    {
        // Stateful stages (DC blocker, flux) must be measured from a
        // canonical initial condition: reset per point, otherwise the
        // blocker's history (tau ~ 80 ms at 2 Hz, hold is only ~0.27 tau)
        // carries between sweep points and the sweep reads transient
        // values instead of the settled transfer. With reset, the transfer
        // is y = x + r(x) * (1 - exp(-hold/tau)), strictly monotonic.
        std::vector<float> ys;
        ys.reserve (xs.size());
        juce::AudioBuffer<float> buf (1, hold);
        for (float x : xs)
        {
            stage.reset();
            buf.clear();
            for (int i = 0; i < hold; ++i)
                buf.setSample (0, i, x);
            stage.process (buf, 1, hold);
            ys.push_back (buf.getSample (0, hold - 1));
        }
        return ys;
    }
};

static G10VoicingValidationTests g10VoicingValidationTests;

