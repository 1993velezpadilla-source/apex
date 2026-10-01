#include <JuceHeader.h>
#include "G10TestUtils.h"

#include <functional>

using APEX::G10::G10Processor;
using APEX::G10::G10Parameter;

// ============================================================================
// G10RtAllocationTests — proves the realtime contract of the real G10
// processor: steady-state processBlock performs zero heap allocation.
//
// Every processor, buffer, container, and parameter reference is created and
// prepared BEFORE the juce::UnitTestAllocationChecker is constructed. The
// checker measures only the steady-state processing loop (and control-plane
// parameter writes, which are plain float stores in G10Parameter). No
// logging, no expect() with messages, no resizing, no state serialization,
// and no FFT inside the measured scope.
//
// Scope contract (verified against the production audio path):
//   - processBlock + the raw float parameter store (setValue(float)) are the
//     audio-thread contract and MUST be inside the measured scope.
//   - juce::String formatting / getValueForText parsing is HOST machinery
//     (control-plane text conversion), NOT the audio-thread contract. It is
//     therefore done OUTSIDE the checker; the normalized float targets are
//     precomputed and only the raw store is exercised inside.
//
// All nine required realtime scenarios are exercised at every required block
// size: mono neutral, stereo neutral, typical multi-band shaping, all bands
// active, extreme +/-12 dB, parameter target changes, band smoothing,
// input/output trim smoothing, and bypass transitions.
// ============================================================================

class G10RtAllocationTests final : public juce::UnitTest
{
public:
    G10RtAllocationTests() : juce::UnitTest ("G10.RtAllocation", "APEX.G10") {}

    void runTest() override
    {
        const int blockSizes[] = { 1, 32, 128, 333, 1024 };

        for (int block : blockSizes)
        {
            testNeutralMono (block);
            testNeutralStereo (block);
            testTypicalShaping (block);
            testAllBands (block);
            testExtreme (block);
            testParamTargetChanges (block);
            testBandSmoothing (block);
            testTrimSmoothing (block);
            testBypassTransitions (block);
            testCanonicalRealtime (block);
            testCanonicalOffline (block);
        }
    }

private:
    // ---------------------------------------------------------------------
    // Fixture: everything is created and prepared before any checker runs.
    // ---------------------------------------------------------------------

    struct Fixture
    {
        std::unique_ptr<G10Processor> proc;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        G10Parameter* params[APEX::G10::kNumParams] = {};

        Fixture (int numChannels, int blockSize, double rate = 48000.0,
             bool nonRealtime = false)
            : buffer (numChannels, blockSize)
        {
            // The canonical quality policy reads isNonRealtime() at
            // prepareToPlay: realtime = NORMAL (2x), offline = HQ (4x).
            proc = G10Test::makePreparedProcessor (rate, blockSize, nonRealtime);
            for (int i = 0; i < APEX::G10::kNumParams; ++i)
                params[i] = proc->getG10Parameter (i);
        }
    };

    /** Set a parameter from product units via the text path. HOST machinery
        (String formatting + parsing) — only for setup OUTSIDE the measured
        scope. Inside the checker, precompute the normalized float with
        getValueForText() first and call setValue(float) directly. */
    void setDb (G10Parameter* p, float db) const
    {
        jassert (p != nullptr);
        p->setValue (p->getValueForText (juce::String (db, 1)));
    }

