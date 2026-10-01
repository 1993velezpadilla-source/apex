#include <JuceHeader.h>
#include "G10TestUtils.h"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

// ============================================================================
// G10ProcessorTests — identity, parameter contract, buses, latency, and the
// tolerant ValueTree state machine of the real G10Processor.
// ============================================================================

namespace
{

using APEX::G10::G10Processor;
using APEX::G10::G10MiniEqCore;
using APEX::G10::G10Parameter;
using APEX::G10::G10CurveEngineCore;
using APEX::G10::kNumParams;
using APEX::G10::kNumBands;
using APEX::G10::kBandInfos;

void setDb (G10Parameter* p, float db)
{
    jassert (p != nullptr);
    p->setValue (p->getValueForText (juce::String (db, 1)));
}

juce::MemoryBlock saveState (G10Processor& proc)
{
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    return block;
}

void loadState (G10Processor& proc, const juce::MemoryBlock& block)
{
    proc.setStateInformation (block.getData(), (int) block.getSize());
}

} // namespace

class G10ProcessorTests : public juce::UnitTest
{
public:
    G10ProcessorTests() : juce::UnitTest ("G10.Processor", "APEX.G10") {}

    void runTest() override
    {
        testIdentity();
        testParameters();
        testExternalPublicParameterContract();
        testBusesAndLatency();
        testState();
        testCanonicalIdentity();
        testMiniEqDefaultsAndLaws();
        testMiniEqProbe();
        testMiniEqOffIdentity();
        testMiniEqResponses();
        testMiniEqNyquistSafety();
        testMiniEqSmoothing();
        testMiniEqReversal();
        testMiniEqSettledOffInitialization();
        testStateVersion2();
        testStateVersion3();
        testMiniEqBellBypass();
        testMiniEqCascadeNominal();
        testInputOutputMeters();
    }

    // ------------------------------------------------------------------
    // Mini EQ measurement helpers
    // ------------------------------------------------------------------

