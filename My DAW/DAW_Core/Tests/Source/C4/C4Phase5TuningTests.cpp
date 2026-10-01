#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>
#include <vector>

// ============================================================================
// C4Phase5TuningTests — Phase 5: design-authority sonic tuning (measurement).
//
// This suite is the measurement backbone of the Phase 5 decision process. It
// does NOT assert final sonic preferences — those are DESIGN DECISIONS
// recorded in docs/C4_PHASE5_PERCEPTUAL_TUNING.md. It asserts the technical
// elimination contract: no candidate may violate the engineering bounds
// (alias floor, THD cap, DC floor, transient preservation, gain-inflation
// cap, sample-rate consistency, bit-identity of BLOOM 0).
//
// Every table printed via logMessage is MEASURED FACT, not a preference.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;
using APEX::C4::C4BandId;
using APEX::C4::C4TuningProfile;
using APEX::C4::makeProfileVariantA;
using APEX::C4::makeProfileVariantB;
using APEX::C4::makeProfileVariantC;
using APEX::C4::makeProfileProduction;

class C4Phase5TuningTests final : public juce::UnitTest
{
public:
    C4Phase5TuningTests() : juce::UnitTest ("C4.Phase5Tuning", "APEX.C4") {}

    void runTest() override
    {
        const double rate = 48000.0;
        const int block = 512;

        testGeometryBandwidthTable (rate, block);
        testCouplingFillTable (rate, block);
        testHaloProgressionTable();
        testColorLevelTable (rate, block);
        testBloomDefaultSweep (rate, block);
        testAliasTopOctave();
        testBoomContourAndLoudness (rate, block);
        testProductionProfileContracts (rate, block);
        testAllRatesProduction();
    }

private:
    // ------------------------------------------------------------------ //
    // Parameter helpers (identical contract to the Phase 4 suite).
    // ------------------------------------------------------------------ //

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

    static void setBloomNorm (C4Processor& proc, float norm01)
    {
        proc.getC4Parameter (C4ParamIndex::kBloom)->setValue (norm01);
    }

    // ------------------------------------------------------------------ //
    // Spectrum capture (linear path: BLOOM forced to 0).
    // ------------------------------------------------------------------ //

    struct Spectrum
    {
        std::vector<float> db;
        double binHz = 1.0;
    };

    static Spectrum measureSpectrum (C4Processor& proc, double rate, int block,
                                     int fftSize = 32768)
    {
        setBloomNorm (proc, 0.0f); // linear-only measurement (IR validity)
        C4Test::settleProcessor (proc, rate, block, 0.5);
        auto ir = C4Test::measureImpulseResponse (proc, 16384, block);

        C4Test::Radix2Fft fft (fftSize);
        std::vector<float> re (fftSize, 0.0f);
        std::vector<float> im (fftSize, 0.0f);
        const int n = (int) juce::jmin ((size_t) ir.size(), (size_t) fftSize);
        for (int i = 0; i < n; ++i)
            re[(size_t) i] = ir[(size_t) i];
        fft.transform (re.data(), im.data());

        Spectrum s;
        s.db.resize ((size_t) (fftSize / 2 + 1));
        for (int i = 0; i <= fftSize / 2; ++i)
        {
            const double mag = std::sqrt ((double) re[(size_t) i] * re[(size_t) i]
                                        + (double) im[(size_t) i] * im[(size_t) i]);
            s.db[(size_t) i] = (float) (20.0 * std::log10 (std::max (1e-12, mag)));
        }
        s.binHz = rate / fftSize;
        return s;
    }

    static float spectrumDbAt (const Spectrum& s, double freq)
    {
        const double bin = freq / s.binHz;
        const int b = (int) std::lround (bin);
        if (b < 0 || b >= (int) s.db.size())
            return -300.0f;
        return s.db[(size_t) b];
    }

