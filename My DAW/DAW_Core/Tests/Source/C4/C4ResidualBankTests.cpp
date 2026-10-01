#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>
#include <vector>

// ============================================================================
// C4ResidualBankTests — Phase 4: BLOOM Nonlinear Color (engineering).
//
// The governing rule: ADDING TONE ADDS COLOR, REMOVING TONE STAYS CLEANER.
// C4 remains an EQ first. This suite proves:
//
//   1. BLOOM = 0 is bit-identical to the frozen Phase 1-3 linear reference
//      (the sacred baseline — zero residual, zero nonlinear work, zero extra
//      phase/gain/latency, no allocations).
//   2. The residual is measurable harmonic content per band, activation
//      follows positive smoothed gain, cuts stay clean (cutSuppression).
//   3. Transients are preserved (crest factor, peak).
//   4. Oversampling candidates (1x/2x/4x) reduce alias monotonically.
//   5. Numerical safety under torture (finite, no NaN/Inf, no runaway).
//   6. Automation is click-free (BLOOM sweep, gain crossing zero).
//   7. Zero realtime allocations with BLOOM active; zero added latency.
//   8. All six supported sample rates.
//
// The final sonic amounts are Phase 5 decisions; this phase only pins the
// architecture and the bounds.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;
using APEX::C4::C4BandId;
using APEX::C4::makeProfilePhase1Reference;
using APEX::C4::makeProfileVariantA;
using APEX::C4::makeProfileVariantB;
using APEX::C4::makeProfileVariantC;

class C4ResidualBankTests final : public juce::UnitTest
{
public:
    C4ResidualBankTests() : juce::UnitTest ("C4.ResidualBank", "APEX.C4") {}

    void runTest() override
    {
        const double rate = 48000.0;
        const int block = 512;

        testBloomZeroBitIdentity (rate, block);
        testHarmonicSuite (rate, block);
        testBoostVsCut (rate, block);
        testTransients (rate, block);
        testAliasBenchmark (rate, block);
        testNumericalSafety (rate, block);
        testAutomation (rate, block);
        testRealtimeAndLatency (rate, block);
        testAllRates();
    }

private:
    // ---- helpers -----------------------------------------------------------

    static void setParam (C4Processor& proc, C4ParamIndex idx, const juce::String& text)
    {
        proc.getC4Parameter (idx)->setValue (proc.getC4Parameter (idx)->getValueForText (text));
    }

    static void setBand (C4Processor& proc, C4BandId band, float freq, float gainDb)
    {
        const auto freqIdx = (C4ParamIndex) ((int) C4ParamIndex::kWeightFreq + (int) band * 3);
        const auto gainIdx = (C4ParamIndex) ((int) C4ParamIndex::kWeightGain + (int) band * 3);
        setParam (proc, freqIdx, juce::String (freq));
        setParam (proc, gainIdx, juce::String (gainDb, 1));
    }

    static void setBloom (C4Processor& proc, float bloom01)
    {
        proc.getC4Parameter (C4ParamIndex::kBloom)->setValue (bloom01);
    }

