#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <functional>

// ============================================================================
// C4RtAllocationTests — the realtime contract (spec §20, audit E):
//
//   1. steady-state processBlock performs ZERO heap allocation across every
//      required scenario (neutral, shaped, extreme, parameter target
//      changes, smoothing, trims, Bell/Shelf morph, HPF/LPF changes, bypass
//      transitions) at every required block size;
//   2. every DSP stage honors the ACTIVE numSamples: samples beyond the
//      active range of a small block inside a larger prepared capacity are
//      never read, written, or blended (sentinel-proof).
//
// The allocation checker is scoped exactly around processBlock + plain
// float parameter stores (the audio-thread contract). All processors,
// buffers and containers are constructed and prepared BEFORE the checker.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4Parameter;
using APEX::C4::C4ParamIndex;

class C4RtAllocationTests final : public juce::UnitTest
{
public:
    C4RtAllocationTests() : juce::UnitTest ("C4.RtAllocation", "APEX.C4") {}

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
            testSmoothing (block);
            testTrimSmoothing (block);
            testModeMorph (block);
            testFilterChanges (block);
            testBypassTransitions (block);
        }

        testActiveRangeOnly();
    }

private:
    struct Fixture
    {
        std::unique_ptr<C4Processor> proc;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;

        Fixture (int numChannels, int blockSize, double rate = 48000.0)
            : buffer (numChannels, blockSize)
        {
            proc = C4Test::makePreparedProcessor (rate, blockSize);
        }
    };

    static void setDb (C4Parameter* p, float db)
    {
        p->setValue (p->getValueForText (juce::String (db, 1)));
    }

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

    // ---- Scenarios --------------------------------------------------------

    void testNeutralMono (int block)
    {
        beginTest ("Allocation-free: mono neutral, block " + juce::String (block));
        Fixture f (1, block);
        C4Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        runMeasured (f, 2000, [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testNeutralStereo (int block)
    {
        beginTest ("Allocation-free: stereo neutral, block " + juce::String (block));
        Fixture f (2, block);
        C4Test::fillDeterministic (f.buffer, 0xA9E12026u);
        warmUp (f);
        runMeasured (f, 2000, [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testTypicalShaping (int block)
    {
        beginTest ("Allocation-free: typical shaping, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kWeightGain), 3.0f);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kSculptGain), 2.0f);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kBiteGain), 2.0f);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kOpenGain), 3.0f);
        C4Test::fillDeterministic (f.buffer, 0xABCDEF01u);
        warmUp (f);
        runMeasured (f, 2000, [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testAllBands (int block)
    {
        beginTest ("Allocation-free: all bands active, block " + juce::String (block));
        Fixture f (2, block);
        for (int idx : { C4ParamIndex::kWeightGain, C4ParamIndex::kSculptGain,
                         C4ParamIndex::kBiteGain, C4ParamIndex::kOpenGain })
            setDb (f.proc->getC4Parameter (idx), 6.0f);
        f.proc->getC4Parameter (C4ParamIndex::kWeightMode)->setValue (1.0f);
        f.proc->getC4Parameter (C4ParamIndex::kOpenMode)->setValue (1.0f);
        C4Test::fillDeterministic (f.buffer, 0x10203040u);
        warmUp (f);
        runMeasured (f, 2000, [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testExtreme (int block)
    {
        beginTest ("Allocation-free: extreme settings, block " + juce::String (block));
        Fixture f (2, block);
        for (int idx : { C4ParamIndex::kWeightGain, C4ParamIndex::kSculptGain,
                         C4ParamIndex::kBiteGain, C4ParamIndex::kOpenGain })
            setDb (f.proc->getC4Parameter (idx), 15.0f);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kInput), 18.0f);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kOutput), 18.0f);
        f.proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
            f.proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("20"));
        f.proc->getC4Parameter (C4ParamIndex::kLpf)->setValue (
            f.proc->getC4Parameter (C4ParamIndex::kLpf)->getValueForText ("24000"));
        f.proc->getC4Parameter (C4ParamIndex::kBloom)->setValue (1.0f);
        C4Test::fillDeterministic (f.buffer, 0xF0F0F0F0u);
        warmUp (f);
        runMeasured (f, 2000, [] (Fixture& fx, int) { fx.proc->processBlock (fx.buffer, fx.midi); });
    }

    void testParamTargetChanges (int block)
    {
        beginTest ("Allocation-free: per-block parameter changes, block " + juce::String (block));
        Fixture f (2, block);
        C4Test::fillDeterministic (f.buffer, 0x31415926u);
        warmUp (f);
        runMeasured (f, 2000, [this] (Fixture& fx, int b)
        {
            // Plain float stores + processBlock only (audio-thread contract).
            auto* gain = fx.proc->getC4Parameter (C4ParamIndex::kSculptGain);
            gain->setValue (b % 2 == 0 ? 1.0f : 0.0f);
            fx.proc->processBlock (fx.buffer, fx.midi);
        });
    }

    void testSmoothing (int block)
    {
        beginTest ("Allocation-free: frequency/Q sweeps, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kSculptGain), 8.0f);
        C4Test::fillDeterministic (f.buffer, 0x22222222u);
        warmUp (f);
        runMeasured (f, 2000, [this] (Fixture& fx, int b)
        {
            auto* freq = fx.proc->getC4Parameter (C4ParamIndex::kSculptFreq);
            auto* q = fx.proc->getC4Parameter (C4ParamIndex::kBiteQ);
            freq->setValue ((b % 100) / 99.0f);
            q->setValue ((b % 50) / 49.0f);
            fx.proc->processBlock (fx.buffer, fx.midi);
        });
    }

    void testTrimSmoothing (int block)
    {
        beginTest ("Allocation-free: trim sweeps, block " + juce::String (block));
        Fixture f (2, block);
        C4Test::fillDeterministic (f.buffer, 0x33333333u);
        warmUp (f);
        runMeasured (f, 2000, [this] (Fixture& fx, int b)
        {
            auto* in = fx.proc->getC4Parameter (C4ParamIndex::kInput);
            in->setValue ((b % 100) / 99.0f);
            fx.proc->processBlock (fx.buffer, fx.midi);
        });
    }

    void testModeMorph (int block)
    {
        beginTest ("Allocation-free: Bell/Shelf morph, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kWeightGain), 6.0f);
        C4Test::fillDeterministic (f.buffer, 0x44444444u);
        warmUp (f);
        runMeasured (f, 2000, [this] (Fixture& fx, int b)
        {
            auto* mode = fx.proc->getC4Parameter (C4ParamIndex::kWeightMode);
            mode->setValue ((b % 60) < 30 ? 1.0f : 0.0f);
            fx.proc->processBlock (fx.buffer, fx.midi);
        });
    }

    void testFilterChanges (int block)
    {
        beginTest ("Allocation-free: HPF/LPF changes, block " + juce::String (block));
        Fixture f (2, block);
        C4Test::fillDeterministic (f.buffer, 0x55555555u);
        warmUp (f);

        // Precompute normalized targets OUTSIDE the measured scope (text
        // parsing is host machinery, not the audio-thread contract).
        auto* hpf = f.proc->getC4Parameter (C4ParamIndex::kHpf);
        auto* lpf = f.proc->getC4Parameter (C4ParamIndex::kLpf);
        const float hpfOnNorm = hpf->getValueForText ("200");
        const float lpfOnNorm = lpf->getValueForText ("10000");

        runMeasured (f, 2000, [this, hpf, lpf, hpfOnNorm, lpfOnNorm] (Fixture& fx, int b)
        {
            hpf->setValue ((b % 80) < 40 ? hpfOnNorm : 0.0f);
            lpf->setValue ((b % 80) >= 40 ? lpfOnNorm : 0.0f);
            fx.proc->processBlock (fx.buffer, fx.midi);
        });
    }

    void testBypassTransitions (int block)
    {
        beginTest ("Allocation-free: bypass transitions, block " + juce::String (block));
        Fixture f (2, block);
        setDb (f.proc->getC4Parameter (C4ParamIndex::kBiteGain), 9.0f);
        C4Test::fillDeterministic (f.buffer, 0x66666666u);
        warmUp (f);
        runMeasured (f, 2000, [this] (Fixture& fx, int b)
        {
            auto* bypass = fx.proc->getC4Parameter (C4ParamIndex::kBypass);
            bypass->setValue ((b % 40) < 20 ? 1.0f : 0.0f);
            fx.proc->processBlock (fx.buffer, fx.midi);
        });
    }

    // ---- Active-range proof ------------------------------------------------

    void testActiveRangeOnly()
    {
        beginTest ("Active-range proof: samples beyond numSamples are never touched");
        const double rate = 48000.0;
        const int prepared = 512;   // prepared capacity
        const int active = 100;     // active block size this call

        auto proc = C4Test::makePreparedProcessor (rate, prepared);
        setDb (proc->getC4Parameter (C4ParamIndex::kWeightGain), 6.0f);
        setDb (proc->getC4Parameter (C4ParamIndex::kBiteGain), -6.0f);
        proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
            proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("80"));

        // Warm up at the FULL capacity first (crossfades/smoothers settle).
        juce::AudioBuffer<float> warm (2, prepared);
        C4Test::fillDeterministic (warm, 0x7777u);
        juce::MidiBuffer midi;
        for (int b = 0; b < 200; ++b)
            proc->processBlock (warm, midi);

        // Now process a SMALL block with sentinels beyond the active range.
        // The processor must only ever read/write samples [0, active): pass a
        // VIEW whose numSamples is exactly `active` (a full 512-sample buffer
        // legitimately processes all 512 samples — the active range is defined
        // by numSamples, not by the allocation).
        juce::AudioBuffer<float> backing (2, prepared);
        backing.clear();
        for (int i = active; i < prepared; ++i)
        {
            backing.setSample (0, i, 123.5f);
            backing.setSample (1, i, -77.25f);
        }
        const auto sentinel = backing;

        juce::AudioBuffer<float> small (backing.getArrayOfWritePointers(), 2, 0, active);

        // Leave a bypass crossfade mid-flight so the blend path also runs.
        proc->getC4Parameter (C4ParamIndex::kBypass)->setValue (1.0f);
        proc->processBlock (small, midi);          // blend active: dry copy path
        proc->getC4Parameter (C4ParamIndex::kBypass)->setValue (0.0f);
        proc->processBlock (small, midi);          // engine path

        bool sentinelsIntact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = active; i < prepared; ++i)
                if (backing.getSample (ch, i) != sentinel.getSample (ch, i))
                    sentinelsIntact = false;

        expect (sentinelsIntact,
                "samples beyond the active numSamples must never be read, written or blended");
    }
};

static C4RtAllocationTests c4RtAllocationTests;
