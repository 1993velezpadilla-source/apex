#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4CompositionTests — PARALLEL-BAND TOPOLOGY AUDIT (audit A).
//
// The engine composes bands in parallel: y = x0 + SUM (A_b - 1) * shape_b.
// This suite proves the composition is PREDICTABLE LINEAR SUPERPOSITION —
// the correct base for BLOOM Coupling to enhance later — by comparing the
// measured total response against the exact COMPLEX prediction built from
// individually measured COMPLEX band responses:
//
//   G(f) = 1 + SUM_b ( H_b(f) - 1 ),   |G| in dB
//
// The complex model is the authority: magnitude-only scalar addition is
// wrong whenever neighboring skirts carry phase (measured 0.3..0.8 dB errors
// with the naive magnitude model; the complex model is near-exact). At a
// frequency shared by two +6 dB centers the parallel law gives
// |1 + 2*(A-1)| = 2A - 1 = +9.54 dB — NOT +6 — and the complex model
// reproduces that exactly.
//
// Cases: one band, two non-overlapping bands, two heavily overlapping bands,
// WEIGHT+SCULPT, SCULPT+BITE, BITE+OPEN, all four boosted, all four cut,
// mixed boost/cut. For every case:
//   - the TOTAL at each band's center matches the complex superposition
//     prediction (the requested center gain itself is proven by the solo
//     C4.Engine center-gain suite)
//   - total response matches the complex-superposition prediction (<= 0.15 dB)
//   - no unexpected cancellation (prediction error bounds it)
//   - no pathological peak formation (bounded peak envelope)
//   - zero-gain neutrality is preserved even with frequencies moved
//
// If the prediction did NOT match, the architecture itself would be wrong —
// BLOOM Coupling must never be used to repair a flawed base topology.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4Parameter;
using APEX::C4::C4ParamIndex;

class C4CompositionTests final : public juce::UnitTest
{
public:
    struct BandSetting
    {
        C4ParamIndex freqIdx;
        C4ParamIndex gainIdx;
        float freq;
        float gainDb;
    };

    C4CompositionTests() : juce::UnitTest ("C4.Composition", "APEX.C4") {}