    /** Run one measured steady-state loop. The checker is scoped exactly
        around the processing; perBlock runs on the same thread and may only
        perform allocation-free work (plain parameter stores + processBlock). */
    void runMeasured (Fixture& f, int numBlocks,
                      const std::function<void (Fixture&, int)>& perBlock)
    {
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int b = 0; b < numBlocks; ++b)
                perBlock (f, b);
        }
    }

    void warmUp (Fixture& f, int numBlocks = 64)
    {
        for (int b = 0; b < numBlocks; ++b)
            f.proc->processBlock (f.buffer, f.midi);
    }

    // ---------------------------------------------------------------------

    void testNeutralMono (int block)
    {
        beginTest ("Allocation-free: mono neutral, block " + juce::String (block));
        Fixture f (1, block);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        runMeasured (f, 2000,
                     [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testNeutralStereo (int block)
    {
        beginTest ("Allocation-free: stereo neutral, block " + juce::String (block));
        Fixture f (2, block);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        runMeasured (f, 2000,
                     [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testTypicalShaping (int block)
    {
        beginTest ("Allocation-free: typical multi-band shaping, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.params[APEX::G10::kBand31], 6.0f);
        setDb (f.params[APEX::G10::kBand1k], -3.0f);
        setDb (f.params[APEX::G10::kBand16k], 2.0f);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        runMeasured (f, 2000,
                     [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testAllBands (int block)
    {
        beginTest ("Allocation-free: all bands active, block " + juce::String (block));
        Fixture f (2, block);
        for (int b = 0; b < APEX::G10::kNumBands; ++b)
            setDb (f.params[APEX::G10::kBand31 + b], (b % 2) ? 4.0f : -2.0f);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        runMeasured (f, 2000,
                     [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testExtreme (int block)
    {
        beginTest ("Allocation-free: extreme +/-12 dB, block " + juce::String (block));
        Fixture f (2, block);
        for (int b = 0; b < APEX::G10::kNumBands; ++b)
            setDb (f.params[APEX::G10::kBand31 + b], (b % 2) ? 12.0f : -12.0f);
        setDb (f.params[APEX::G10::kInput], 18.0f);
        setDb (f.params[APEX::G10::kOutput], 18.0f);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        runMeasured (f, 2000,
                     [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testParamTargetChanges (int block)
    {
        beginTest ("Allocation-free: parameter target changes, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.params[APEX::G10::kBand1k], 6.0f);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);

        // Precompute the normalized target values OUTSIDE the measured
        // scope: juce::String formatting + getValueForText parsing is host
        // machinery, not the audio-thread contract. Inside the scope only
        // the raw float store (the exact mechanism processBlock consumes)
        // is exercised — the same pattern the passing bypass test uses.
        float targets[24];
        for (int i = 0; i < 24; ++i)
            targets[i] = f.params[APEX::G10::kBand1k]->getValueForText (juce::String ((float) (i - 12), 1));

        runMeasured (f, 2000,
                     [&targets] (Fixture& fx, int b)
                     {
                         fx.params[APEX::G10::kBand1k]->setValue (targets[b % 24]);
                         fx.proc->processBlock (fx.buffer, fx.midi);
                     });
    }

    void testBandSmoothing (int block)
    {
        beginTest ("Allocation-free: band smoothing, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.params[APEX::G10::kBand31], 6.0f);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        // Precomputed OUTSIDE the measured scope (host text machinery).
        const float target = f.params[APEX::G10::kBand31]->getValueForText ("6.0");
        // The smoothing still advances while the target is repeatedly
        // re-written; the coefficient recompute path must stay allocation-free.
        runMeasured (f, 2000,
                     [target] (Fixture& fx, int)
                     {
                         fx.params[APEX::G10::kBand31]->setValue (target);
                         fx.proc->processBlock (fx.buffer, fx.midi);
                     });
    }

    void testTrimSmoothing (int block)
    {
        beginTest ("Allocation-free: input/output trim smoothing, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.params[APEX::G10::kInput], 6.0f);
        setDb (f.params[APEX::G10::kOutput], -6.0f);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        // Precomputed OUTSIDE the measured scope (host text machinery).
        float inTargets[12];
        float outTargets[12];
        for (int i = 0; i < 12; ++i)
        {
            inTargets[i]  = f.params[APEX::G10::kInput]->getValueForText (juce::String ((float) (i - 6), 1));
            outTargets[i] = f.params[APEX::G10::kOutput]->getValueForText (juce::String ((float) (6 - i), 1));
        }
        runMeasured (f, 2000,
                     [&inTargets, &outTargets] (Fixture& fx, int b)
                     {
                         fx.params[APEX::G10::kInput]->setValue (inTargets[b % 12]);
                         fx.params[APEX::G10::kOutput]->setValue (outTargets[b % 12]);
                         fx.proc->processBlock (fx.buffer, fx.midi);
                     });
    }

    void testBypassTransitions (int block)
    {
        beginTest ("Allocation-free: bypass transitions, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.params[APEX::G10::kBand1k], 6.0f);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        runMeasured (f, 2000,
                     [] (Fixture& fx, int b)
                     {
                         // Toggle every 50 blocks: the per-sample equal-power
                         // crossfade runs inside the engine.
                         fx.params[APEX::G10::kBypass]->setValue ((b / 50) % 2 == 0 ? 1.0f : 0.0f);
                         fx.proc->processBlock (fx.buffer, fx.midi);
                     });
    }

    void testCanonicalRealtime (int block)
    {
        beginTest ("Allocation-free: canonical analog, realtime 2x, block " + juce::String (block));
        Fixture f (2, block);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f); // settles the canonical chains
        runMeasured (f, 2000,
                     [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testCanonicalOffline (int block)
    {
        beginTest ("Allocation-free: canonical analog, offline 4x, block " + juce::String (block));
        Fixture f (2, block, 48000.0, true);
        G10Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f); // settles the chain saturation
        runMeasured (f, 2000,
                     [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }
};

static G10RtAllocationTests g10RtAllocationTests;