    /** Find the -3.02 dB points of the dominant feature around `center`
        within [center / spanLow .. center * spanHigh] and return the width
        in octaves. For boosts the feature is a peak, for cuts a dip. */
    static float measureBandwidthOctaves (C4Processor& proc, double rate, int block,
                                          double center, bool boost,
                                          double spanLow = 4.0, double spanHigh = 4.0)
    {
        const auto s = measureSpectrum (proc, rate, block);

        const double lo = center / spanLow;
        const double hi = center * spanHigh;
        const int bLo = juce::jmax (1, (int) std::ceil (lo / s.binHz));
        const int bHi = juce::jmin ((int) s.db.size() - 2, (int) std::floor (hi / s.binHz));
        if (bHi <= bLo)
            return -1.0f;

        // Dominant feature dB.
        float extreme = s.db[(size_t) bLo];
        int bExtreme = bLo;
        for (int b = bLo; b <= bHi; ++b)
        {
            const float v = s.db[(size_t) b];
            if ((boost && v > extreme) || (! boost && v < extreme))
            {
                extreme = v;
                bExtreme = b;
            }
        }

        // Bandwidth definition for the PARALLEL console topology:
        //   y = x + (A-1)*B(f). For a boost the response NEVER returns to
        //   0 dB (|H| -> 1 asymptotically), so a "-3 dB from the peak" width
        //   does not exist for small boosts (measured: +3 dB has no crossing).
        //   The musically meaningful width is where the boost EXCESS falls to
        //   half: |H| = 1 + (A-1)/2  ->  in the in-phase region B(f) = 0.5.
        //   For cuts the dip is finite: width at depth + 3.02 dB.
        const float target = boost
            ? 20.0f * std::log10 (1.0f + (std::pow (10.0f, extreme / 20.0f) - 1.0f) * 0.5f)
            : extreme + 3.02f;

        // Crossings: the -3.02 dB points straddle the feature center.
        float fLow = 0.0f, fHigh = 0.0f;
        const auto interpolateCrossing = [&] (int from, int to) -> double
        {
            const int step = to > from ? 1 : -1;
            int prev = from;
            for (int b = from + step; b != to + step; b += step)
            {
                const float a = s.db[(size_t) prev];
                const float c = s.db[(size_t) b];
                const bool crossed = boost ? (a > target && c <= target)
                                           : (a < target && c >= target);
                if (crossed)
                {
                    const double t = std::abs (target - a) / std::max (1e-9, std::abs ((double) c - a));
                    return (prev + step * t) * s.binHz;
                }
                prev = b;
            }
            return -1.0;
        };

        // Walk left from the extreme, then right.
        fLow = interpolateCrossing (bExtreme, bLo);
        fHigh = interpolateCrossing (bExtreme, bHi);
        if (fLow <= 0.0 || fHigh <= 0.0 || fHigh <= fLow)
            return -1.0f;
        return (float) (std::log2 (fHigh / fLow));
    }