    void runTest() override
    {
        const double rate = 48000.0;
        const int block = 512;

        const BandSetting weight  { C4ParamIndex::kWeightFreq, C4ParamIndex::kWeightGain, 100.0f,  0.0f };
        const BandSetting sculpt  { C4ParamIndex::kSculptFreq, C4ParamIndex::kSculptGain, 800.0f,  0.0f };
        const BandSetting bite    { C4ParamIndex::kBiteFreq,   C4ParamIndex::kBiteGain,   3000.0f, 0.0f };
        const BandSetting open    { C4ParamIndex::kOpenFreq,   C4ParamIndex::kOpenGain,   12000.0f,0.0f };

        struct Case
        {
            juce::String name;
            BandSetting bands[4];
            int numBands;
        };

        const Case cases[] =
        {
            { "one band (WEIGHT +6)", { { weight.freqIdx, weight.gainIdx, 100, 6 }, sculpt, bite, open }, 1 },
            { "two non-overlapping (WEIGHT 100 +6, SCULPT 2000 +6)",
              { { weight.freqIdx, weight.gainIdx, 100, 6 }, { sculpt.freqIdx, sculpt.gainIdx, 2000, 6 }, bite, open }, 2 },
            { "two heavily overlapping (WEIGHT 300 +6, SCULPT 300 +6)",
              { { weight.freqIdx, weight.gainIdx, 300, 6 }, { sculpt.freqIdx, sculpt.gainIdx, 300, 6 }, bite, open }, 2 },
            { "WEIGHT+SCULPT overlap (200 +6, 600 +6)",
              { { weight.freqIdx, weight.gainIdx, 200, 6 }, { sculpt.freqIdx, sculpt.gainIdx, 600, 6 }, bite, open }, 2 },
            { "SCULPT+BITE overlap (800 +6, 3000 +6)",
              { weight, { sculpt.freqIdx, sculpt.gainIdx, 800, 6 }, { bite.freqIdx, bite.gainIdx, 3000, 6 }, open }, 2 },
            { "BITE+OPEN overlap (3000 +6, 12000 +6)",
              { weight, sculpt, { bite.freqIdx, bite.gainIdx, 3000, 6 }, { open.freqIdx, open.gainIdx, 12000, 6 } }, 2 },
            { "all four boosted (+6 spread)",
              { { weight.freqIdx, weight.gainIdx, 100, 6 }, { sculpt.freqIdx, sculpt.gainIdx, 800, 6 },
                { bite.freqIdx, bite.gainIdx, 3000, 6 }, { open.freqIdx, open.gainIdx, 12000, 6 } }, 4 },
            { "all four cut (-6 spread)",
              { { weight.freqIdx, weight.gainIdx, 100, -6 }, { sculpt.freqIdx, sculpt.gainIdx, 800, -6 },
                { bite.freqIdx, bite.gainIdx, 3000, -6 }, { open.freqIdx, open.gainIdx, 12000, -6 } }, 4 },
            { "mixed (WEIGHT +9, SCULPT -6, BITE +6, OPEN -3)",
              { { weight.freqIdx, weight.gainIdx, 100, 9 }, { sculpt.freqIdx, sculpt.gainIdx, 800, -6 },
                { bite.freqIdx, bite.gainIdx, 3000, 6 }, { open.freqIdx, open.gainIdx, 12000, -3 } }, 4 }
        };

        const double grid[] = { 50, 100, 200, 400, 800, 1600, 3000, 6000, 12000, 20000 };

        for (const auto& c : cases)
        {
            beginTest ("Composition: " + c.name);

            auto total = C4Test::makePreparedProcessor (rate, block);
            applySettings (*total, c.bands, c.numBands);

            // Individual COMPLEX band responses (single band active, rest at
            // 0 dB), measured from ONE settled IR per solo.
            std::complex<float> singleC[4][10]; // [band][grid]
            std::vector<float> soloIr[4];
            for (int b = 0; b < c.numBands; ++b)
            {
                auto solo = C4Test::makePreparedProcessor (rate, block);
                setOneBand (*solo, c.bands[b]);
                C4Test::settleProcessor (*solo, rate, block, 1.0);
                soloIr[b] = C4Test::measureImpulseResponse (*solo, 8192, block);
                for (int k = 0; k < 10; ++k)
                    singleC[b][k] = C4Test::complexResponseAtFrequency (soloIr[b], rate, grid[k], 16384);
            }

            // Total COMPLEX response.
            C4Test::settleProcessor (*total, rate, block, 1.0);
            const auto totalIr = C4Test::measureImpulseResponse (*total, 8192, block);
            std::complex<float> totalC[10];
            for (int k = 0; k < 10; ++k)
                totalC[k] = C4Test::complexResponseAtFrequency (totalIr, rate, grid[k], 16384);

            // 1. The TOTAL at each band's center must equal the complex
            //    superposition prediction there (solo center gains are proven
            //    by the C4.Engine center-gain suite; at a shared center two
            //    +6 dB bands sum to 2A-1 = +9.54 dB, not +6).
            for (int b = 0; b < c.numBands; ++b)
            {
                const float freq = c.bands[b].freq;
                std::complex<float> pred = 1.0f;
                for (int b2 = 0; b2 < c.numBands; ++b2)
                    pred += C4Test::complexResponseAtFrequency (soloIr[b2], rate, freq, 16384) - 1.0f;
                const float predictedDb = 20.0f * (float) std::log10 (std::max (1.0e-6f, std::abs (pred)));
                const float gAtCenter = C4Test::measureGainDb (*total, rate, block, freq);
                expect (std::abs (gAtCenter - predictedDb) <= 0.5f,
                        "band " + juce::String (b) + " center: superposition prediction "
                            + juce::String (predictedDb, 2) + " dB, measured "
                            + juce::String (gAtCenter, 2) + " dB at "
                            + juce::String (freq, 0) + " Hz");
            }

            // 2. Total response == COMPLEX linear superposition prediction.
            double maxPredErr = 0.0;
            juce::String worstFreq;
            for (int k = 0; k < 10; ++k)
            {
                std::complex<float> lin = 1.0f;
                for (int b = 0; b < c.numBands; ++b)
                    lin += singleC[b][k] - 1.0f;
                const double predictedDb = 20.0 * std::log10 (std::max (1.0e-6f, std::abs (lin)));
                const double err = std::abs ((double) 20.0 * std::log10 (std::max (1.0e-6f, std::abs (totalC[k]))) - predictedDb);
                if (err > maxPredErr)
                {
                    maxPredErr = err;
                    worstFreq = juce::String (grid[k], 0) + " Hz";
                }
            }
            expect (maxPredErr <= 0.15,
                    "total must equal complex linear superposition (max error "
                        + juce::String (maxPredErr, 3) + " dB at " + worstFreq + ")");

            // 3. No pathological peak formation: the envelope of an
            //    all-boost configuration is bounded by sum-of-boosts + 1 dB.
            if (c.name.startsWith ("all four boosted") || c.name.startsWith ("mixed"))
            {
                float maxDb = -300.0f;
                for (int k = 0; k < 10; ++k)
                    maxDb = juce::jmax (maxDb, (float) (20.0 * std::log10 (std::max (1.0e-6f, std::abs (totalC[k])))));
                float sumBoostDb = 0.0f;
                for (int b = 0; b < c.numBands; ++b)
                    sumBoostDb += juce::jmax (0.0f, c.bands[b].gainDb);
                expect (maxDb <= sumBoostDb + 1.0f,
                        "no pathological peak: max " + juce::String (maxDb, 2)
                            + " dB vs boost envelope " + juce::String (sumBoostDb, 1) + " dB");
            }

            // 4. No unexpected cancellation: the total must not fall below
            //    the deepest single-band cut minus 1 dB (linear parallel
            //    composition cannot cancel deeper than its own cuts).
            {
                float minDb = 300.0f;
                for (int k = 0; k < 10; ++k)
                    minDb = juce::jmin (minDb, (float) (20.0 * std::log10 (std::max (1.0e-6f, std::abs (totalC[k])))));
                float deepestCut = 0.0f;
                for (int b = 0; b < c.numBands; ++b)
                    deepestCut = juce::jmin (deepestCut, c.bands[b].gainDb);
                expect (minDb >= deepestCut - 1.0f,
                        "no unexpected cancellation: min " + juce::String (minDb, 2)
                            + " dB vs deepest cut " + juce::String (deepestCut, 1) + " dB");
            }
        }

        testZeroGainWithMovedFrequencies();
    }

private:
    static void setParam (C4Processor& proc, C4ParamIndex idx, const juce::String& text)
    {
        proc.getC4Parameter (idx)->setValue (
            proc.getC4Parameter (idx)->getValueForText (text));
    }