    /** Render a steady sine through the FULL processor (musical core + mini
        EQ) and return the RMS of the final block. Low level keeps D1B/I1 in
        their near-linear region; ratios vs the filter-off run cancel the
        chain's own response. */
    static double renderRms (G10Processor& proc, double rate, int block,
                             double frequency, int blocks = 24, double amp = 0.01)
    {
        proc.prepareToPlay (rate, block);
        double phase = 0.0;
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        double lastRms = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            for (int s = 0; s < block; ++s)
            {
                const float v = (float) (amp * std::sin (phase));
                phase += juce::MathConstants<double>::twoPi * frequency / rate;
                buffer.setSample (0, s, v);
                buffer.setSample (1, s, v);
            }
            proc.processBlock (buffer, midi);
            double sum = 0.0;
            for (int s = 0; s < block; ++s)
            {
                const double l = buffer.getSample (0, s);
                const double r = buffer.getSample (1, s);
                sum += l * l + r * r;
            }
            lastRms = std::sqrt (sum / (2.0 * block));
        }
        return lastRms;
    }

    static void setMiniEqOff (G10Processor& proc)
    {
        proc.getG10Parameter (APEX::G10::kHpf)->setValue (G10MiniEqCore::kHpfOffNorm);
        proc.getG10Parameter (APEX::G10::kLpf)->setValue (G10MiniEqCore::kLpfOffNorm);
        for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
        {
            const int base = APEX::G10::kBell1Enabled + b * 4;
            proc.getG10Parameter (base)->setValue (0.0f);
            proc.getG10Parameter (base + 2)->setValue (0.5f); // 0 dB
        }
    }

    static double miniEqGainDb (G10Processor& procOn, G10Processor& procOff,
                                double rate, int block, double frequency)
    {
        const double on = renderRms (procOn, rate, block, frequency);
        const double off = renderRms (procOff, rate, block, frequency);
        if (off <= 0.0 || on <= 0.0)
            return -300.0;
        return 20.0 * std::log10 (on / off);
    }

    // ------------------------------------------------------------------

    void testMiniEqProbe()
    {
        beginTest ("Mini EQ processor probe (diagnostic)");

        G10Processor proc;
        setMiniEqOff (proc);
        proc.getG10Parameter (APEX::G10::kHpf)->setValue (G10MiniEqCore::hpfNormFromHz (100.0f));
        proc.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        for (int b = 0; b < 24; ++b)
        {
            buffer.clear();
            for (int s = 0; s < 512; ++s)
            {
                const float v = 0.01f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 100.0 / 48000.0;
                buffer.setSample (0, s, v);
                buffer.setSample (1, s, v);
            }
            proc.processBlock (buffer, midi);
        }
        double sum = 0.0;
        for (int s = 0; s < 512; ++s)
        {
            const double l = buffer.getSample (0, s);
            sum += l * l;
        }
        const double rms = std::sqrt (sum / 512.0);
        expect (rms > 0.0, "probe rms=" + juce::String (rms, 6)
                + " hpfNorm=" + juce::String (proc.getG10Parameter (APEX::G10::kHpf)->getValue(), 4)
                + " hpfHz=" + juce::String (G10MiniEqCore::hpfHzFromNorm (proc.getG10Parameter (APEX::G10::kHpf)->getValue()), 1));
    }

    void testMiniEqDefaultsAndLaws()
    {
        beginTest ("Mini EQ defaults and control laws");

        G10Processor proc;
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kHpf)->getValue(),
                                   G10MiniEqCore::kHpfOffNorm, 1.0e-6f, "HPF defaults OFF");
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kLpf)->getValue(),
                                   G10MiniEqCore::kLpfOffNorm, 1.0e-6f, "LPF defaults OFF");
        for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
        {
            const int base = APEX::G10::kBell1Enabled + b * 4;
            expect (proc.getG10Parameter (base)->getBool() == false,
                    "bell " + juce::String (b + 1) + " disabled by default");
            expect (proc.getG10Parameter (APEX::G10::kBell1Bypass + b)->getBool() == false,
                    "bell " + juce::String (b + 1) + " not bypassed by default");
        }

        // Control-law round trips (v3: full 20 Hz..20 kHz on BOTH cutoffs,
        // shared log law, and Q 0.10..40.0).
        expectWithinAbsoluteError (G10MiniEqCore::hpfNormFromHz (G10MiniEqCore::hpfHzFromNorm (0.5f)),
                                   0.5f, 1.0e-4f, "HPF law round trip");
        expectWithinAbsoluteError (G10MiniEqCore::hpfHzFromNorm (1.0f), 20000.0f, 50.0f, "HPF max 20 kHz");
        expectWithinAbsoluteError (G10MiniEqCore::hpfHzFromNorm (G10MiniEqCore::hpfNormFromHz (100.0f)),
                                   100.0f, 0.5f, "HPF law round trip at 100 Hz");
        expectWithinAbsoluteError (G10MiniEqCore::lpfHzFromNorm (0.0f), 20.0f, 0.5f, "LPF min 20 Hz");
        expectWithinAbsoluteError (G10MiniEqCore::lpfHzFromNorm (G10MiniEqCore::lpfNormFromHz (12000.0f)),
                                   12000.0f, 50.0f, "LPF law round trip at 12 kHz");
        expectWithinAbsoluteError (G10MiniEqCore::lpfHzFromNorm (G10MiniEqCore::lpfNormFromHz (500.0f)),
                                   500.0f, 2.0f, "LPF law round trip at 500 Hz (below the old 8.5 kHz floor)");
        // The 20 kHz endpoint is the LPF OFF sentinel (p == 1); everything
        // below it is an ACTIVE position.
        expectWithinAbsoluteError (G10MiniEqCore::lpfNormFromHz (20000.0f), 1.0f, 1.0e-6f,
                                   "20 kHz maps to the LPF OFF endpoint");
        expect (G10MiniEqCore::lpfNormFromHz (19000.0f) < 1.0f,
                "19 kHz is an ACTIVE LPF position (not OFF)");
        // Both cutoffs share the analyzer log law: p == 0 -> 20 Hz,
        // p == 1 -> 20 kHz, p == 0.5 -> sqrt(20*20000) = 632.5 Hz.
        expectWithinAbsoluteError (G10MiniEqCore::hpfHzFromNorm (0.5f), 632.455f, 0.5f,
                                   "HPF mid maps to sqrt(20*20000)");
        expectWithinAbsoluteError (G10MiniEqCore::lpfHzFromNorm (0.5f), 632.455f, 0.5f,
                                   "LPF mid maps to sqrt(20*20000)");
        expectWithinAbsoluteError (G10MiniEqCore::bellHzFromNorm (G10MiniEqCore::bellNormFromHz (1000.0f)),
                                   1000.0f, 1.0f, "Bell freq law round trip");
        expectWithinAbsoluteError (G10MiniEqCore::bellGainDbFromNorm (0.5f), 0.0f, 1.0e-5f, "Bell gain 0 dB at mid");
        expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (G10MiniEqCore::bellNormFromQ (1.0f)),
                                   1.0f, 1.0e-4f, "Bell Q law round trip at 1.0");
        // v3 Bell Q: 0.10 .. 40.0, logarithmic (0.10 * 400^p).
        expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (0.0f), 0.10f, 1.0e-4f, "Bell Q min 0.10");
        expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (1.0f), 40.0f, 1.0e-2f, "Bell Q max 40");
        expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (G10MiniEqCore::bellNormFromQ (0.5f)),
                                   0.5f, 1.0e-4f, "Bell Q round trip at 0.5");
        expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (G10MiniEqCore::bellNormFromQ (10.0f)),
                                   10.0f, 1.0e-2f, "Bell Q round trip at 10");
        expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (G10MiniEqCore::bellNormFromQ (40.0f)),
                                   40.0f, 1.0e-2f, "Bell Q round trip at 40");
        expect (G10MiniEqCore::bellNormFromQ (1.0f) > G10MiniEqCore::bellNormFromQ (0.5f),
                "default Q 1.0 sits above Q 0.5 in the new log law");
        // OFF endpoints are NOT fake low cutoffs.
        expect (G10MiniEqCore::hpfHzFromNorm (0.0f) == 0.0f, "HPF OFF is a true bypass endpoint");
        expect (G10MiniEqCore::lpfHzFromNorm (1.0f) == 0.0f, "LPF OFF is a true bypass endpoint");
    }

    void testMiniEqOffIdentity()
    {
        beginTest ("Mini EQ OFF bit identity (core stage)");

        G10MiniEqCore core;
        G10MiniEqCore::Targets targets; // all defaults: OFF

        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            core.prepare (rate, 2);
            core.reset();
            for (const int block : { 64, 128, 512, 1024, 4096 })
            {
                for (const int channels : { 1, 2 })
                {
                    core.prepare (rate, channels);
                    core.reset();
                    std::vector<std::vector<float>> data ((size_t) channels, std::vector<float> ((size_t) block));
                    std::vector<float*> ptrs ((size_t) channels);
                    for (int c = 0; c < channels; ++c)
                        ptrs[(size_t) c] = data[(size_t) c].data();

                    // Deterministic pseudo-random-ish input (finite, varied).
                    unsigned seed = (unsigned) (rate + block + channels);
                    for (int c = 0; c < channels; ++c)
                        for (int s = 0; s < block; ++s)
                        {
                            seed = seed * 1664525u + 1013904223u;
                            data[(size_t) c][(size_t) s] = (float) (seed / 4294967296.0 - 0.5);
                        }

                    std::vector<std::vector<float>> copy = data;

                    core.processBlock (ptrs.data(), block, targets);
                    core.processBlock (ptrs.data(), block, targets); // settled OFF twice

                    bool identical = true;
                    for (int c = 0; c < channels; ++c)
                        for (int s = 0; s < block; ++s)
                            if (data[(size_t) c][(size_t) s] != copy[(size_t) c][(size_t) s])
                                identical = false;
                    expect (identical, "mini EQ OFF bit-identical at "
                            + juce::String (rate / 1000.0, 1) + "k block " + juce::String (block)
                            + " ch " + juce::String (channels));
                }
            }
        }

        // A Bell enabled at exactly 0 dB is skipped (no sample difference).
        core.prepare (48000.0, 2);
        core.reset();
        targets.bell[0].enabled = true;
        targets.bell[0].freqNorm = G10MiniEqCore::bellNormFromHz (1000.0f);
        targets.bell[0].gainDb = 0.0f;
        targets.bell[0].qNorm = G10MiniEqCore::bellNormFromQ (2.0f);
        std::vector<std::vector<float>> data (2, std::vector<float> (512));
        std::vector<float*> ptrs (2);
        for (int c = 0; c < 2; ++c)
            ptrs[(size_t) c] = data[(size_t) c].data();
        unsigned seed = 12345u;
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s)
            {
                seed = seed * 1664525u + 1013904223u;
                data[(size_t) c][(size_t) s] = (float) (seed / 4294967296.0 - 0.5);
            }
        const auto copy = data;
        core.processBlock (ptrs.data(), 512, targets);
        bool identical = true;
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s)
                if (data[(size_t) c][(size_t) s] != copy[(size_t) c][(size_t) s])
                    identical = false;
        expect (identical, "enabled 0 dB bell is skipped bit-exactly");
    }

    void testMiniEqResponses()
    {
        beginTest ("Mini EQ measured responses (12 dB/oct, flat, no bump)");

        const double rate = 48000.0;
        const int block = 512;

        // ---- HPF at 100 Hz: -3 dB at cutoff, ~-12 dB at 1 octave below,
        // flat above 2x cutoff, no resonant bump just above cutoff.
        {
            G10Processor on;
            G10Processor off;
            setMiniEqOff (off);
            on.getG10Parameter (APEX::G10::kHpf)->setValue (
                G10MiniEqCore::hpfNormFromHz (100.0f));

            const double atCutoff = miniEqGainDb (on, off, rate, block, 100.0);
            const double oneOctBelow = miniEqGainDb (on, off, rate, block, 50.0);
            const double twoOctBelow = miniEqGainDb (on, off, rate, block, 25.0);
            const double above = miniEqGainDb (on, off, rate, block, 400.0);
            const double farAbove = miniEqGainDb (on, off, rate, block, 2000.0);

            expectWithinAbsoluteError (atCutoff, -3.0, 1.0, "HPF -3 dB at cutoff");
            expect (oneOctBelow < atCutoff - 6.0 && oneOctBelow > atCutoff - 15.0,
                    "HPF ~12 dB/oct below cutoff (" + juce::String (oneOctBelow, 2) + " dB)");
            expect (twoOctBelow < oneOctBelow - 6.0 && twoOctBelow > oneOctBelow - 15.0,
                    "HPF continues ~12 dB/oct (" + juce::String (twoOctBelow, 2) + " dB)");
            expect (std::abs (above) < 1.0, "HPF flat passband above cutoff (" + juce::String (above, 2) + " dB)");
            expect (std::abs (farAbove) < 1.0, "HPF flat far above (" + juce::String (farAbove, 2) + " dB)");

            // No resonant bump: response just above cutoff must not exceed the
            // far-passband level by more than 0.5 dB.
            const double nearBump = miniEqGainDb (on, off, rate, block, 141.0);
            expect (nearBump - farAbove < 0.5, "HPF has no resonant bump (" + juce::String (nearBump, 2) + " dB)");
        }

        // ---- LPF at 12 kHz: -3 dB at cutoff, ~-12 dB one octave above,
        // flat below; 16 kHz position is measurably ACTIVE; OFF is identity.
        {
            G10Processor on;
            G10Processor off;
            setMiniEqOff (off);
            on.getG10Parameter (APEX::G10::kLpf)->setValue (
                G10MiniEqCore::lpfNormFromHz (12000.0f));

            const double atCutoff = miniEqGainDb (on, off, rate, block, 12000.0);
            const double oneOctAbove = miniEqGainDb (on, off, rate, block, 18000.0);
            const double below = miniEqGainDb (on, off, rate, block, 4000.0);

            expectWithinAbsoluteError (atCutoff, -3.0, 1.2, "LPF -3 dB at cutoff");
            expect (oneOctAbove < atCutoff - 6.0 && oneOctAbove > atCutoff - 15.0,
                    "LPF ~12 dB/oct above cutoff (" + juce::String (oneOctAbove, 2) + " dB)");
            expect (std::abs (below) < 1.0, "LPF flat passband below (" + juce::String (below, 2) + " dB)");

            // 16 kHz position is ACTIVE (not OFF).
            G10Processor at16k;
            setMiniEqOff (at16k);
            at16k.getG10Parameter (APEX::G10::kLpf)->setValue (
                G10MiniEqCore::lpfNormFromHz (16000.0f));
            const double at16 = miniEqGainDb (at16k, off, rate, block, 16000.0);
            expect (at16 < -1.5, "16 kHz LPF position measurably filters (" + juce::String (at16, 2) + " dB)");
        }

        // ---- Bell 1 kHz +6 dB Q 2: +6 at center, ~+1.5 at fc*2 (Q2 skirt),
        // returns to ~0 in the far skirt.
        {
            G10Processor on;
            G10Processor off;
            setMiniEqOff (off);
            const int base = APEX::G10::kBell1Enabled;
            on.getG10Parameter (base)->setValue (1.0f);
            on.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (1000.0f));
            on.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (6.0f));
            on.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (2.0f));

            const double center = miniEqGainDb (on, off, rate, block, 1000.0);
            const double skirt = miniEqGainDb (on, off, rate, block, 2000.0);
            const double far = miniEqGainDb (on, off, rate, block, 400.0);

            expectWithinAbsoluteError (center, 6.0, 0.6, "Bell +6 dB at center");
            expect (skirt > 0.5 && skirt < 4.5, "Bell Q2 skirt ~+1.5 dB at 2x (" + juce::String (skirt, 2) + ")");
            expect (std::abs (far) < 1.2, "Bell far skirt ~0 (" + juce::String (far, 2) + ")");

            // Q 10 is narrow, Q 0.5 is wide.
            G10Processor narrow;
            setMiniEqOff (narrow);
            narrow.getG10Parameter (base)->setValue (1.0f);
            narrow.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (1000.0f));
            narrow.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (6.0f));
            narrow.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (10.0f));
            const double narrowSkirt = miniEqGainDb (narrow, off, rate, block, 1200.0);
            expect (narrowSkirt < skirt, "Q10 skirt narrower than Q2 (" + juce::String (narrowSkirt, 2) + ")");

            G10Processor wide;
            setMiniEqOff (wide);
            wide.getG10Parameter (base)->setValue (1.0f);
            wide.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (1000.0f));
            wide.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (6.0f));
            wide.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (0.5f));
            const double wideSkirt = miniEqGainDb (wide, off, rate, block, 2000.0);
            expect (wideSkirt > skirt, "Q0.5 skirt wider than Q2 (" + juce::String (wideSkirt, 2) + ")");
        }
    }

    void testMiniEqNyquistSafety()
    {
        beginTest ("Mini EQ Nyquist / sample-rate safety");

        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            G10Processor proc;
            setMiniEqOff (proc);
            const int base = APEX::G10::kBell1Enabled;
            proc.getG10Parameter (base)->setValue (1.0f);
            proc.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (20000.0f));
            proc.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (12.0f));
            proc.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (10.0f));
            proc.getG10Parameter (APEX::G10::kHpf)->setValue (1.0f);          // 420 Hz
            proc.getG10Parameter (APEX::G10::kLpf)->setValue (0.0f);          // 8.5 kHz

            const double rms = renderRms (proc, rate, 512, 1000.0, 24);
            expect (std::isfinite (rms) && rms > 0.0,
                    "extreme Mini EQ settings finite at " + juce::String (rate / 1000.0, 1) + "k");
        }
    }

    void testMiniEqSmoothing()
    {
        beginTest ("Mini EQ smoothing / click-free transitions");

        G10Processor proc;
        setMiniEqOff (proc);
        proc.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;

        // HPF OFF -> ON (400 Hz), LPF OFF -> ON (9 kHz), a Bell enabled,
        // then everything back OFF. No NaN/Inf and no sample-to-sample jumps
        // larger than 20% of the signal amplitude (click-free proxy). The
        // buffer is REFILLED every block (processBlock is in place); the
        // first sample seeds the previous-value state (no initialization
        // artifact).
        auto sweep = [&] (int blocks)
        {
            double maxJump = 0.0;
            float prev[2] = { 0.0f, 0.0f };
            bool firstSample = true;
            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();
                for (int s = 0; s < 512; ++s)
                {
                    const float v = 0.05f * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 997.0 / 48000.0;
                    buffer.setSample (0, s, v);
                    buffer.setSample (1, s, v);
                }
                proc.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < 512; ++s)
                    {
                        const float v = buffer.getSample (ch, s);
                        if (! std::isfinite (v))
                            return -1.0;
                        if (! firstSample)
                        {
                            const double jump = std::abs ((double) v - prev[ch]);
                            if (jump > maxJump)
                                maxJump = jump;
                        }
                        prev[ch] = v;
                    }
                firstSample = false;
            }
            return maxJump;
        };

        proc.getG10Parameter (APEX::G10::kHpf)->setValue (G10MiniEqCore::hpfNormFromHz (400.0f));
        proc.getG10Parameter (APEX::G10::kLpf)->setValue (G10MiniEqCore::lpfNormFromHz (9000.0f));
        const int base = APEX::G10::kBell1Enabled;
        proc.getG10Parameter (base)->setValue (1.0f);
        proc.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (2000.0f));
        proc.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (8.0f));
        proc.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (3.0f));
        const double inJump = sweep (8);
        expect (inJump >= 0.0 && inJump < 0.02, "transition into Mini EQ is click-free (max jump "
                + juce::String (inJump, 5) + ")");

        setMiniEqOff (proc);
        const double outJump = sweep (8);
        expect (outJump >= 0.0 && outJump < 0.02, "transition out of Mini EQ is click-free (max jump "
                + juce::String (outJump, 5) + ")");
    }

    void testMiniEqReversal()
    {
        beginTest ("Mini EQ rapid transition reversal safety");

        // ACTIVE -> OFF -> ACTIVE and OFF -> ACTIVE -> OFF, reversed BEFORE
        // the ~5 ms transition completes. Must never click, produce NaN/Inf,
        // or blow up filter state — a reversal continues from the CURRENT
        // transition state.
        auto reversalSweep = [this] (G10Processor& proc,
                                     const std::function<void (bool active)>& setActive,
                                     int blocksBetween)
        {
            double maxJump = 0.0;
            float prev[2] = { 0.0f, 0.0f };
            bool firstSample = true;
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;

            setActive (true);
            for (int rep = 0; rep < 6; ++rep)
            {
                for (int b = 0; b < blocksBetween; ++b)
                {
                    buffer.clear();
                    for (int s = 0; s < 512; ++s)
                    {
                        const float v = 0.05f * (float) std::sin (phase);
                        phase += juce::MathConstants<double>::twoPi * 997.0 / 48000.0;
                        buffer.setSample (0, s, v);
                        buffer.setSample (1, s, v);
                    }
                    proc.processBlock (buffer, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int s = 0; s < 512; ++s)
                        {
                            const float v = buffer.getSample (ch, s);
                            if (! std::isfinite (v))
                                return -1.0;
                            if (! firstSample)
                            {
                                const double jump = std::abs ((double) v - prev[ch]);
                                if (jump > maxJump)
                                    maxJump = jump;
                            }
                            prev[ch] = v;
                        }
                    firstSample = false;
                }
                setActive ((rep & 1) == 0); // toggle each rep
            }
            return maxJump;
        };

        // HPF 400 Hz toggling every 2 blocks (~21 ms — a reversal occurs
        // before the 5 ms transition of the FIRST toggle completes).
        {
            G10Processor proc;
            setMiniEqOff (proc);
            proc.prepareToPlay (48000.0, 512);
            const double jump = reversalSweep (proc,
                [&] (bool active)
                {
                    proc.getG10Parameter (APEX::G10::kHpf)->setValue (
                        active ? G10MiniEqCore::hpfNormFromHz (400.0f)
                               : G10MiniEqCore::kHpfOffNorm);
                }, 2);
            expect (jump >= 0.0 && jump < 0.02, "HPF rapid reversal click-free (max jump "
                    + juce::String (jump, 5) + ")");
        }

        // LPF 12 kHz toggling every 2 blocks.
        {
            G10Processor proc;
            setMiniEqOff (proc);
            proc.prepareToPlay (48000.0, 512);
            const double jump = reversalSweep (proc,
                [&] (bool active)
                {
                    proc.getG10Parameter (APEX::G10::kLpf)->setValue (
                        active ? G10MiniEqCore::lpfNormFromHz (12000.0f)
                               : G10MiniEqCore::kLpfOffNorm);
                }, 2);
            expect (jump >= 0.0 && jump < 0.02, "LPF rapid reversal click-free (max jump "
                    + juce::String (jump, 5) + ")");
        }

        // Bell enable + gain toggling every 2 blocks.
        {
            G10Processor proc;
            setMiniEqOff (proc);
            proc.prepareToPlay (48000.0, 512);
            const int base = APEX::G10::kBell1Enabled;
            const double jump = reversalSweep (proc,
                [&] (bool active)
                {
                    proc.getG10Parameter (base)->setValue (active ? 1.0f : 0.0f);
                    proc.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (2000.0f));
                    proc.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (active ? 8.0f : 0.0f));
                    proc.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (3.0f));
                }, 2);
            expect (jump >= 0.0 && jump < 0.02, "Bell rapid reversal click-free (max jump "
                    + juce::String (jump, 5) + ")");
        }
    }

    void testMiniEqSettledOffInitialization()
    {
        beginTest ("Mini EQ starts settled OFF (no initial transition)");

        // A legacy v1 state (or defaults) must leave the Mini EQ stage in the
        // settled-bypass state at prepare: NO 5 ms transition passes through
        // at startup, and the very first processBlock call skips the stage.
        G10Processor proc; // defaults: all Mini EQ OFF
        proc.prepareToPlay (48000.0, 512);
        expect (proc.getMiniEq().isSettledOff(),
                "fresh processor mini EQ is settled OFF after prepare");

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                buffer.setSample (ch, s, 0.25f * std::sin (s * 0.13f));

        proc.processBlock (buffer, midi);
        expect (proc.getMiniEq().isSettledOff(),
                "mini EQ remains settled OFF after the first block (no transition started)");

        // Direct core check: the very first processBlock with all-OFF targets
        // is bit-identical (stage skipped, zero arithmetic).
        G10MiniEqCore core;
        core.prepare (48000.0, 2);
        G10MiniEqCore::Targets targets;
        std::vector<std::vector<float>> data (2, std::vector<float> (512));
        std::vector<float*> ptrs (2);
        for (int c = 0; c < 2; ++c)
            ptrs[(size_t) c] = data[(size_t) c].data();
        unsigned seed = 777u;
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s)
            {
                seed = seed * 1664525u + 1013904223u;
                data[(size_t) c][(size_t) s] = (float) (seed / 4294967296.0 - 0.5);
            }
        const auto copy = data;
        core.processBlock (ptrs.data(), 512, targets); // FIRST call ever
        bool identical = true;
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s)
                if (data[(size_t) c][(size_t) s] != copy[(size_t) c][(size_t) s])
                    identical = false;
        expect (identical, "first-ever core block with Mini EQ OFF is bit-identical "
                "(no initial transition, no filter arithmetic)");
    }

    void testStateVersion2()
    {
        beginTest ("State v2 round trip and v1 backward compatibility");

        // v2 round trip preserves all Mini EQ values.
        G10Processor a;
        a.getG10Parameter (APEX::G10::kHpf)->setValue (G10MiniEqCore::hpfNormFromHz (120.0f));
        a.getG10Parameter (APEX::G10::kLpf)->setValue (G10MiniEqCore::lpfNormFromHz (11000.0f));
        for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
        {
            const int base = APEX::G10::kBell1Enabled + b * 4;
            a.getG10Parameter (base)->setValue (1.0f);
            a.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (500.0f * (b + 1)));
            a.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (-3.0f * (b + 1)));
            a.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (1.5f + 0.5f * b));
        }
        const auto saved = saveState (a);

        G10Processor b;
        loadState (b, saved);
        expectWithinAbsoluteError (b.getG10Parameter (APEX::G10::kHpf)->getValue(),
                                   G10MiniEqCore::hpfNormFromHz (120.0f), 1.0e-6f, "HPF restored");
        expectWithinAbsoluteError (b.getG10Parameter (APEX::G10::kLpf)->getValue(),
                                   G10MiniEqCore::lpfNormFromHz (11000.0f), 1.0e-6f, "LPF restored");
        for (int i = 0; i < G10MiniEqCore::kMaxBells; ++i)
        {
            const int base = APEX::G10::kBell1Enabled + i * 4;
            expect (b.getG10Parameter (base)->getBool(), "bell " + juce::String (i + 1) + " enabled restored");
            expectWithinAbsoluteError (b.getG10Parameter (base + 1)->getValue(),
                                       G10MiniEqCore::bellNormFromHz (500.0f * (i + 1)), 1.0e-6f,
                                       "bell " + juce::String (i + 1) + " freq restored");
            expectWithinAbsoluteError (b.getG10Parameter (base + 2)->getUnitsValue(),
                                       -3.0f * (i + 1), 1.0e-4f,
                                       "bell " + juce::String (i + 1) + " gain restored");
            expectWithinAbsoluteError (b.getG10Parameter (base + 3)->getValue(),
                                       G10MiniEqCore::bellNormFromQ (1.5f + 0.5f * i), 1.0e-6f,
                                       "bell " + juce::String (i + 1) + " Q restored");
        }

        // v1 (legacy 15-field, version 1) state: all Mini EQ defaults -> OFF.
        G10Processor legacy;
        legacy.getG10Parameter (APEX::G10::kBand31)->setValue (0.75f);
        legacy.getG10Parameter (APEX::G10::kAnalog)->setValue (1.0f);
        juce::ValueTree v1 ("g10state");
        v1.setProperty ("version", 1, nullptr);
        for (int i = 0; i < 15; ++i)
        {
            const auto* id = G10Processor::getParamId (i);
            if (id != nullptr)
                v1.setProperty (juce::Identifier (id), legacy.getG10Parameter (i)->getValue(), nullptr);
        }
        juce::MemoryBlock v1Block;
        juce::MemoryOutputStream stream (v1Block, false);
        v1.writeToStream (stream);

        G10Processor restored;
        loadState (restored, v1Block);
        expectWithinAbsoluteError (restored.getG10Parameter (APEX::G10::kHpf)->getValue(),
                                   G10MiniEqCore::kHpfOffNorm, 1.0e-6f, "v1 restore: HPF OFF");
        expectWithinAbsoluteError (restored.getG10Parameter (APEX::G10::kLpf)->getValue(),
                                   G10MiniEqCore::kLpfOffNorm, 1.0e-6f, "v1 restore: LPF OFF");
        for (int i = 0; i < G10MiniEqCore::kMaxBells; ++i)
        {
            expect (restored.getG10Parameter (APEX::G10::kBell1Enabled + i * 4)->getBool() == false,
                    "v1 restore: bell " + juce::String (i + 1) + " disabled");
            expect (restored.getG10Parameter (APEX::G10::kBell1Bypass + i)->getBool() == false,
                    "v1 restore: bell " + juce::String (i + 1) + " not bypassed");
        }
    }

    void testStateVersion3()
    {
        beginTest ("State v3: v1 defaults, v2 semantic migration, v3 round trip");

        // ---- v1 load: Mini EQ stays at defaults EXACTLY as before ----------
        // A genuine v1 state has ONLY the 15 frozen fields; Mini EQ fields
        // are absent and must restore to defaults (no migration, no bypass).
        {
            G10Processor legacy;
            legacy.getG10Parameter (APEX::G10::kBand31)->setValue (0.75f);
            legacy.getG10Parameter (APEX::G10::kAnalog)->setValue (1.0f);
            juce::ValueTree v1 ("g10state");
            v1.setProperty ("version", 1, nullptr);
            for (int i = 0; i < 15; ++i)
            {
                const auto* id = G10Processor::getParamId (i);
                if (id != nullptr)
                    v1.setProperty (juce::Identifier (id), legacy.getG10Parameter (i)->getValue(), nullptr);
            }
            juce::MemoryBlock v1Block;
            juce::MemoryOutputStream stream (v1Block, false);
            v1.writeToStream (stream);

            G10Processor restored;
            loadState (restored, v1Block);
            expectWithinAbsoluteError (restored.getG10Parameter (APEX::G10::kHpf)->getValue(),
                                       G10MiniEqCore::kHpfOffNorm, 1.0e-6f, "v1 restore: HPF OFF");
            expectWithinAbsoluteError (restored.getG10Parameter (APEX::G10::kLpf)->getValue(),
                                       G10MiniEqCore::kLpfOffNorm, 1.0e-6f, "v1 restore: LPF OFF");
            for (int i = 0; i < G10MiniEqCore::kMaxBells; ++i)
            {
                expect (restored.getG10Parameter (APEX::G10::kBell1Enabled + i * 4)->getBool() == false,
                        "v1 restore: bell " + juce::String (i + 1) + " absent");
                expect (restored.getG10Parameter (APEX::G10::kBell1Bypass + i)->getBool() == false,
                        "v1 restore: bell " + juce::String (i + 1) + " not bypassed");
            }
        }

        // A stray Mini-EQ-looking property inside a v1-shaped state is NOT a
        // v2 semantic value: it must be restored RAW (tolerant restore) and
        // must never be run through the legacy-law migration.
        {
            juce::ValueTree v1 ("g10state");
            v1.setProperty ("version", 1, nullptr);
            v1.setProperty ("g10.hpf", 0.1234f, nullptr);
            v1.setProperty ("g10.bell1.q", 0.6543f, nullptr);
            juce::MemoryBlock v1Block;
            juce::MemoryOutputStream stream (v1Block, false);
            v1.writeToStream (stream);

            G10Processor restored;
            loadState (restored, v1Block);
            expectWithinAbsoluteError (restored.getG10Parameter (APEX::G10::kHpf)->getValue(),
                                       0.1234f, 1.0e-6f, "v1 stray HPF property restored raw (no migration)");
            expectWithinAbsoluteError (restored.getG10Parameter (APEX::G10::kBell1Q)->getValue(),
                                       0.6543f, 1.0e-6f, "v1 stray Bell Q property restored raw (no migration)");
        }

        // ---- v2 load: SEMANTIC migration preserves the old sound -----------
        // A genuine v2 state stores normalized values under the OLD laws.
        // Decode with the EXACT legacy helpers (same constants the old code
        // used), then re-encode with the new laws.
        {
            const float oldHpfNorm = std::log (100.0f / G10MiniEqCore::kHpfMinHz)
                                   / std::log (G10MiniEqCore::kLegacyHpfMaxHz / G10MiniEqCore::kHpfMinHz);       // 100 Hz under the old law
            const float oldLpfNorm = std::log (12000.0f / G10MiniEqCore::kLegacyLpfMinHz)
                                   / std::log (G10MiniEqCore::kLegacyLpfLawMaxHz / G10MiniEqCore::kLegacyLpfMinHz); // 12 kHz under the old law
            const float oldQNorm   = std::log (3.0f / G10MiniEqCore::kLegacyBellMinQ)
                                   / std::log (G10MiniEqCore::kLegacyBellMaxQ / G10MiniEqCore::kLegacyBellMinQ);   // Q 3 under the old law

            juce::ValueTree v2 ("g10state");
            v2.setProperty ("version", 2, nullptr);
            v2.setProperty ("g10.hpf", oldHpfNorm, nullptr);
            v2.setProperty ("g10.lpf", oldLpfNorm, nullptr);
            v2.setProperty ("g10.bell1.enabled", 1.0f, nullptr);
            v2.setProperty ("g10.bell1.freq", G10MiniEqCore::bellNormFromHz (1000.0f), nullptr);
            v2.setProperty ("g10.bell1.gain", G10MiniEqCore::bellNormFromGainDb (6.0f), nullptr);
            v2.setProperty ("g10.bell1.q", oldQNorm, nullptr);
            v2.setProperty ("g10.bell3.enabled", 1.0f, nullptr);
            v2.setProperty ("g10.bell3.q", oldQNorm, nullptr);
            juce::MemoryBlock v2Block;
            juce::MemoryOutputStream stream (v2Block, false);
            v2.writeToStream (stream);

            G10Processor restored;
            loadState (restored, v2Block);

            // Old HPF 100 Hz -> new normalized -> 100 Hz (NOT the raw number).
            expectWithinAbsoluteError (G10MiniEqCore::hpfHzFromNorm (
                                           restored.getG10Parameter (APEX::G10::kHpf)->getValue()),
                                       100.0f, 1.0f, "v2 HPF semantic Hz preserved");
            // Old LPF 12 kHz -> new normalized -> 12 kHz.
            expectWithinAbsoluteError (G10MiniEqCore::lpfHzFromNorm (
                                           restored.getG10Parameter (APEX::G10::kLpf)->getValue()),
                                       12000.0f, 50.0f, "v2 LPF semantic Hz preserved");
            // Old Q 3 -> new normalized -> Q 3 (both migrated bells).
            expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (
                                           restored.getG10Parameter (APEX::G10::kBell1Q)->getValue()),
                                       3.0f, 1.0e-2f, "v2 Bell1 Q preserved");
            expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (
                                           restored.getG10Parameter (APEX::G10::kBell3Q)->getValue()),
                                       3.0f, 1.0e-2f, "v2 Bell3 Q preserved");
            // Laws unchanged in v3 pass through untouched.
            expectWithinAbsoluteError (G10MiniEqCore::bellHzFromNorm (
                                           restored.getG10Parameter (APEX::G10::kBell1Freq)->getValue()),
                                       1000.0f, 1.0f, "v2 Bell freq pass-through");
            expectWithinAbsoluteError (restored.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue(),
                                       6.0f, 1.0e-4f, "v2 Bell gain pass-through");
            // New bypass parameters default to false for every Bell.
            for (int i = 0; i < G10MiniEqCore::kMaxBells; ++i)
                expect (restored.getG10Parameter (APEX::G10::kBell1Bypass + i)->getBool() == false,
                        "v2 load: bell " + juce::String (i + 1) + " bypass defaults false");
        }

        // ---- v3: direct round trip, NO migration ----------------------------
        // Values are chosen so a buggy legacy migration would CHANGE them
        // (e.g. hpf 0.9 under the old law decodes to ~309 Hz, which re-encodes
        // to ~0.393 — nowhere near 0.9).
        {
            G10Processor a;
            a.getG10Parameter (APEX::G10::kHpf)->setValue (0.9f);
            a.getG10Parameter (APEX::G10::kLpf)->setValue (0.13f);
            a.getG10Parameter (APEX::G10::kBell1Q)->setValue (0.77f);
            a.getG10Parameter (APEX::G10::kBell2Q)->setValue (0.9f);
            a.getG10Parameter (APEX::G10::kBell3Q)->setValue (0.01f);
            a.getG10Parameter (APEX::G10::kBell1Bypass)->setValue (1.0f);
            a.getG10Parameter (APEX::G10::kBell3Bypass)->setValue (1.0f);
            const auto saved = saveState (a);

            G10Processor b;
            loadState (b, saved);
            expectWithinAbsoluteError (b.getG10Parameter (APEX::G10::kHpf)->getValue(),
                                       0.9f, 1.0e-6f, "v3 HPF direct round trip (no migration)");
            expectWithinAbsoluteError (b.getG10Parameter (APEX::G10::kLpf)->getValue(),
                                       0.13f, 1.0e-6f, "v3 LPF direct round trip (no migration)");
            expectWithinAbsoluteError (b.getG10Parameter (APEX::G10::kBell1Q)->getValue(),
                                       0.77f, 1.0e-6f, "v3 Bell1 Q direct round trip (no migration)");
            expectWithinAbsoluteError (b.getG10Parameter (APEX::G10::kBell2Q)->getValue(),
                                       0.9f, 1.0e-6f, "v3 Bell2 Q direct round trip (no migration)");
            expectWithinAbsoluteError (b.getG10Parameter (APEX::G10::kBell3Q)->getValue(),
                                       0.01f, 1.0e-6f, "v3 Bell3 Q direct round trip (no migration)");
            expect (b.getG10Parameter (APEX::G10::kBell1Bypass)->getBool(),
                    "v3 Bell1 bypass round trip");
            expect (! b.getG10Parameter (APEX::G10::kBell2Bypass)->getBool(),
                    "v3 Bell2 bypass default false round trip");
            expect (b.getG10Parameter (APEX::G10::kBell3Bypass)->getBool(),
                    "v3 Bell3 bypass round trip");
        }
    }

    void testMiniEqBellBypass()
    {
        beginTest ("Mini EQ Bell bypass (v3): settled skip, edit-while-bypassed, click-free");

        const double rate = 48000.0;
        const int block = 512;
        const int base = APEX::G10::kBell1Enabled;
        const int bypassIdx = APEX::G10::kBell1Bypass;

        // ---- Settled bypass preserves zero-DSP / bit identity ---------------
        // A Bell enabled but fully bypassed (rest of the stage off) must take
        // the early-skip path: sample-exact equality with a reference
        // processor whose Mini EQ is entirely off (both run the same frozen
        // musical core; the D1B/I1 chain is deterministic for identical
        // processing history).
        {
            G10Processor proc;
            G10Processor reference;
            setMiniEqOff (proc);
            setMiniEqOff (reference);
            proc.prepareToPlay (rate, block);
            reference.prepareToPlay (rate, block);
            proc.getG10Parameter (base)->setValue (1.0f);
            proc.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (1000.0f));
            proc.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (8.0f));
            proc.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (3.0f));
            proc.getG10Parameter (bypassIdx)->setValue (1.0f); // bypass ON

            G10Test::settleProcessor (proc, rate, block, 0.25);      // mix fully out
            G10Test::settleProcessor (reference, rate, block, 0.25);

            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            bool identical = true;
            for (int rep = 0; rep < 4; ++rep)
            {
                unsigned seed = 4242u + (unsigned) rep;
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < block; ++s)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        buffer.setSample (ch, s, (float) (seed / 4294967296.0 - 0.5));
                    }
                // Copy the RAW input BEFORE the in-place processing.
                juce::AudioBuffer<float> refBuf (buffer);
                proc.processBlock (buffer, midi);
                reference.processBlock (refBuf, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < block; ++s)
                        if (buffer.getSample (ch, s) != refBuf.getSample (ch, s))
                            identical = false;
            }
            expect (identical, "settled bypassed Bell: sample-exact vs all-off reference (zero-DSP skip)");

            // Bypass must not disturb the Bell's stored Freq/Gain/Q.
            expectWithinAbsoluteError (proc.getG10Parameter (base + 1)->getValue(),
                                       G10MiniEqCore::bellNormFromHz (1000.0f), 1.0e-6f,
                                       "bypass preserves Bell frequency");
            expectWithinAbsoluteError (proc.getG10Parameter (base + 2)->getUnitsValue(),
                                       8.0f, 1.0e-4f, "bypass preserves Bell gain");
            expectWithinAbsoluteError (proc.getG10Parameter (base + 3)->getValue(),
                                       G10MiniEqCore::bellNormFromQ (3.0f), 1.0e-6f,
                                       "bypass preserves Bell Q");
        }

        // ---- Edits while bypassed are adopted on un-bypass ------------------
        // While the Bell is fully bypassed (inaudible), change Freq/Gain/Q;
        // then un-bypass. After the ~5 ms fade-in the settled response must
        // equal a reference that was ACTIVE with the new settings from the
        // start (the snap-on-exit path, not a glide from the old values).
        {
            G10Processor on;
            G10Processor off;
            G10Processor toggled;
            setMiniEqOff (off);
            setMiniEqOff (toggled);
            on.prepareToPlay (rate, block);
            off.prepareToPlay (rate, block);
            toggled.prepareToPlay (rate, block);

            const float freqNorm = G10MiniEqCore::bellNormFromHz (1500.0f);
            const float gainDb = 6.0f;
            const float qNorm = G10MiniEqCore::bellNormFromQ (2.0f);

            on.getG10Parameter (base)->setValue (1.0f);
            on.getG10Parameter (base + 1)->setValue (freqNorm);
            on.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (gainDb));
            on.getG10Parameter (base + 3)->setValue (qNorm);

            toggled.getG10Parameter (base)->setValue (1.0f);
            toggled.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (500.0f));
            toggled.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (-4.0f));
            toggled.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (0.5f));
            toggled.getG10Parameter (bypassIdx)->setValue (1.0f);   // bypass ON
            G10Test::settleProcessor (toggled, rate, block, 0.25);  // fully out

            // Edit while inaudible, then un-bypass.
            toggled.getG10Parameter (base + 1)->setValue (freqNorm);
            toggled.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (gainDb));
            toggled.getG10Parameter (base + 3)->setValue (qNorm);
            toggled.getG10Parameter (bypassIdx)->setValue (0.0f);   // un-bypass

            // Steady-state gain at the bell center must match the reference.
            // miniEqGainDb re-prepares each processor (identical ramps in),
            // so the comparison isolates the adopted configuration.
            const double onDb = miniEqGainDb (on, off, rate, block, 1500.0);
            const double toggledDb = miniEqGainDb (toggled, off, rate, block, 1500.0);
            expectWithinAbsoluteError (onDb, 6.0, 0.6, "reference bell +6 dB at center");
            expectWithinAbsoluteError (toggledDb, onDb, 0.5,
                                       "edits made while bypassed are adopted on un-bypass (settled gain == reference)");
        }

        // ---- Rapid ON <-> OFF <-> ON (bypass toggling) stays click-free ----
        {
            G10Processor proc;
            setMiniEqOff (proc);
            proc.prepareToPlay (rate, block);
            proc.getG10Parameter (base)->setValue (1.0f);
            proc.getG10Parameter (base + 1)->setValue (G10MiniEqCore::bellNormFromHz (2000.0f));
            proc.getG10Parameter (base + 2)->setValue (G10MiniEqCore::bellNormFromGainDb (8.0f));
            proc.getG10Parameter (base + 3)->setValue (G10MiniEqCore::bellNormFromQ (3.0f));

            double maxJump = 0.0;
            float prev[2] = { 0.0f, 0.0f };
            bool firstSample = true;
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;
            for (int rep = 0; rep < 6; ++rep)
            {
                // ON (bypass 0) for 2 blocks, then OFF (bypass 1) for 2 blocks.
                proc.getG10Parameter (bypassIdx)->setValue ((rep & 1) == 0 ? 0.0f : 1.0f);
                for (int b = 0; b < 2; ++b)
                {
                    buffer.clear();
                    for (int s = 0; s < 512; ++s)
                    {
                        const float v = 0.05f * (float) std::sin (phase);
                        phase += juce::MathConstants<double>::twoPi * 997.0 / 48000.0;
                        buffer.setSample (0, s, v);
                        buffer.setSample (1, s, v);
                    }
                    proc.processBlock (buffer, midi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int s = 0; s < 512; ++s)
                        {
                            const float v = buffer.getSample (ch, s);
                            if (! std::isfinite (v))
                            {
                                expect (false, "bypass toggling produced non-finite output");
                                return;
                            }
                            if (! firstSample)
                                maxJump = juce::jmax (maxJump, std::abs ((double) v - prev[ch]));
                            prev[ch] = v;
                        }
                    firstSample = false;
                }
            }
            expect (maxJump < 0.02, "Bell bypass rapid reversal click-free (max jump "
                    + juce::String (maxJump, 5) + ")");
        }
    }

    void testMiniEqCascadeNominal()
    {
        beginTest ("Mini EQ nominal curve matches the implemented stages");

        // The z-domain magnitude helpers must agree with direct simulation of
        // the audio-path stages (locks the nominal curve to the DSP).
        G10MiniEqCore core;
        core.prepare (48000.0, 1);
        core.reset();

        G10MiniEqCore::Targets targets;
        targets.hpfNorm = G10MiniEqCore::hpfNormFromHz (80.0f);
        targets.lpfNorm = G10MiniEqCore::lpfNormFromHz (9000.0f);
        targets.bell[0].enabled = true;
        targets.bell[0].freqNorm = G10MiniEqCore::bellNormFromHz (1500.0f);
        targets.bell[0].gainDb = 6.0f;
        targets.bell[0].qNorm = G10MiniEqCore::bellNormFromQ (2.5f);
        targets.bell[1].enabled = true;
        targets.bell[1].freqNorm = G10MiniEqCore::bellNormFromHz (400.0f);
        targets.bell[1].gainDb = -5.0f;
        targets.bell[1].qNorm = G10MiniEqCore::bellNormFromQ (6.0f);
        targets.bell[2].enabled = true;
        targets.bell[2].freqNorm = G10MiniEqCore::bellNormFromHz (8000.0f);
        targets.bell[2].gainDb = 3.0f;
        targets.bell[2].qNorm = G10MiniEqCore::bellNormFromQ (1.0f);

        // Simulate the cascade with a complex exponential per test frequency
        // using the ACTUAL process() path (settled, all stages engaged).
        for (const double hz : { 80.0, 400.0, 1500.0, 8000.0, 9000.0, 12000.0 })
        {
            core.reset();
            const float w = (float) (2.0 * juce::MathConstants<double>::pi * hz / 48000.0);
            const float re = std::cos (w);
            const float im = std::sin (w);

            float accRe = 0.0f, accIm = 0.0f;
            float re1 = 1.0f, im1 = 0.0f;
            // Feed 8192 samples; correlate output with the CONJUGATE input
            // phasor over the last 2048 (steady state) to get the complex gain.
            for (int n = 0; n < 8192; ++n)
            {
                float data[1] = { re1 };
                float* ptrs[1] = { data };
                core.processBlock (ptrs, 1, targets);

                if (n >= 8192 - 2048)
                {
                    accRe += data[0] * re1;
                    accIm -= data[0] * im1; // conj(input)
                }

                const float nextRe = re1 * re - im1 * im;
                const float nextIm = re1 * im + im1 * re;
                re1 = nextRe;
                im1 = nextIm;
            }

            // Output/input correlation gives the complex gain. A real cosine
            // input correlates at HALF the phasor power, so the amplitude is
            // 2 * |corr|.
            const float corrRe = accRe / 2048.0f;
            const float corrIm = accIm / 2048.0f;
            const double measuredDb = 20.0 * std::log10 (2.0 * std::sqrt ((double) (corrRe * corrRe + corrIm * corrIm)) + 1.0e-30);
            const double nominalDb = core.getNominalMagnitudeDb ((float) hz, 48000.0f, targets);

            expectWithinAbsoluteError (measuredDb, nominalDb, 0.35,
                                       "nominal curve matches measured cascade at " + juce::String (hz, 0)
                                       + " Hz (" + juce::String (measuredDb, 2) + " vs " + juce::String (nominalDb, 2) + " dB)");
        }
    }

    // ------------------------------------------------------------------

    void testIdentity()
    {
        beginTest ("Identity");

        G10Processor proc;
        const auto desc = proc.getPluginDescription();

        expectEquals (proc.getName(), juce::String (G10Processor::kPluginName), "plugin name");
        expectEquals (desc.name, juce::String (G10Processor::kPluginName), "description name");
        expectEquals (desc.descriptiveName, juce::String (G10Processor::kPluginName), "descriptive name");
        expectEquals (desc.pluginFormatName, juce::String ("APEX Native"), "format name");
        expectEquals (desc.manufacturerName, juce::String ("APEX"), "manufacturer");
        expectEquals (desc.version, juce::String ("1.0.0"), "version");
        expectEquals (desc.fileOrIdentifier, juce::String ("APEX::G10"), "stable identifier");
        expectEquals (desc.uniqueId, (int) G10Processor::kUniqueId, "unique ID");
        expectEquals (desc.deprecatedUid, (int) G10Processor::kUniqueId, "deprecated UID");
        expect (! desc.isInstrument, "must not be an instrument");
        expectEquals (desc.numInputChannels, 2, "description input channels");
        expectEquals (desc.numOutputChannels, 2, "description output channels");
        expect (proc.hasEditor(), "native editor available");
        expectEquals (proc.getNumPrograms(), 1, "single program");
    }

    // ------------------------------------------------------------------

    void testParameters()
    {
        beginTest ("Parameter contract");

        G10Processor proc;

        // Exact 32 IDs in the exact required order (15 frozen + 14 Mini EQ
        // + 3 v3 Bell bypass, appended after the v2 IDs).
        const char* expected[kNumParams] =
        {
            "g10.input",
            "g10.band31", "g10.band63", "g10.band125", "g10.band250",
            "g10.band500", "g10.band1k", "g10.band2k", "g10.band4k",
            "g10.band8k", "g10.band16k",
            "g10.output",
            "g10.analog",
            "g10.quality",
            "g10.bypass",
            "g10.hpf",
            "g10.lpf",
            "g10.bell1.enabled", "g10.bell1.freq", "g10.bell1.gain", "g10.bell1.q",
            "g10.bell2.enabled", "g10.bell2.freq", "g10.bell2.gain", "g10.bell2.q",
            "g10.bell3.enabled", "g10.bell3.freq", "g10.bell3.gain", "g10.bell3.q",
            "g10.bell1.bypass", "g10.bell2.bypass", "g10.bell3.bypass",
        };

        expectEquals (proc.getNumParameters(), (int) kNumParams, "parameter count");

        for (int i = 0; i < kNumParams; ++i)
        {
            auto* p = proc.getParameters()[i];
            expect (p != nullptr, "null parameter at index " + juce::String (i));

            auto* pwid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
            expect (pwid != nullptr, "parameter " + juce::String (i) + " not castable to AudioProcessorParameterWithID");
            if (pwid != nullptr)
                expectEquals (pwid->paramID, juce::String (expected[i]),
                              "parameter ID at index " + juce::String (i));

            auto* g = dynamic_cast<G10Parameter*> (p);
            expect (g != nullptr, "parameter " + juce::String (i) + " not a G10Parameter");
        }

        // Ranges and defaults.
        auto* input = G10Test::findParam (proc, "g10.input");
        auto* output = G10Test::findParam (proc, "g10.output");
        auto* band = G10Test::findParam (proc, "g10.band1k");
        auto* bypass = G10Test::findParam (proc, "g10.bypass");

        expect (input != nullptr && output != nullptr && band != nullptr && bypass != nullptr,
                "required parameters missing");

        // Input/output trim: -18..+18 dB, default 0 dB (normalized 0.5).
        expectWithinAbsoluteError (input->getValue(), 0.5f, 1.0e-6f, "input default");
        expectWithinAbsoluteError (output->getValue(), 0.5f, 1.0e-6f, "output default");
        input->setValue (input->getValueForText ("-18"));
        expectWithinAbsoluteError (input->getUnitsValue(), -18.0f, 1.0e-3f, "input min");
        input->setValue (input->getValueForText ("18"));
        expectWithinAbsoluteError (input->getUnitsValue(), 18.0f, 1.0e-3f, "input max");
        input->setValue (input->getValueForText ("19")); // out of range -> clamped
        expectWithinAbsoluteError (input->getUnitsValue(), 18.0f, 1.0e-3f, "input clamp");

        // Bands: -12..+12 dB, default 0.
        expectWithinAbsoluteError (band->getValue(), 0.5f, 1.0e-6f, "band default");
        band->setValue (band->getValueForText ("-12"));
        expectWithinAbsoluteError (band->getUnitsValue(), -12.0f, 1.0e-3f, "band min");
        band->setValue (band->getValueForText ("12"));
        expectWithinAbsoluteError (band->getUnitsValue(), 12.0f, 1.0e-3f, "band max");

        // Toggles: discrete, boolean, 2 steps, On/Off text.
        expect (bypass->isDiscrete(), "bypass not discrete");
        expect (bypass->isBoolean(), "bypass not boolean");
        expectEquals (bypass->getNumSteps(), 2, "bypass steps");
        expectEquals (bypass->getText (0.0f, 0), juce::String ("Off"), "bypass text off");
        expectEquals (bypass->getText (1.0f, 0), juce::String ("On"), "bypass text on");
        expectEquals (bypass->getValueForText ("on"), 1.0f, "bypass text-to-value on");
        expectEquals (bypass->getValueForText ("off"), 0.0f, "bypass text-to-value off");
        expectEquals (bypass->getDefaultValue(), 0.0f, "bypass default");

        // Value-to-text for a band.
        band->setValue (band->getValueForText ("6"));
        expect (band->getText (band->getValue(), 0).startsWith ("6"), "band text formatting");
    }

    // ------------------------------------------------------------------

    void testExternalPublicParameterContract()
    {
        beginTest ("External VST3 public parameters and legacy state compatibility");

        G10Processor external (G10Processor::ParameterExposure::publicVst3);
        constexpr std::array<int, G10Processor::kNumPublicVst3Parameters> expectedSemanticIndexes {
            APEX::G10::kInput,
            APEX::G10::kBand31, APEX::G10::kBand63, APEX::G10::kBand125,
            APEX::G10::kBand250, APEX::G10::kBand500, APEX::G10::kBand1k,
            APEX::G10::kBand2k, APEX::G10::kBand4k, APEX::G10::kBand8k,
            APEX::G10::kBand16k, APEX::G10::kOutput, APEX::G10::kBypass,
            APEX::G10::kHpf, APEX::G10::kLpf,
            APEX::G10::kBell1Enabled, APEX::G10::kBell1Freq, APEX::G10::kBell1Gain, APEX::G10::kBell1Q,
            APEX::G10::kBell2Enabled, APEX::G10::kBell2Freq, APEX::G10::kBell2Gain, APEX::G10::kBell2Q,
            APEX::G10::kBell3Enabled, APEX::G10::kBell3Freq, APEX::G10::kBell3Gain, APEX::G10::kBell3Q,
            APEX::G10::kBell1Bypass, APEX::G10::kBell2Bypass, APEX::G10::kBell3Bypass
        };
        constexpr std::array<const char*, G10Processor::kNumPublicVst3Parameters> expectedIds {
            "g10.input",
            "g10.band31", "g10.band63", "g10.band125", "g10.band250",
            "g10.band500", "g10.band1k", "g10.band2k", "g10.band4k",
            "g10.band8k", "g10.band16k", "g10.output", "g10.bypass",
            "g10.hpf", "g10.lpf",
            "g10.bell1.enabled", "g10.bell1.freq", "g10.bell1.gain", "g10.bell1.q",
            "g10.bell2.enabled", "g10.bell2.freq", "g10.bell2.gain", "g10.bell2.q",
            "g10.bell3.enabled", "g10.bell3.freq", "g10.bell3.gain", "g10.bell3.q",
            "g10.bell1.bypass", "g10.bell2.bypass", "g10.bell3.bypass"
        };

        expectEquals (external.getNumParameters(), G10Processor::kNumPublicVst3Parameters,
                      "external public parameter count");
        for (int i = 0; i < G10Processor::kNumPublicVst3Parameters; ++i)
        {
            auto* parameter = external.getParameters()[i];
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            expect (withId != nullptr, "external parameter has stable ID at index " + juce::String (i));
            if (withId != nullptr)
                expectEquals (withId->paramID, juce::String (expectedIds[(size_t) i]),
                              "external stable ID at index " + juce::String (i));
            expectEquals (external.getSemanticParameterIndex (i), expectedSemanticIndexes[(size_t) i],
                          "external hosted-to-semantic mapping at index " + juce::String (i));
        }

        expect (G10Test::findParam (external, "g10.analog") == nullptr,
                "legacy Analog is not host-exposed");
        expect (G10Test::findParam (external, "g10.quality") == nullptr,
                "legacy Quality is not host-exposed");
        expect (external.getG10Parameter (APEX::G10::kAnalog) != nullptr,
                "legacy Analog state object remains owned");
        expect (external.getG10Parameter (APEX::G10::kQuality) != nullptr,
                "legacy Quality state object remains owned");
        expect (external.getBypassParameter() == external.getG10Parameter (APEX::G10::kBypass),
                "external Bypass remains the declared VST3 bypass parameter");

        // A state written by the 15-parameter Native processor must load into
        // the 13-public-parameter VST3 policy and survive a save back to Native.
        G10Processor oldNative;
        oldNative.getG10Parameter (APEX::G10::kAnalog)->setValue (1.0f);
        oldNative.getG10Parameter (APEX::G10::kQuality)->setValue (1.0f);
        setDb (oldNative.getG10Parameter (APEX::G10::kBand1k), 6.0f);
        loadState (external, saveState (oldNative));
        expect (external.getG10Parameter (APEX::G10::kAnalog)->getBool(),
                "external policy recalls legacy Analog state");
        expect (external.getG10Parameter (APEX::G10::kQuality)->getBool(),
                "external policy recalls legacy Quality state");
        expectWithinAbsoluteError (external.getG10Parameter (APEX::G10::kBand1k)->getUnitsValue(),
                                   6.0f, 1.0e-3f, "external policy recalls public state");

        G10Processor nativeRestored;
        loadState (nativeRestored, saveState (external));
        expect (nativeRestored.getG10Parameter (APEX::G10::kAnalog)->getBool(),
                "legacy Analog survives external save back to Native");
        expect (nativeRestored.getG10Parameter (APEX::G10::kQuality)->getBool(),
                "legacy Quality survives external save back to Native");
    }

    // ------------------------------------------------------------------

    void testBusesAndLatency()
    {
        beginTest ("Buses and latency");

        G10Processor proc;

        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        expect (proc.isBusesLayoutSupported (mono), "mono layout must be supported");

        juce::AudioProcessor::BusesLayout stereo;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        expect (proc.isBusesLayoutSupported (stereo), "stereo layout must be supported");

        juce::AudioProcessor::BusesLayout monoInStereoOut;
        monoInStereoOut.inputBuses.add (juce::AudioChannelSet::mono());
        monoInStereoOut.outputBuses.add (juce::AudioChannelSet::stereo());
        expect (! proc.isBusesLayoutSupported (monoInStereoOut), "mono-in/stereo-out must be rejected");

        juce::AudioProcessor::BusesLayout surround;
        surround.inputBuses.add (juce::AudioChannelSet::create5point1());
        surround.outputBuses.add (juce::AudioChannelSet::create5point1());
        expect (! proc.isBusesLayoutSupported (surround), "5.1 layout must be rejected");

        expectEquals (proc.getLatencySamples(), 0, "reported latency must be zero");
        expectEquals (proc.getTailLengthSeconds(), 0.0, "tail length zero");
    }

    // ------------------------------------------------------------------

    void testState()
    {
        beginTest ("State v1 save/reload and tolerance");

        // Save a state with several parameters set.
        G10Processor a;
        setDb (G10Test::findParam (a, "g10.band31"), 6.0f);
        setDb (G10Test::findParam (a, "g10.band1k"), -3.0f);
        setDb (G10Test::findParam (a, "g10.input"), 2.0f);
        G10Test::findParam (a, "g10.analog")->setValue (1.0f);
        G10Test::findParam (a, "g10.quality")->setValue (1.0f);
        const auto saved = saveState (a);

        G10Processor b;
        loadState (b, saved);
        expectWithinAbsoluteError (G10Test::findParam (b, "g10.band31")->getUnitsValue(), 6.0f, 1.0e-3f, "band31 restored");
        expectWithinAbsoluteError (G10Test::findParam (b, "g10.band1k")->getUnitsValue(), -3.0f, 1.0e-3f, "band1k restored");
        expectWithinAbsoluteError (G10Test::findParam (b, "g10.input")->getUnitsValue(), 2.0f, 1.0e-3f, "input restored");
        expect (G10Test::findParam (b, "g10.analog")->getBool(), "analog restored");
        expect (G10Test::findParam (b, "g10.quality")->getBool(), "quality restored");

        // Missing properties: keep defaults, no crash.
        {
            G10Processor c;
            juce::ValueTree state ("g10state");
            state.setProperty ("version", 1, nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            state.writeToStream (stream);
            loadState (c, block);
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band31")->getUnitsValue(), 0.0f, 1.0e-3f, "missing props keep defaults");
        }

        // Unknown properties: ignored, no crash.
        {
            G10Processor c;
            juce::ValueTree state ("g10state");
            state.setProperty ("version", 1, nullptr);
            state.setProperty ("g10.band31", 0.75f, nullptr);
            state.setProperty ("someUnknownProp", 123, nullptr);
            state.setProperty ("another.unknown", "hello", nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            state.writeToStream (stream);
            loadState (c, block);
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band31")->getUnitsValue(), 6.0f, 1.0e-3f, "unknown props ignored");
        }

        // Unknown future version: tolerant restore, no crash.
        {
            G10Processor c;
            juce::ValueTree state ("g10state");
            state.setProperty ("version", 999, nullptr);
            state.setProperty ("g10.band63", 0.75f, nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            state.writeToStream (stream);
            loadState (c, block);
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band63")->getUnitsValue(), 6.0f, 1.0e-3f, "future version restored");
        }

        // Invalid numeric values: no crash, finite params.
        {
            G10Processor c;
            juce::ValueTree state ("g10state");
            state.setProperty ("version", 1, nullptr);
            state.setProperty ("g10.band31", "not-a-number", nullptr);
            state.setProperty ("g10.band63", juce::var (juce::String ("abc")), nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            state.writeToStream (stream);
            loadState (c, block);
            bool finite = true;
            for (int i = 0; i < kNumParams; ++i)
                if (! std::isfinite (c.getParameters()[i]->getValue()))
                    finite = false;
            expect (finite, "invalid numeric state produced non-finite parameter");
        }

        // Non-finite values: skipped, keep defaults.
        {
            G10Processor c;
            juce::ValueTree state ("g10state");
            state.setProperty ("version", 1, nullptr);
            state.setProperty ("g10.band31", std::numeric_limits<float>::infinity(), nullptr);
            state.setProperty ("g10.band63", std::numeric_limits<float>::quiet_NaN(), nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            state.writeToStream (stream);
            loadState (c, block);
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band31")->getUnitsValue(), 0.0f, 1.0e-3f, "inf skipped");
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band63")->getUnitsValue(), 0.0f, 1.0e-3f, "nan skipped");
        }

        // Truncated binary data: no crash.
        {
            G10Processor c;
            juce::MemoryBlock truncated (saved.getData(), saved.getSize() / 2);
            loadState (c, truncated);
            expect (true, "truncated state did not crash");
        }

        // Random malformed binary state: no crash.
        {
            G10Processor c;
            juce::MemoryBlock garbage (256);
            for (int i = 0; i < 256; ++i)
                garbage[i] = (char) (0xAB + (i % 7));
            loadState (c, garbage);
            expect (true, "malformed state did not crash");
        }

        // Repeated state loading: stable, no crash, no corruption.
        {
            G10Processor c;
            setDb (G10Test::findParam (c, "g10.band2k"), 4.0f);
            for (int i = 0; i < 5; ++i)
                loadState (c, saved);
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band31")->getUnitsValue(), 6.0f, 1.0e-3f, "repeated load band31");
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band2k")->getUnitsValue(), 0.0f, 1.0e-3f, "repeated load overwrote unrelated param");
        }

        // No unrelated parameter corruption: a state that sets only band63
        // must leave band31 untouched.
        {
            G10Processor c;
            setDb (G10Test::findParam (c, "g10.band31"), 6.0f);
            juce::ValueTree state ("g10state");
            state.setProperty ("version", 1, nullptr);
            state.setProperty ("g10.band63", 0.75f, nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            state.writeToStream (stream);
            loadState (c, block);
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band31")->getUnitsValue(), 6.0f, 1.0e-3f, "unrelated param corrupted");
            expectWithinAbsoluteError (G10Test::findParam (c, "g10.band63")->getUnitsValue(), 6.0f, 1.0e-3f, "target param restored");
        }
    }

    // ------------------------------------------------------------------

    void testCanonicalIdentity()
    {
        beginTest ("Canonical identity: legacy analog/quality values are inert");

        const double rate = 48000.0;
        const int block = 512;

        // The canonical path IS the APEX color: a processor with the legacy
        // defaults (analog=0, quality=0) must still run the analog chain, so
        // its output must DIFFER from the frozen clean engine (the color is
        // active even though the legacy parameter says OFF).
        auto canonical = G10Test::makePreparedProcessor (rate, block);
        setDb (G10Test::findParam (*canonical, "g10.band31"), 6.0f);

        G10CurveEngineCore engine;
        engine.prepare (rate, block, 2);
        engine.setBandTargetGainDb (0, 6.0f);

        G10Test::settleProcessor (*canonical, rate, block, 0.5);
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
        for (int pos = 0; pos < 8192; pos += block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                            * 440.0 * (pos + i) / rate));
                    bufA.setSample (ch, i, v);
                    bufB.setSample (ch, i, v);
                }
            canonical->processBlock (bufA, midi);
            float* chans[2] = { bufB.getWritePointer (0), bufB.getWritePointer (1) };
            engine.processBlock (chans, 2, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (bufA.getSample (ch, i) != bufB.getSample (ch, i))
                        differs = true;
        }
        expect (differs, "canonical path must include the APEX color (legacy analog=0 is inert)");

        // Legacy values are inert: analog=1/quality=1 must be bit-exact to
        // the legacy defaults (both run the same canonical path). A FRESH
        // canonical processor is used for this comparison: the D1B DC
        // blocker (2 Hz, tau ~79.6 ms) retains state across blocks, so the
        // `canonical` instance used above carries history that a 0.5 s
        // settle cannot fully erase. Both fresh processors are settled
        // identically, so the comparison isolates the parameter policy.
        auto canonical2 = G10Test::makePreparedProcessor (rate, block);
        setDb (G10Test::findParam (*canonical2, "g10.band31"), 6.0f);
        auto legacyOn = G10Test::makePreparedProcessor (rate, block);
        setDb (G10Test::findParam (*legacyOn, "g10.band31"), 6.0f);
        G10Test::findParam (*legacyOn, "g10.analog")->setValue (1.0f);
        G10Test::findParam (*legacyOn, "g10.quality")->setValue (1.0f);

        G10Test::settleProcessor (*canonical2, rate, block, 0.5);
        G10Test::settleProcessor (*legacyOn, rate, block, 0.5);

        bool identical = true;
        for (int pos = 0; pos < 8192; pos += block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                            * 440.0 * (pos + i) / rate));
                    bufA.setSample (ch, i, v);
                    bufB.setSample (ch, i, v);
                }
            canonical2->processBlock (bufA, midi);
            legacyOn->processBlock (bufB, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (bufA.getSample (ch, i) != bufB.getSample (ch, i))
                        identical = false;
        }
        expect (identical, "legacy analog/quality values must not change the sound");

        // They persist through state round-trip.
        const auto saved = saveState (*legacyOn);
        G10Processor restored;
        loadState (restored, saved);
        expect (G10Test::findParam (restored, "g10.analog")->getBool(), "analog persisted");
        expect (G10Test::findParam (restored, "g10.quality")->getBool(), "quality persisted");
    }
    void testInputOutputMeters()
    {
        beginTest ("Input/Output level meters follow the real signal");

        const double rate = 48000.0;
        const int block = 512;

        // ---- Silence: both meters at the floor -----------------------------
        {
            G10Processor proc;
            proc.prepareToPlay (rate, block);
            juce::AudioBuffer<float> buffer (2, block);
            buffer.clear();
            juce::MidiBuffer midi;
            proc.processBlock (buffer, midi);
            expect (proc.getInputMeterDb() <= -80.0f, "input meter silent floor");
            expect (proc.getOutputMeterDb() <= -80.0f, "output meter silent floor");
        }

        auto render = [&] (G10Processor& proc, float amp, int blocks = 8)
        {
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            double phase = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();
                for (int s = 0; s < block; ++s)
                {
                    const float v = amp * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 1000.0 / rate;
                    buffer.setSample (0, s, v);
                    buffer.setSample (1, s, v);
                }
                proc.processBlock (buffer, midi);
            }
        };

        // ---- -20 dBFS sine, trims 0, bands flat ----------------------------
        {
            G10Processor proc;
            proc.prepareToPlay (rate, block);
            render (proc, 0.1f);
            expectWithinAbsoluteError (proc.getInputMeterDb(), -20.0f, 0.5f,
                "input meter = input level (trim 0)");
            expectWithinAbsoluteError (proc.getOutputMeterDb(), -20.0f, 1.0f,
                "output meter = final output level (trims 0, flat bands)");
        }

        // ---- Output trim +6 dB: output meter rises, input unaffected --------
        {
            G10Processor proc;
            proc.prepareToPlay (rate, block);
            render (proc, 0.1f);
            G10Test::findParam (proc, "g10.output")->setValue (
                G10Test::findParam (proc, "g10.output")->getValueForText ("6"));
            render (proc, 0.1f);
            expectWithinAbsoluteError (proc.getOutputMeterDb(), -14.0f, 1.0f,
                "output meter follows the output trim");
            expectWithinAbsoluteError (proc.getInputMeterDb(), -20.0f, 0.5f,
                "input meter unaffected by the output trim");
        }

        // ---- Input trim +12 dB: input meter shows the level entering
        // internal processing ------------------------------------------------
        {
            G10Processor proc;
            proc.prepareToPlay (rate, block);
            render (proc, 0.1f);
            G10Test::findParam (proc, "g10.input")->setValue (
                G10Test::findParam (proc, "g10.input")->getValueForText ("12"));
            render (proc, 0.1f);
            expectWithinAbsoluteError (proc.getInputMeterDb(), -8.0f, 0.5f,
                "input meter follows the input trim (level into processing)");
        }

        // ---- prepareToPlay resets the meters to the floor -------------------
        {
            G10Processor proc;
            proc.prepareToPlay (rate, block);
            render (proc, 0.1f);
            proc.prepareToPlay (rate, block);
            expect (proc.getInputMeterDb() <= -80.0f,
                    "prepare resets the input meter");
            expect (proc.getOutputMeterDb() <= -80.0f,
                    "prepare resets the output meter");
        }
    }
};

static G10ProcessorTests g10ProcessorTests;