    // ------------------------------------------------------------------ //
    // Steady-sine capture + harmonic measurement (Phase 4 pattern).
    // ------------------------------------------------------------------ //

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
        std::vector<float> out ((size_t) captureSamples, 0.0f);
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
        for (int i = 0; i < captureSamples; ++i)
            out[(size_t) i] *= (float) (0.5 - 0.5 * std::cos (
                2.0 * juce::MathConstants<double>::pi * i / captureSamples));
        return out;
    }

    struct Harmonics
    {
        float h1Db = -300.0f;
        float h2Db = -300.0f, h3Db = -300.0f, h4Db = -300.0f;
        float thdDb = -300.0f;
        float dcDb = -300.0f;
    };

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
        const float h2l = std::pow (10.0f, h.h2Db / 10.0f);
        const float h3l = std::pow (10.0f, h.h3Db / 10.0f);
        const float h4l = std::pow (10.0f, h.h4Db / 10.0f);
        h.thdDb = (float) (10.0 * std::log10 (std::max (1.0e-30, (double) (h2l + h3l + h4l))));
        h.dcDb = binMagDb (0.0) - h.h1Db;
        return h;
    }

    // ------------------------------------------------------------------ //
    // Tests.
    // ------------------------------------------------------------------ //

    void testGeometryBandwidthTable (double rate, int block)
    {
        beginTest ("Geometry candidates: -3 dB bandwidth table (octaves, bell mode)");

        const struct { C4BandId band; float freq; } cases[] =
        {
            { C4BandId::Weight, 100.0f },
            { C4BandId::Sculpt, 400.0f },
            { C4BandId::Bite, 2500.0f },
            { C4BandId::Open, 10000.0f },
        };
        const struct { const char* name; C4TuningProfile prof; } profs[] =
        {
            { "A", makeProfileVariantA() },
            { "B", makeProfileVariantB() },
            { "C", makeProfileVariantC() },
            { "P", makeProfileProduction() },
        };
        const float gains[] = { 3.0f, 6.0f, 12.0f, -6.0f, -12.0f };

        for (const auto& c : cases)
        {
            juce::String line = juce::String ((int) c.band) + " @" + juce::String (c.freq, 0)
                                + " Hz: ";
            for (const auto& pr : profs)
            {
                auto proc = C4Test::makePreparedProcessor (rate, block, pr.prof);
                juce::String cell;
                for (float g : gains)
                {
                    setBand (*proc, c.band, c.freq, g);
                    const float oct = measureBandwidthOctaves (*proc, rate, block, c.freq, g > 0.0f);
                    cell += (g > 0.0f ? juce::String (juce::roundToInt (g)) + "+:"
                                      : juce::String (juce::roundToInt (-g)) + "-:")
                          + (oct > 0.0f ? juce::String (oct, 2) : juce::String ("?")) + " ";
                }
                line += juce::String (pr.name) + "[" + cell.trim() + "]  ";
            }
            logMessage (line);

            // Elimination contract on the production profile only:
            // positive finite bandwidths; small boosts no narrower than large
            // boosts (proportional-Q monotonicity); cuts finite.
            auto p = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
            setBand (*p, c.band, c.freq, 3.0f);
            const float oct3 = measureBandwidthOctaves (*p, rate, block, c.freq, true);
            setBand (*p, c.band, c.freq, 12.0f);
            const float oct12 = measureBandwidthOctaves (*p, rate, block, c.freq, true);
            setBand (*p, c.band, c.freq, -12.0f);
            const float octCut = measureBandwidthOctaves (*p, rate, block, c.freq, false);
            expect (oct3 > 0.0f && oct12 > 0.0f && octCut > 0.0f,
                    juce::String ((int) c.band) + ": production bandwidths must be finite");
            // Near-constant-Q bands (BITE by design) legitimately measure
            // nearly equal widths across gains; the assert rejects only a
            // strong inversion (small boost clearly narrower than large).
            expect (oct3 >= oct12 - 0.15f,
                    juce::String ((int) c.band) + ": small boost must not be narrower than "
                    "large boost (oct3=" + juce::String (oct3, 2) + " oct12="
                        + juce::String (oct12, 2) + ")");
        }
    }

    void testCouplingFillTable (double rate, int block)
    {
        beginTest ("BLOOM coupling candidates: valley fill table (dB at log midpoint)");

        const struct { C4BandId a; float fa; C4BandId b; float fb; float fmid; } pairs[] =
        {
            { C4BandId::Weight, 100.0f, C4BandId::Sculpt, 400.0f, 200.0f },
            { C4BandId::Sculpt, 400.0f, C4BandId::Bite, 2500.0f, 1000.0f },
            { C4BandId::Bite, 2500.0f, C4BandId::Open, 10000.0f, 5000.0f },
        };
        const struct { float gA; float gB; } combos[] =
        {
            { 6.0f, 6.0f }, { 9.0f, 3.0f }, { 15.0f, 15.0f },
        };

        for (const auto& pr : pairs)
        {
            for (const auto& prof : { makeProfileVariantB(), makeProfileVariantC(),
                                      makeProfileProduction() })
            {
                // Uncoupled reference: the SAME profile with coupling OFF, so
                // the fill isolates the coupling layer exactly.
                auto uncoupledProfile = prof;
                uncoupledProfile.coupling.enabled = false;

                auto coupled = C4Test::makePreparedProcessor (rate, block, prof);
                auto uncoupled = C4Test::makePreparedProcessor (rate, block, uncoupledProfile);
                juce::String cells;
                bool allCapped = true;
                bool noneNegative = true;
                for (const auto& combo : combos)
                {
                    setBand (*coupled, pr.a, pr.fa, combo.gA);
                    setBand (*coupled, pr.b, pr.fb, combo.gB);
                    setBand (*uncoupled, pr.a, pr.fa, combo.gA);
                    setBand (*uncoupled, pr.b, pr.fb, combo.gB);
                    const float fill = measureGainDbFast (*coupled, rate, block, pr.fmid)
                                     - measureGainDbFast (*uncoupled, rate, block, pr.fmid);
                    cells += juce::String (combo.gA, 0) + "/" + juce::String (combo.gB, 0)
                           + ":" + juce::String (fill, 2) + "dB ";
                    allCapped = allCapped && fill <= 0.7f;
                    noneNegative = noneNegative && fill >= -0.05f;
                }
                logMessage (juce::String ((int) pr.a) + "<->" + juce::String ((int) pr.b)
                            + " " + prof.name + ": " + cells.trim());
                if (juce::String (prof.name) == "PROD")
                {
                    expect (allCapped,
                            juce::String ((int) pr.a) + "<->" + juce::String ((int) pr.b)
                                + ": production coupling fill must stay <= 0.7 dB (no gain inflation)");
                    expect (noneNegative,
                            juce::String ((int) pr.a) + "<->" + juce::String ((int) pr.b)
                                + ": production coupling must never subtract");
                }
            }
        }
    }

    static float measureGainDbFast (C4Processor& proc, double rate, int block, double freq)
    {
        setBloomNorm (proc, 0.0f);
        C4Test::settleProcessor (proc, rate, block, 0.4);
        auto ir = C4Test::measureImpulseResponse (proc, 8192, block);
        return C4Test::magnitudeDbAtFrequency (ir, rate, freq, 16384);
    }

    void testHaloProgressionTable()
    {
        beginTest ("HALO mapping: realized shelf frequency progression");

        const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
        const float controls[] = { 2500.0f, 5000.0f, 10000.0f, 20000.0f, 25000.0f, 30000.0f, 40000.0f };
        const float softnesses[] = { 0.3f, 0.5f, 0.7f };

        for (double rate : rates)
        {
            for (float soft : softnesses)
            {
                APEX::C4::C4HaloTuning halo { soft, 1.0f, 0.90f };
                juce::String line = juce::String (rate, 0) + " Hz soft=" + juce::String (soft, 1) + ": ";
                float prev = 0.0f;
                bool monotonic = true;
                for (float c : controls)
                {
                    const float f = APEX::C4::C4HaloShelfMapper::mapControlFreq (c, (float) rate, halo);
                    if (f < prev - 1.0f)
                        monotonic = false;
                    prev = f;
                    line += juce::String (c / 1000.0f, c >= 10000.0f ? 0 : 1)
                          + "k->" + juce::String (f, 0) + " ";
                }
                logMessage (line);
                expect (monotonic, "HALO mapping must be monotonic non-decreasing");
            }
        }
    }

    void testColorLevelTable (double rate, int block)
    {
        beginTest ("BLOOM color candidates: harmonic table (BLOOM 10, 48 kHz)");

        const struct { C4BandId band; float freq; } cases[] =
        {
            { C4BandId::Weight, 100.0f },
            { C4BandId::Sculpt, 1000.0f },
            { C4BandId::Bite, 4000.0f },
            { C4BandId::Open, 5000.0f },
        };
        const float gains[] = { 3.0f, 6.0f, 15.0f };

        for (const auto& pr : { makeProfileVariantB(), makeProfileVariantC(),
                                makeProfileProduction() })
        {
            for (const auto& c : cases)
            {
                for (float g : gains)
                {
                    auto proc = C4Test::makePreparedProcessor (rate, block, pr);
                    setBand (*proc, c.band, c.freq, g);
                    setBloomNorm (*proc, 1.0f);
                    const auto out = captureSine (*proc, rate, block, c.freq, -6.0f, 0.5, 16384);
                    const auto h = measureHarmonics (out, rate, c.freq);
                    logMessage (juce::String ("COLOR ") + pr.name + " band" + juce::String ((int) c.band)
                                + " +" + juce::String (g, 0) + "dB @-6dBFS: H2="
                                + juce::String (h.h2Db, 1) + " H3=" + juce::String (h.h3Db, 1)
                                + " THD=" + juce::String (h.thdDb, 1) + " DC=" + juce::String (h.dcDb, 1));
                    // Elimination: no "saturator inserted" behavior at any
                    // setting; DC deliberately managed.
                    expect (h.thdDb < -30.0f,
                            juce::String (pr.name) + " band" + juce::String ((int) c.band)
                                + " THD must stay below -30 dB (saturator elimination)");
                    expect (h.dcDb < -250.0f,
                            juce::String (pr.name) + " band" + juce::String ((int) c.band)
                                + " DC must stay below -250 dB");
                }
            }
        }
    }

    void testBloomDefaultSweep (double rate, int block)
    {
        beginTest ("BLOOM default sweep: harmonics vs control value (production color)");

        const struct { C4BandId band; float freq; } cases[] =
        {
            { C4BandId::Weight, 100.0f },
            { C4BandId::Sculpt, 1000.0f },
            { C4BandId::Bite, 4000.0f },
            { C4BandId::Open, 5000.0f },
        };
        const float blooms[] = { 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };

        for (const auto& c : cases)
        {
            juce::String line = juce::String ("BLOOM SWEEP band") + juce::String ((int) c.band)
                                + " +6dB @-6dBFS: ";
            float prevThd = -300.0f;
            bool monotone = true;
            for (float b : blooms)
            {
                auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
                setBand (*proc, c.band, c.freq, 6.0f);
                setBloomNorm (*proc, b);
                const auto out = captureSine (*proc, rate, block, c.freq, -6.0f, 0.5, 16384);
                const auto h = measureHarmonics (out, rate, c.freq);
                if (h.thdDb < prevThd - 0.5f) // THD must not DECREASE with BLOOM
                    monotone = false;
                prevThd = h.thdDb;
                line += juce::String (b * 10.0f, 0) + ":" + juce::String (h.thdDb, 1) + " ";
            }
            logMessage (line);
            expect (monotone, "color must grow monotonically with BLOOM");
        }
    }

    /** Worst folded-harmonic alias level (dB rel. fundamental) in a captured
        spectrum. Harmonics k*f0 that fold into [0, Nyquist] and do not
        coincide with an in-band harmonic of f0 are alias products. */
    static float worstAliasDb (const std::vector<float>& out, double rate, double freq)
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
            if (b <= 2 || b >= n - 2)
                return -300.0f;
            double sum = 0.0;
            for (int k = b - 2; k <= b + 2; ++k)
                if (k > 2 && k < n - 2)
                    sum += (double) re[(size_t) k] * re[(size_t) k]
                         + (double) im[(size_t) k] * im[(size_t) k];
            return (float) (10.0 * std::log10 (std::max (1.0e-30, sum)));
        };

        const float fundamentalDb = binMagDb (freq);
        float worst = -300.0f;
        for (int k = 2; k <= 8; ++k)
        {
            double folded = std::fmod (k * freq, rate);
            if (folded > rate * 0.5)
                folded = rate - folded;

            // Skip if folded coincides with the fundamental OR an in-band
            // harmonic (not alias): e.g. 3*f0 of 12 kHz folds exactly onto
            // 12 kHz (measured artifact that reported 0.0 dB "alias").
            bool coincides = std::abs (folded - freq) < binHz * 3.0;
            for (int j = 2; j <= 8 && ! coincides; ++j)
            {
                const double inband = j * freq;
                if (inband < rate * 0.4999 && std::abs (inband - folded) < binHz * 3.0)
                    coincides = true;
            }
            if (coincides || folded < binHz * 4.0)
                continue;

            // Skip folds landing in the top bin at/next to Nyquist: single-
            // bin edge content is inaudible and removed by any reconstruction
            // filter — it is not the "glass" this metric protects against
            // (glass lives in the audible band). Note: at f0 = fs/4 every
            // fold lands on {f0, DC, Nyquist}, so that cell has NO audible
            // alias targets by construction.
            if (folded > rate * 0.5 - binHz * 2.0)
                continue;

            const float level = binMagDb (folded) - fundamentalDb;
            if (level > worst)
                worst = level;
        }
        return worst;
    }

    void testAliasTopOctave()
    {
        beginTest ("Antialias strategy: top-octave fold products (OPEN band, BLOOM 10, +15 dB)");

        const double rates[] = { 44100.0, 48000.0 };
        const float freqs[] = { 10000.0f, 12000.0f, 15000.0f, 18000.0f };

        for (double rate : rates)
        {
            const int block = 512;
            for (float f0 : freqs)
            {
                float aliasOs[3] = { -300.0f, -300.0f, -300.0f };
                int osIndex = 0;
                juce::String line = juce::String (rate, 0) + " Hz f0=" + juce::String (f0, 0) + ": ";
                for (int os : { 1, 2, 4 })
                {
                    auto prof = makeProfileProduction();
                    prof.residualOversample = os;
                    auto proc = C4Test::makePreparedProcessor (rate, block, prof);
                    setBand (*proc, C4BandId::Open, f0, 15.0f);
                    setBloomNorm (*proc, 1.0f);
                    const auto out = captureSine (*proc, rate, block, f0, -6.0f, 0.5, 16384);
                    aliasOs[osIndex++] = worstAliasDb (out, rate, f0);
                    line += juce::String (os) + "x:" + juce::String (aliasOs[osIndex - 1], 1) + " ";
                }
                logMessage (line);
                // The elimination floor applies to the PRODUCTION strategy
                // (2x): the worst audible-band fold product at the absolute
                // extreme settings must stay <= -55 dB. The 1x/4x columns are
                // benchmark evidence for the strategy decision.
                expect (aliasOs[1] <= -55.0f,
                        juce::String (rate, 0) + " Hz f0=" + juce::String (f0, 0)
                            + ": production 2x fold product must be <= -55 dB (glass elimination)");
                // Monotonicity applies only when the cell HAS an audible
                // alias target (e.g. f0 = fs/4 folds everything onto
                // {f0, DC, Nyquist}: no measurable product at any OS).
                if (aliasOs[0] > -290.0f)
                    expect (aliasOs[1] <= aliasOs[0] - 0.2f && aliasOs[2] <= aliasOs[1] - 0.2f,
                            "oversampling must improve alias monotonically");
            }
        }
    }

    static float rmsDb (const float* data, int n) noexcept
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += (double) data[i] * data[i];
        return (float) (10.0 * std::log10 (std::max (1e-30, sum / n)));
    }

    static float peakDb (const float* data, int n) noexcept
    {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = juce::jmax (peak, std::abs (data[i]));
        return (float) (20.0 * std::log10 (std::max (1e-12f, peak)));
    }

    /** Deterministic "program-like" signal: white LCG noise through two
        one-pole tilt stages (rough -3 dB/oct spectral rolloff). */
    static std::vector<float> makeProgramSignal (int samples, double rate, juce::uint32 seed)
    {
        std::vector<float> x ((size_t) samples);
        juce::uint32 state = seed;
        float lp1 = 0.0f, lp2 = 0.0f;
        const float a1 = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 3000.0 / rate);
        const float a2 = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 900.0 / rate);
        for (int i = 0; i < samples; ++i)
        {
            state = state * 1664525u + 1013904223u;
            const float w = (float) (state / 4294967296.0) * 2.0f - 1.0f;
            lp1 += a1 * (w - lp1);
            lp2 += a2 * (lp1 - lp2);
            x[(size_t) i] = lp2 * 0.7f;
        }
        return x;
    }

    static float processSignalRmsPeak (C4Processor& proc, const std::vector<float>& sig,
                                       int block, float& outPeakDb)
    {
        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        std::vector<float> out (sig.size());
        int pos = 0;
        while (pos < (int) sig.size())
        {
            const int n = juce::jmin (block, (int) sig.size() - pos);
            for (int i = 0; i < n; ++i)
                buf.setSample (0, i, sig[(size_t) (pos + i)]);
            proc.processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
                out[(size_t) (pos + i)] = buf.getSample (0, i);
            pos += n;
        }
        const size_t trim = (size_t) (0.2 * sig.size());
        outPeakDb = peakDb (out.data() + trim, (int) (out.size() - trim));
        return rmsDb (out.data() + trim, (int) (out.size() - trim));
    }

    void testBoomContourAndLoudness (double rate, int block)
    {
        beginTest ("C4 BOOM TEST settings: contour, loudness delta, crest preservation");

        const double rateLocal = rate;
        auto proc = C4Test::makePreparedProcessor (rateLocal, block, makeProfileProduction());
        setBand (*proc, C4BandId::Weight, 100.0f, 3.0f);
        setBand (*proc, C4BandId::Sculpt, 400.0f, 2.0f);
        setBand (*proc, C4BandId::Bite, 2500.0f, 2.0f);
        setBand (*proc, C4BandId::Open, 10000.0f, 3.0f);
        setBloomNorm (*proc, 0.4f); // production default (BLOOM 4.0)

        // (a) Linear contour at the four centers: each requested peak must
        // survive the parallel summation (no cancellation, no inflation).
        const struct { double f; float req; } centers[] =
        {
            { 100.0, 3.0 }, { 400.0, 2.0 }, { 2500.0, 2.0 }, { 10000.0, 3.0 },
        };
        juce::String contourLine = "BOOM centers: ";
        for (const auto& c : centers)
        {
            const float g = measureGainDbFast (*proc, rateLocal, block, c.f);
            contourLine += juce::String (c.f, 0) + "Hz:" + juce::String (g, 2) + "dB ";
            expect (g >= c.req - 0.75f && g <= c.req + 1.25f,
                    "BOOM center " + juce::String (c.f, 0)
                        + " Hz must land within +1.25/-0.75 dB of its request");
        }
        logMessage (contourLine);

        // (b) Level-matched honesty: RMS/peak delta vs bypass, program signal.
        const int sigLen = (int) (rateLocal * 2.0);
        auto sig = makeProgramSignal (sigLen, rateLocal, 0xC4B00u);

        auto dryProc = C4Test::makePreparedProcessor (rateLocal, block, makeProfileProduction());
        dryProc->getC4Parameter (C4ParamIndex::kBypass)->setValue (1.0f);
        C4Test::settleProcessor (*dryProc, rateLocal, block, 0.3);

        float dryPeak, wetPeak;
        const float dryRms = processSignalRmsPeak (*dryProc, sig, block, dryPeak);
        const float wetRms = processSignalRmsPeak (*proc, sig, block, wetPeak);
        logMessage (juce::String ("BOOM loudness delta: RMS ") + juce::String (wetRms - dryRms, 2)
                    + " dB, peak " + juce::String (wetPeak - dryPeak, 2) + " dB "
                    "(render-pack level-match compensation = -RMS delta; NO limiter, "
                    "NO makeup gain)");
        expect (wetRms - dryRms < 6.0f,
                "BOOM settings must not inflate RMS by more than 6 dB (level-match honest)");
        expect (wetPeak - dryPeak < 8.0f,
                "BOOM settings must not inflate peak by more than 8 dB");

        // (c) Transient crest preservation at the default BLOOM (kick burst).
        testBoomCrest (rateLocal, block);
    }

    void testBoomCrest (double rate, int block)
    {
        // Kick-like burst: 60 Hz sine with a fast exponential decay.
        const int total = (int) (rate * 0.6);
        std::vector<float> burst ((size_t) total, 0.0f);
        for (int i = 0; i < total; ++i)
        {
            const double t = i / rate;
            burst[(size_t) i] = (float) (0.8 * std::exp (-t * 18.0) * std::sin (
                2.0 * juce::MathConstants<double>::pi * 60.0 * t));
        }

        auto crestOf = [&] (C4Processor& p) -> float
        {
            juce::AudioBuffer<float> buf (1, block);
            juce::MidiBuffer midi;
            int pos = 0;
            std::vector<float> out ((size_t) total);
            while (pos < total)
            {
                const int n = juce::jmin (block, total - pos);
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, burst[(size_t) (pos + i)]);
                p.processBlock (buf, midi);
                for (int i = 0; i < n; ++i)
                    out[(size_t) (pos + i)] = buf.getSample (0, i);
                pos += n;
            }
            // Crest over the first 250 ms (transient region).
            const int win = (int) (rate * 0.25);
            return peakDb (out.data(), win) - rmsDb (out.data(), win);
        };

        // BOOM settings on a fresh processor (same contour as the parent test).
        auto setBoom = [] (C4Processor& p)
        {
            setBand (p, C4BandId::Weight, 100.0f, 3.0f);
            setBand (p, C4BandId::Sculpt, 400.0f, 2.0f);
            setBand (p, C4BandId::Bite, 2500.0f, 2.0f);
            setBand (p, C4BandId::Open, 10000.0f, 3.0f);
        };

        auto off = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        setBoom (*off);
        setBloomNorm (*off, 0.0f);
        auto def = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        setBoom (*def);
        setBloomNorm (*def, 0.4f);
        auto max = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        setBoom (*max);
        setBloomNorm (*max, 1.0f);

        C4Test::settleProcessor (*off, rate, block, 0.3);
        C4Test::settleProcessor (*def, rate, block, 0.3);
        C4Test::settleProcessor (*max, rate, block, 0.3);

        const float cOff = crestOf (*off);
        const float cDef = crestOf (*def);
        const float cMax = crestOf (*max);
        logMessage (juce::String ("BOOM crest (kick burst): BLOOM0 ") + juce::String (cOff, 2)
                    + " dB, BLOOM4 " + juce::String (cDef, 2) + " dB, BLOOM10 "
                    + juce::String (cMax, 2) + " dB");
        expect (std::abs (cDef - cOff) <= 0.5f,
                "crest factor at the production default must stay within 0.5 dB "
                "(C4 is not a compressor)");
        expect (std::abs (cMax - cOff) <= 0.5f,
                "crest factor at BLOOM 10 must stay within 0.5 dB");
    }

    void testProductionProfileContracts (double rate, int block)
    {
        beginTest ("Production profile: sacred contracts");

        // (1) BLOOM 0 bit-identity vs the same profile with the color layer
        // zeroed (the sacred baseline, re-proven for the frozen profile).
        {
            auto colorlessProfile = makeProfileProduction();
            for (int b = 0; b < 4; ++b)
            {
                auto c = colorlessProfile.color[b];
                c.residualLevel = 0.0f;
                colorlessProfile.color[b] = c;
            }
            auto colored = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
            auto colorless = C4Test::makePreparedProcessor (rate, block, colorlessProfile);
            // BLOOM 0 on BOTH (the production default is 4.0 — the sacred
            // baseline is proven at BLOOM 0, exactly as in Phase 4).
            setBloomNorm (*colored, 0.0f);
            setBloomNorm (*colorless, 0.0f);
            // Shaped, coupling-inert settings (no adjacent boost pair).
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

            juce::AudioBuffer<float> a (2, block);
            juce::AudioBuffer<float> b (2, block);
            juce::MidiBuffer midi;
            bool identical = true;
            for (int blk = 0; blk < 80; ++blk)
            {
                C4Test::fillDeterministic (a, 0xC4B00u + (juce::uint32) blk);
                b = a;
                colored->processBlock (a, midi);
                colorless->processBlock (b, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (a.getSample (ch, i) != b.getSample (ch, i))
                            identical = false;
            }
            expect (identical, "PROD: BLOOM 0 must be bit-identical to the colorless reference");
        }

        // (2) Default BLOOM = 4.0 in user units on a fresh processor.
        {
            C4Processor proc; // default-constructed = production profile
            auto* bloom = proc.getC4Parameter (C4ParamIndex::kBloom);
            expect (std::abs (bloom->getUnitsValue() - 4.0f) < 1e-4f,
                    "production BLOOM default must be 4.0 (got "
                        + juce::String (bloom->getUnitsValue(), 2) + ")");
            expect (proc.getLatencySamples() == 0, "production must remain zero-latency");
        }

        // (3) Default-BLOOM harmonics at the default settings must sit in the
        // "subtle but recognizable" band at +6 dB/-6 dBFS (hot sine): H2 in
        // [-60, -35] dB — audibly present, never saturator-like.
        {
            const struct { C4BandId band; float freq; } cases[] =
            {
                { C4BandId::Weight, 100.0f },
                { C4BandId::Sculpt, 1000.0f },
                { C4BandId::Bite, 4000.0f },
                { C4BandId::Open, 5000.0f },
            };
            for (const auto& c : cases)
            {
                auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
                setBand (*proc, c.band, c.freq, 6.0f);
                setBloomNorm (*proc, 0.4f);
                const auto out = captureSine (*proc, rate, block, c.freq, -6.0f, 0.5, 16384);
                const auto h = measureHarmonics (out, rate, c.freq);
                logMessage (juce::String ("PROD default BLOOM4 band") + juce::String ((int) c.band)
                            + " +6dB @-6dBFS: H2=" + juce::String (h.h2Db, 1)
                            + " H3=" + juce::String (h.h3Db, 1) + " THD=" + juce::String (h.thdDb, 1));
                // Per-band identity windows: WEIGHT/SCULPT/BITE sit in the
                // "subtle but recognizable" band; OPEN is BY DESIGN the least
                // colored band (minimal sheen) — its window is calibrated to
                // the measured floor, not the other bands' windows.
                const float h2Lo = (c.band == C4BandId::Open) ? -68.0f : -60.0f;
                const float h2Hi = (c.band == C4BandId::Open) ? -50.0f : -35.0f;
                expect (h.h2Db > h2Lo && h.h2Db < h2Hi,
                        juce::String ("band") + juce::String ((int) c.band)
                            + ": default-BLOOM H2 must sit in its identity window");
            }
        }

        // (4) Fresh engine is neutral (Phase 5 gate semantics: BLOOM is not a
        // neutrality condition when every band gain is 0).
        {
            auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
            expect (proc->getEngine().isSettledNeutral(), "fresh engine must be neutral");
            juce::AudioBuffer<float> buf (2, block);
            C4Test::fillDeterministic (buf, 0xA11CEu);
            juce::MidiBuffer midi;
            for (int blk = 0; blk < 200; ++blk) // let BLOOM 4.0 settle
                proc->processBlock (buf, midi);
            expect (proc->getEngine().isSettledNeutral(),
                    "flat production instance must re-open the neutral gate after settle");
        }
    }

    void testAllRatesProduction()
    {
        beginTest ("Production profile: sample-rate consistency (all six rates)");

        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        const struct { C4BandId band; float freq; } cases[] =
        {
            { C4BandId::Weight, 100.0f },
            { C4BandId::Sculpt, 400.0f },
            { C4BandId::Bite, 2500.0f },
            { C4BandId::Open, 10000.0f },
        };
        const int block = 512;

        float refGain[4] = {};
        for (const auto& c : cases)
        {
            auto p48 = C4Test::makePreparedProcessor (48000.0, block, makeProfileProduction());
            setBand (*p48, c.band, c.freq, 6.0f);
            refGain[(int) c.band] = measureGainDbFast (*p48, 48000.0, block, c.freq);

            for (double rate : rates)
            {
                if (rate == 48000.0)
                    continue;
                auto p = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
                setBand (*p, c.band, c.freq, 6.0f);
                const float g = measureGainDbFast (*p, rate, block, c.freq);
                logMessage (juce::String ("SR band") + juce::String ((int) c.band)
                            + " @ " + juce::String (rate, 0) + " Hz: " + juce::String (g, 2) + " dB");
                expect (std::abs (g - refGain[(int) c.band]) < 0.5f,
                        juce::String ("band") + juce::String ((int) c.band) + " @ "
                            + juce::String (rate, 0) + " Hz must stay within 0.5 dB of 48 kHz");
            }
        }
    }
};

static C4Phase5TuningTests c4Phase5TuningTests;