    static void applySettings (C4Processor& proc, const BandSetting* bands, int numBands)
    {
        for (int b = 0; b < numBands; ++b)
        {
            setParam (proc, bands[b].freqIdx, juce::String (bands[b].freq));
            setParam (proc, bands[b].gainIdx, juce::String (bands[b].gainDb, 1));
        }
    }

    static void setOneBand (C4Processor& proc, const BandSetting& band)
    {
        setParam (proc, band.freqIdx, juce::String (band.freq));
        setParam (proc, band.gainIdx, juce::String (band.gainDb, 1));
    }

    void testZeroGainWithMovedFrequencies()
    {
        beginTest ("Zero-gain neutrality holds even with all frequencies/modes moved");
        const double rate = 48000.0;
        const int block = 512;

        auto proc = C4Test::makePreparedProcessor (rate, block);
        setParam (*proc, C4ParamIndex::kWeightFreq, "400");
        setParam (*proc, C4ParamIndex::kSculptFreq, "2200");
        setParam (*proc, C4ParamIndex::kBiteFreq, "8000");
        setParam (*proc, C4ParamIndex::kOpenFreq, "18000");
        setParam (*proc, C4ParamIndex::kWeightMode, "Shelf");
        setParam (*proc, C4ParamIndex::kOpenMode, "HALO");
        setParam (*proc, C4ParamIndex::kSculptQ, "2.0");
        setParam (*proc, C4ParamIndex::kBiteQ, "0.6");

        juce::AudioBuffer<float> in (2, block);
        C4Test::fillDeterministic (in, 0x99ABCDEFu);
        auto out = in;
        proc->processBlock (out, juce::MidiBuffer());

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                if (out.getSample (ch, i) != in.getSample (ch, i))
                {
                    expect (false, "all gains at 0 dB must be bit-identical regardless "
                                   "of frequency/Q/mode settings");
                    return;
                }
    }
};

static C4CompositionTests c4CompositionTests;