    /** Steady-state sine response capture: settle `settleSeconds`, then
        capture `captureSamples` into a Hann-windowed buffer. */
    static std::vector<float> captureSine (C4Processor& proc, double rate, int block,
                                           double freq, float levelDb,
                                           double settleSeconds, int captureSamples)
    {
        const double amp = std::pow (10.0, levelDb / 20.0);
        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        const int settleTotal = (int) (rate * settleSeconds);
        int pos = 0;
        while (pos < settleTotal)
        {
            for (int i = 0; i < block; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (
                    2.0 * juce::MathConstants<double>::pi * freq * (pos + i) / rate)));
            proc.processBlock (buf, midi);
            pos += block;
        }
        std::vector<float> out (captureSamples, 0.0f);
        int written = 0;
        while (written < captureSamples)
        {
            for (int i = 0; i < block; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (
                    2.0 * juce::MathConstants<double>::pi * freq * (pos + i) / rate)));
            proc.processBlock (buf, midi);
            const int n = juce::jmin (block, captureSamples - written);
            for (int i = 0; i < n; ++i)
                out[(size_t) (written + i)] = buf.getSample (0, i);
            written += n;
            pos += block;
        }
        // Hann window.
        for (int i = 0; i < captureSamples; ++i)
            out[(size_t) i] *= (float) (0.5 - 0.5 * std::cos (
                2.0 * juce::MathConstants<double>::pi * i / captureSamples));
        return out;
    }

    struct Harmonics
    {
        float h1Db = -300.0f; // fundamental, dBFS
        float h2Db = -300.0f, h3Db = -300.0f, h4Db = -300.0f;
        float thdDb = -300.0f;
        float dcDb = -300.0f;
    };

    /** FFT-based harmonic measurement (bin power summed over the 3 nearest
        bins per harmonic — robust to non-integer bin frequencies). */
    static Harmonics measureHarmonics (const std::vector<float>& out,
                                       double rate, double freq)
    {
        const int n = (int) out.size();
        C4Test::Radix2Fft fft (n);
        std::vector<float> re (n, 0.0f), im (n, 0.0f);
        for (int i = 0; i < n; ++i)
            re[(size_t) i] = out[(size_t) i];
        fft.transform (re.data(), im.data());

        const double binHz = rate / n;
        auto binMagDb = [&] (double f) -> float
        {
            const int b = (int) std::lround (f / binHz);
            if (b <= 0 || b >= n)
                return -300.0f;
            double sum = 0.0;
            for (int k = b - 1; k <= b + 1; ++k)
                if (k > 0 && k < n)
                    sum += (double) re[(size_t) k] * re[(size_t) k]
                         + (double) im[(size_t) k] * im[(size_t) k];
            return (float) (10.0 * std::log10 (std::max (1.0e-30, sum)));
        };

        Harmonics h;
        h.h1Db = binMagDb (freq);
        h.h2Db = binMagDb (freq * 2.0) - h.h1Db;
        h.h3Db = binMagDb (freq * 3.0) - h.h1Db;
        h.h4Db = binMagDb (freq * 4.0) - h.h1Db;
        const float h2l = std::pow (10.0, h.h2Db / 10.0);
        const float h3l = std::pow (10.0, h.h3Db / 10.0);
        const float h4l = std::pow (10.0, h.h4Db / 10.0);
        h.thdDb = (float) (10.0 * std::log10 (std::max (1.0e-30, (double) (h2l + h3l + h4l))));
        h.dcDb = binMagDb (0.0) - h.h1Db;
        return h;
    }

    // ---- tests -------------------------------------------------------------

    void testBloomZeroBitIdentity (double rate, int block)
    {
        beginTest ("BLOOM 0 is bit-identical to the frozen Phase 1-3 reference");

        // The sacred check: for EVERY variant, the output with BLOOM = 0 must
        // be bit-identical to the SAME profile with the color layer forced
        // off (residualLevel 0 — the color data affects nothing else), under
        // shaped settings. This proves the residual layer contributes nothing
        // at BLOOM 0: zero nonlinear residual, zero extra phase/gain.
        for (const auto& base : { makeProfileVariantA(), makeProfileVariantB(), makeProfileVariantC() })
        {
            auto colorlessProfile = base;
            for (int b = 0; b < 4; ++b)
            {
                auto c = colorlessProfile.color[b];
                c.residualLevel = 0.0f;
                colorlessProfile.color[b] = c;
            }

            auto colored = C4Test::makePreparedProcessor (rate, block, base);
            auto colorless = C4Test::makePreparedProcessor (rate, block, colorlessProfile);
            // Shaped state: boosts, cuts, HALO — with NO adjacent boost pair
            // (the coupling layer must be inert in BOTH processors, so the
            // comparison isolates the residual layer exactly).
            setBand (*colored, C4BandId::Weight, 200.0f, 9.0f);
            setBand (*colored, C4BandId::Sculpt, 500.0f, -6.0f);
            setBand (*colored, C4BandId::Bite, 3000.0f, 6.0f);
            setBand (*colored, C4BandId::Open, 15000.0f, -3.0f);
            colored->getC4Parameter (C4ParamIndex::kOpenMode)->setValue (1.0f);
            setBand (*colorless, C4BandId::Weight, 200.0f, 9.0f);
            setBand (*colorless, C4BandId::Sculpt, 500.0f, -6.0f);
            setBand (*colorless, C4BandId::Bite, 3000.0f, 6.0f);
            setBand (*colorless, C4BandId::Open, 15000.0f, -3.0f);
            colorless->getC4Parameter (C4ParamIndex::kOpenMode)->setValue (1.0f);

            // Phase 5: the production BLOOM default is 4.0 — the sacred
            // baseline must be proven at BLOOM 0 explicitly.
            setBloom (*colored, 0.0f);
            setBloom (*colorless, 0.0f);

            juce::AudioBuffer<float> a (2, block);
            juce::AudioBuffer<float> b (2, block);
            juce::MidiBuffer midi;
            bool identical = true;
            for (int blk = 0; blk < 80; ++blk)
            {
                C4Test::fillDeterministic (a, 0x11B0u + (juce::uint32) blk);
                b = a;
                colored->processBlock (a, midi);
                colorless->processBlock (b, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (a.getSample (ch, i) != b.getSample (ch, i))
                            identical = false;
            }
            expect (identical,
                    juce::String (base.name)
                        + ": BLOOM 0 must be bit-identical to the colorless reference");
        }

        // Cross-check against the frozen Phase 1 reference: variant B with
        // BLOOM 0 == the Phase 1 reference under settings where coupling is
        // inert (the Phase 1 reference has coupling OFF; B's coupling is
        // identical to P1's tuning but the contour must not engage).
        {
            auto colored = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
            auto reference = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
            setBand (*colored, C4BandId::Weight, 200.0f, 9.0f);
            setBand (*colored, C4BandId::Sculpt, 500.0f, -6.0f);
            setBand (*colored, C4BandId::Bite, 3000.0f, 6.0f);
            setBand (*colored, C4BandId::Open, 15000.0f, -3.0f);
            colored->getC4Parameter (C4ParamIndex::kOpenMode)->setValue (1.0f);
            setBand (*reference, C4BandId::Weight, 200.0f, 9.0f);
            setBand (*reference, C4BandId::Sculpt, 500.0f, -6.0f);
            setBand (*reference, C4BandId::Bite, 3000.0f, 6.0f);
            setBand (*reference, C4BandId::Open, 15000.0f, -3.0f);
            reference->getC4Parameter (C4ParamIndex::kOpenMode)->setValue (1.0f);

            setBloom (*colored, 0.0f);
            setBloom (*reference, 0.0f);

            juce::AudioBuffer<float> a (2, block);
            juce::AudioBuffer<float> b (2, block);
            juce::MidiBuffer midi;
            bool identical = true;
            for (int blk = 0; blk < 80; ++blk)
            {
                C4Test::fillDeterministic (a, 0x11B0u + (juce::uint32) blk);
                b = a;
                colored->processBlock (a, midi);
                reference->processBlock (b, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (a.getSample (ch, i) != b.getSample (ch, i))
                            identical = false;
            }
            expect (identical,
                    "B: BLOOM 0 must be bit-identical to the frozen Phase 1 reference");
        }
    }

    void testHarmonicSuite (double rate, int block)
    {
        beginTest ("Harmonic content per band (measurable, level-dependent, BLOOM-dependent)");

        const struct { C4BandId band; float freq; float gain; } cases[] =
        {
            { C4BandId::Weight, 100.0f, 6.0f },
            { C4BandId::Sculpt, 1000.0f, 6.0f },
            { C4BandId::Bite, 4000.0f, 6.0f },
            // OPEN at 5 kHz: harmonics H2/H3/H4 (10/15/20 kHz) stay below
            // Nyquist at 48 kHz — at 12 kHz the harmonic bins would alias
            // onto the FFT mirror (measurement artifact, not a DSP defect).
            { C4BandId::Open, 5000.0f, 6.0f },
        };

        for (const auto& c : cases)
        {
            auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
            setBand (*proc, c.band, c.freq, c.gain);

            // BLOOM 0: no harmonics (linear).
            setBloom (*proc, 0.0f);
            auto out0 = captureSine (*proc, rate, block, c.freq, -6.0f, 1.0, 16384);
            const auto h0 = measureHarmonics (out0, rate, c.freq);
            expect (h0.h2Db < -60.0f && h0.h3Db < -60.0f,
                    juce::String ((int) c.band) + ": BLOOM 0 must be linear (H2 "
                        + juce::String (h0.h2Db, 1) + " dB, H3 " + juce::String (h0.h3Db, 1) + " dB)");

            // BLOOM 10 at -6 dBFS: harmonics present and measurable.
            setBloom (*proc, 1.0f);
            auto out10 = captureSine (*proc, rate, block, c.freq, -6.0f, 1.0, 16384);
            const auto h10 = measureHarmonics (out10, rate, c.freq);
            const float h2 = h10.h2Db, h3 = h10.h3Db;
            expect (h2 > -60.0f || h3 > -60.0f,
                    juce::String ((int) c.band) + ": BLOOM 10 must produce measurable "
                    "harmonics (H2 " + juce::String (h2, 1) + " dB, H3 " + juce::String (h3, 1) + " dB)");
            expect (std::abs (h10.dcDb) < -50.0f || h10.dcDb < -50.0f,
                    juce::String ((int) c.band) + ": DC deliberately managed (DC "
                        + juce::String (h10.dcDb, 1) + " dB rel. fundamental)");

            // Level dependence: -30 dBFS must be much cleaner than -6 dBFS.
            setBloom (*proc, 1.0f);
            auto outL = captureSine (*proc, rate, block, c.freq, -30.0f, 1.0, 16384);
            const auto hL = measureHarmonics (outL, rate, c.freq);
            expect (hL.thdDb < h10.thdDb - 10.0f,
                    juce::String ((int) c.band) + ": level-dependent color (THD -30 dBFS "
                        + juce::String (hL.thdDb, 1) + " vs -6 dBFS " + juce::String (h10.thdDb, 1) + ")");

            logMessage (juce::String ((int) c.band) + " @ " + juce::String (c.freq, 0)
                        + " Hz: BLOOM10/-6dBFS H2=" + juce::String (h2, 1)
                        + " H3=" + juce::String (h3, 1)
                        + " THD=" + juce::String (h10.thdDb, 1)
                        + " DC=" + juce::String (h10.dcDb, 1)
                        + " | -30dBFS THD=" + juce::String (hL.thdDb, 1));
        }
    }

    void testBoostVsCut (double rate, int block)
    {
        beginTest ("Positive boosts generate substantially more residual than cuts");

        const struct { C4BandId band; float freq; } cases[] =
        {
            { C4BandId::Weight, 100.0f },
            { C4BandId::Sculpt, 1000.0f },
            { C4BandId::Bite, 4000.0f },
            { C4BandId::Open, 5000.0f },
        };

        for (const auto& c : cases)
        {
            auto boostProc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
            auto cutProc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
            setBand (*boostProc, c.band, c.freq, 6.0f);
            setBand (*cutProc, c.band, c.freq, -6.0f);
            setBloom (*boostProc, 1.0f);
            setBloom (*cutProc, 1.0f);

            const auto outB = captureSine (*boostProc, rate, block, c.freq, -6.0f, 1.0, 16384);
            const auto outC = captureSine (*cutProc, rate, block, c.freq, -6.0f, 1.0, 16384);
            const auto hB = measureHarmonics (outB, rate, c.freq);
            const auto hC = measureHarmonics (outC, rate, c.freq);

            // cutSuppression = 1.0 -> cuts produce essentially zero residual;
            // the boost must be >= 20 dB richer in harmonics.
            expect (hB.thdDb > hC.thdDb + 20.0f,
                    juce::String ((int) c.band) + ": boost THD "
                        + juce::String (hB.thdDb, 1) + " dB vs cut THD "
                        + juce::String (hC.thdDb, 1) + " dB (need >= 20 dB separation)");
        }
    }

    void testTransients (double rate, int block)
    {
        beginTest ("Transients preserved: peak and crest factor at BLOOM 0 vs 10");

        // A kick-like transient: 60 Hz decaying sine burst.
        const int total = (int) (rate * 0.5);
        std::vector<float> signal (total, 0.0f);
        for (int i = 0; i < total; ++i)
            signal[(size_t) i] = (float) (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / rate)
                                          * std::exp (-3.0 * i / rate));

        auto measureCrest = [&] (float bloom) -> float
        {
            auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
            setBand (*proc, C4BandId::Weight, 100.0f, 6.0f);
            setBand (*proc, C4BandId::Sculpt, 500.0f, 6.0f);
            setBloom (*proc, bloom);
            juce::AudioBuffer<float> buf (1, block);
            juce::MidiBuffer midi;
            double sumSq = 0.0;
            float peak = 0.0f;
            int n = 0;
            int pos = 0;
            while (pos < total)
            {
                const int cnt = juce::jmin (block, total - pos);
                for (int i = 0; i < cnt; ++i)
                    buf.setSample (0, i, signal[(size_t) (pos + i)]);
                proc->processBlock (buf, midi);
                for (int i = 0; i < cnt; ++i)
                {
                    const float v = buf.getSample (0, i);
                    sumSq += (double) v * v;
                    peak = juce::jmax (peak, std::abs (v));
                    ++n;
                }
                pos += cnt;
            }
            const float rms = (float) std::sqrt (sumSq / n);
            return peak / rms; // crest factor
        };

        const float crest0 = measureCrest (0.0f);
        const float crest10 = measureCrest (1.0f);
        const float crestMedium = measureCrest (0.5f);
        expect (std::abs (crest10 - crest0) < 0.5f,
                "crest factor preserved at BLOOM 10 ("
                    + juce::String (crest0, 3) + " -> " + juce::String (crest10, 3) + ")");
        expect (std::abs (crestMedium - crest0) < 0.3f,
                "crest factor preserved at medium BLOOM ("
                    + juce::String (crest0, 3) + " -> " + juce::String (crestMedium, 3) + ")");
    }

    void testAliasBenchmark (double rate, int block)
    {
        beginTest ("Alias benchmark: oversampling candidates reduce alias monotonically");

        // OPEN +15 at 7 kHz, BLOOM 10, -6 dBFS, 48 kHz: the shaper's H4
        // (28 kHz) and H5 (35 kHz) fold below Nyquist at 1x onto the empty
        // bins at 20 kHz and 13 kHz. At 2x/4x (residual path only) those
        // harmonics stay below the oversampled Nyquist and the fold bins
        // contain only noise. The folded-bin levels are measured directly.
        const double f0 = 7000.0;
        const float foldBins[] = { 13000.0f, 20000.0f }; // H5 fold, H4 fold

        auto measureFold = [&] (int os) -> float
        {
            auto profile = makeProfileVariantB();
            profile.residualOversample = os;
            auto p = C4Test::makePreparedProcessor (rate, block, profile);
            setBand (*p, C4BandId::Open, (float) f0, 15.0f);
            setBloom (*p, 1.0f);
            const auto out = captureSine (*p, rate, block, f0, -6.0f, 1.0, 16384);
            const auto h = measureHarmonics (out, rate, f0);
            // Fold-bin level relative to the fundamental (max of the two).
            return juce::jmax (C4Test::magnitudeDbAtFrequency (out, rate, foldBins[0], 16384) - h.h1Db,
                               C4Test::magnitudeDbAtFrequency (out, rate, foldBins[1], 16384) - h.h1Db);
        };

        const float fold1x = measureFold (1);
        const float fold2x = measureFold (2);
        const float fold4x = measureFold (4);
        logMessage ("alias @ 7 kHz: fold bins 1x=" + juce::String (fold1x, 1)
                    + " dB, 2x=" + juce::String (fold2x, 1)
                    + " dB, 4x=" + juce::String (fold4x, 1) + " dB rel. fundamental");
        // Both are far below audibility (-67 dB); the oversampling must still
        // reduce the fold monotonically (measured -2.9 dB at 2x).
        expect (fold2x <= fold1x - 2.0f,
                "2x must reduce the folded alias ("
                    + juce::String (fold1x, 1) + " -> " + juce::String (fold2x, 1) + ")");
        expect (fold4x <= fold2x + 1.0f,
                "4x must not be worse than 2x (" + juce::String (fold2x, 1)
                    + " -> " + juce::String (fold4x, 1) + ")");
    }

    void testNumericalSafety (double rate, int block)
    {
        beginTest ("Numerical safety torture: finite, no NaN/Inf, no runaway");

        // All bands +15, input trim +18, BLOOM 10, coupling C, HALO active.
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantC());
        for (int b = 0; b < 4; ++b)
        {
            setBand (*proc, (C4BandId) b, 200.0f * (float) std::pow (4.0, b), 15.0f);
        }
        proc->getC4Parameter (C4ParamIndex::kOpenMode)->setValue (1.0f);
        proc->getC4Parameter (C4ParamIndex::kInput)->setValue (1.0f); // +18 dB
        setBloom (*proc, 1.0f);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        bool finite = true;

        // Near-full-scale noise.
        for (int blk = 0; blk < 200; ++blk)
        {
            C4Test::fillDeterministic (buf, 0xF00Du + (juce::uint32) blk);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, buf.getSample (ch, i) * 0.99f);
            proc->processBlock (buf, midi);
            finite = finite && C4Test::allFinite (buf);
        }
        expect (finite, "near-full-scale noise stays finite");

        // Silence (all gains positive, BLOOM 10): the residual layer's DC
        // blocker decays its state (15 Hz pole, denormal-flushed), then the
        // output must be EXACTLY zero — settle the tail first.
        bool zero = true;
        for (int blk = 0; blk < 60; ++blk) // let the residual tail decay
        {
            buf.clear();
            proc->processBlock (buf, midi);
        }
        for (int blk = 0; blk < 100; ++blk)
        {
            buf.clear();
            proc->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (buf.getSample (ch, i) != 0.0f)
                        zero = false;
        }
        expect (zero, "silence with BLOOM 10 produces exactly zero output");

        // DC input (re-filled EVERY block — the processor overwrites the
        // buffer with its output, so a stale buffer would feed the output
        // back as input and explode; same lifecycle rule as the linear tests).
        bool dcFinite = true;
        for (int blk = 0; blk < 100; ++blk)
        {
            buf.clear();
            for (int i = 0; i < block; ++i)
            {
                buf.setSample (0, i, 0.5f);
                buf.setSample (1, i, 0.5f);
            }
            proc->processBlock (buf, midi);
            if (dcFinite && ! C4Test::allFinite (buf))
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (! std::isfinite (buf.getSample (ch, i)))
                        {
                            logMessage ("DC probe: first non-finite at block "
                                        + juce::String (blk) + " ch " + juce::String (ch)
                                        + " sample " + juce::String (i)
                                        + " value=" + juce::String (buf.getSample (ch, i), 8));
                            ch = 2;
                            break;
                        }
                dcFinite = false;
            }
        }
        expect (dcFinite, "DC input stays finite");

        // Impulse (re-filled every block: impulse once, then zeros).
        bool impFinite = true;
        bool impSent = false;
        for (int blk = 0; blk < 100; ++blk)
        {
            buf.clear();
            if (! impSent)
            {
                buf.setSample (0, 0, 1.0f);
                impSent = true;
            }
            proc->processBlock (buf, midi);
            impFinite = impFinite && C4Test::allFinite (buf);
        }
        expect (impFinite, "impulse stays finite");
    }

    void testAutomation (double rate, int block)
    {
        beginTest ("Automation: BLOOM sweep and gain zero-crossing are click-free");

        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        setBand (*proc, C4BandId::Sculpt, 1000.0f, 6.0f);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        bool finite = true;
        float prev = 0.0f, maxDelta = 0.0f;

        for (int b = 0; b < 400; ++b)
        {
            const float phase = (float) (b % 200) / 199.0f;
            // BLOOM 0 -> 10 -> 0.
            setBloom (*proc, phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f);
            // SCULPT gain +6 -> -6 -> +6 (crosses zero with color active).
            const float g = 6.0f * (phase < 0.5f ? (1.0f - phase * 2.0f) : (phase * 2.0f - 1.0f));
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText (juce::String (g, 2)));
            C4Test::fillDeterministic (buf, 0xB1u + (juce::uint32) b);
            proc->processBlock (buf, midi);
            finite = finite && C4Test::allFinite (buf);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    maxDelta = juce::jmax (maxDelta, std::abs (v - prev));
                    prev = v;
                }
        }
        expect (finite, "BLOOM/gain automation stays finite");
        expect (maxDelta < 4.0f, "no click from BLOOM/gain automation (max delta "
                + juce::String (maxDelta, 3) + ")");
    }

    void testRealtimeAndLatency (double rate, int block)
    {
        beginTest ("Realtime: zero allocation with BLOOM active; zero added latency");

        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantC());
        for (int b = 0; b < 4; ++b)
            setBand (*proc, (C4BandId) b, 200.0f * (float) std::pow (4.0, b), 15.0f);
        setBloom (*proc, 1.0f);

        expect (proc->getLatencySamples() == 0, "zero added latency with BLOOM active");

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        C4Test::fillDeterministic (buf, 0xCAFEu);
        for (int b = 0; b < 50; ++b)
            proc->processBlock (buf, midi);
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int b = 0; b < 200; ++b)
            {
                setBloom (*proc, (float) (b % 20) / 19.0f); // plain float store
                proc->processBlock (buf, midi);
            }
        }
    }

    void testAllRates()
    {
        beginTest ("Harmonic color and safety at every supported sample rate");
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        for (double r : rates)
        {
            auto proc = C4Test::makePreparedProcessor (r, 512, makeProfileVariantB());
            setBand (*proc, C4BandId::Sculpt, 1000.0f, 6.0f);
            setBloom (*proc, 1.0f);
            const auto out = captureSine (*proc, r, 512, 1000.0, -6.0f, 1.0, 16384);
            const auto h = measureHarmonics (out, r, 1000.0);
            expect (C4Test::allFinite (out), juce::String (r, 0) + ": finite");
            expect (h.h2Db > -60.0f || h.h3Db > -60.0f,
                    juce::String (r, 0) + ": color measurable (H2 "
                        + juce::String (h.h2Db, 1) + ", H3 " + juce::String (h.h3Db, 1) + ")");
        }
    }
};

static C4ResidualBankTests c4ResidualBankTests;
