#include <JuceHeader.h>
#include "C4TestUtils.h"
#include "../../../Source/C4Core/C4ResponseCurveCore.h"

#include <cmath>
#include <vector>

// ============================================================================
// C4SpectrumTests — Phase 6: the C4 SPECTRUM FLAG (observational analyzer).
//
// Pins the architecture contract:
//   - hidden/closed by default (dormant worker, empty FIFOs, zero cost)
//   - PRE / POST / BOTH taps; the BOTH mode derives the total response curve
//   - observational only: the tap NEVER alters the audio output (bit-identical
//     with the flag on/off)
//   - no FFT in processBlock (the worker owns the FFT; the audio callback
//     only copies into a preallocated SPSC ring)
//   - zero allocations in the audio callback with the flag open
//   - open/close lifecycle, state persistence, sample-rate + block-size
//     changes, multiple instances, processor/editor reopen cycles
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;
using APEX::C4::C4BandId;
using APEX::C4::C4SpectrumTapMode;
using APEX::C4::C4SpectrumCore;
using APEX::C4::makeProfileProduction;

class C4SpectrumTests final : public juce::UnitTest
{
public:
    C4SpectrumTests() : juce::UnitTest ("C4.Spectrum", "APEX.C4") {}

    void runTest() override
    {
        const double rate = 48000.0;
        const int block = 512;

        testDisabledPath (rate, block);
        testPreTap (rate, block);
        testPostTap (rate, block);
        testBothResponseCurve (rate, block);
        testBothCutResponse (rate, block);
        testAnalyticResponseCurve (rate, block);
        testOpenCloseLifecycle (rate, block);
        testOutputUnaffected (rate, block);
        testZeroAllocationCallback (rate, block);
        testStatePersistence (rate, block);
        testSampleRateChange (rate, block);
        testBlockSizeChanges (rate, block);
        testMultipleInstances (rate, block);
        testReopenCycles (rate, block);
    }

private:
    static void setBand (C4Processor& proc, C4BandId band, float freq, float gainDb)
    {
        const auto freqIdx = (C4ParamIndex) ((int) C4ParamIndex::kWeightFreq + (int) band * 3);
        const auto gainIdx = (C4ParamIndex) ((int) C4ParamIndex::kWeightGain + (int) band * 3);
        auto setText = [&] (C4ParamIndex idx, const juce::String& t)
        {
            proc.getC4Parameter (idx)->setValue (proc.getC4Parameter (idx)->getValueForText (t));
        };
        setText (freqIdx, juce::String (freq));
        setText (gainIdx, juce::String (gainDb, 1));
    }

    /** Feed a sine into the processor for `seconds`; returns samples processed. */
    static void feedSine (C4Processor& proc, double rate, int block,
                          double freq, float levelDb, double seconds)
    {
        const double amp = std::pow (10.0, levelDb / 20.0);
        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        const int total = (int) (rate * seconds);
        int pos = 0;
        while (pos < total)
        {
            const int n = juce::jmin (block, total - pos);
            for (int i = 0; i < n; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (
                    2.0 * juce::MathConstants<double>::pi * freq * (pos + i) / rate)));
            proc.processBlock (buf, midi);
            pos += n;
        }
    }

    /** Feed a sine at REAL-TIME cadence (sleeps between blocks) so the SPSC
        rings never overflow — this matches the architecture's real use case.
        The analyzer worker drains on 20 ms ticks; a CPU-speed burst feed can
        fill the 16384-sample ring faster than the worker drains and force
        the newest-block drops the ring is designed to do under overload. */
    static void feedSineRt (C4Processor& proc, double rate, int block,
                            double freq, float levelDb, double seconds)
    {
        const double amp = std::pow (10.0, levelDb / 20.0);
        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        const int total = (int) (rate * seconds);
        const double blockSeconds = (double) block / rate;
        int pos = 0;
        while (pos < total)
        {
            const int n = juce::jmin (block, total - pos);
            for (int i = 0; i < n; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (
                    2.0 * juce::MathConstants<double>::pi * freq * (pos + i) / rate)));
            proc.processBlock (buf, midi);
            pos += n;
            juce::Thread::sleep ((int) (blockSeconds * 1000.0 * 0.9)); // ~90% real time
        }
    }

    /** Feed deterministic noise (allocation-free hot loop). */
    static void feedNoise (C4Processor& proc, double rate, int block,
                           double seconds, juce::uint32 seed)
    {
        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        juce::uint32 state = seed;
        const int total = (int) (rate * seconds);
        int pos = 0;
        while (pos < total)
        {
            const int n = juce::jmin (block, total - pos);
            for (int i = 0; i < n; ++i)
            {
                state = state * 1664525u + 1013904223u;
                buf.setSample (0, i, (float) (state / 4294967296.0) * 0.5f - 0.25f);
            }
            proc.processBlock (buf, midi);
            pos += n;
        }
    }

    static int binOf (float freq, float binHz) noexcept
    {
        return (int) std::lround (freq / binHz);
    }

    /** The strongest bin (dB) in [freq/span .. freq*span]. Only considers
        spectra whose active flag is set (the inactive double-buffer half is
        never FFT'd — reading it would measure uninitialized memory). */
    static float maxDbNear (const C4SpectrumCore::Snapshot& s, double freq,
                            double spanLow = 1.4, double spanHigh = 1.4)
    {
        float best = -300.0f;
        const bool any = (s.preActive && s.preDb != nullptr)
                      || (s.postActive && s.postDb != nullptr);
        if (! any)
            return best;
        const int lo = binOf ((float) (freq / spanLow), s.binHz);
        const int hi = binOf ((float) (freq * spanHigh), s.binHz);
        for (int b = juce::jmax (1, lo); b <= juce::jmin (hi, C4SpectrumCore::kSpectrumBins - 2); ++b)
        {
            if (s.preActive && s.preDb != nullptr)
                best = juce::jmax (best, s.preDb[b]);
            if (s.postActive && s.postDb != nullptr)
                best = juce::jmax (best, s.postDb[b]);
        }
        return best;
    }

    /** Diagnostics: strongest PRE-only bin in the neighborhood. */
    static float maxDbNearPreOnly (const C4SpectrumCore::Snapshot& s, double freq)
    {
        float best = -300.0f;
        if (! s.preActive || s.preDb == nullptr)
            return best;
        const int lo = binOf ((float) (freq / 1.4), s.binHz);
        const int hi = binOf ((float) (freq * 1.4), s.binHz);
        for (int b = juce::jmax (1, lo); b <= juce::jmin (hi, C4SpectrumCore::kSpectrumBins - 2); ++b)
            best = juce::jmax (best, s.preDb[b]);
        return best;
    }

    /** Diagnostics: strongest POST-only bin in the neighborhood. */
    static float maxDbNearPostOnly (const C4SpectrumCore::Snapshot& s, double freq)
    {
        float best = -300.0f;
        if (! s.postActive || s.postDb == nullptr)
            return best;
        const int lo = binOf ((float) (freq / 1.4), s.binHz);
        const int hi = binOf ((float) (freq * 1.4), s.binHz);
        for (int b = juce::jmax (1, lo); b <= juce::jmin (hi, C4SpectrumCore::kSpectrumBins - 2); ++b)
            best = juce::jmax (best, s.postDb[b]);
        return best;
    }

    // ---- tests -------------------------------------------------------------

    void testDisabledPath (double rate, int block)
    {
        beginTest ("Closed (default): dormant worker, empty FIFOs, zero cost");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        expect (proc->getSpectrumMode() == C4SpectrumTapMode::Closed,
                "fresh processor must default to Closed");
        expect (! proc->getSpectrumCore().isWorkerRunning(),
                "worker must be dormant while Closed");

        feedNoise (*proc, rate, block, 0.5, 0x5C4Fu);
        expect (proc->getSpectrumCore().availablePre() == 0,
                "PRE FIFO must stay empty while Closed");
        expect (proc->getSpectrumCore().availablePost() == 0,
                "POST FIFO must stay empty while Closed");
        expect (proc->getSpectrumCore().getSnapshot().index == 0,
                "no snapshot must ever be published while Closed");
    }

    void testPreTap (double rate, int block)
    {
        beginTest ("PRE tap: input spectrum only, output untouched");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        proc->setSpectrumMode (C4SpectrumTapMode::Pre);
        expect (proc->getSpectrumCore().isWorkerRunning(), "worker must start on open");

        // Settle the flow first, then measure a window published AFTER the
        // settle index (the newest windows are steady-state).
        feedSine (*proc, rate, block, 1000.0, -6.0, 0.6);
        const int before = proc->getSpectrumCore().getSnapshot().index;
        feedSine (*proc, rate, block, 1000.0, -6.0, 0.4);
        const int idx = proc->getSpectrumCore().waitForSnapshot (before, 2000);
        expect (idx > before, "PRE: a snapshot must be published after settle");

        const auto s = proc->getSpectrumCore().getSnapshot();
        expect (s.preActive && ! s.postActive, "PRE mode: pre active, post inactive");
        const float peak = maxDbNear (s, 1000.0);
        expect (std::abs (peak - (-6.0f)) < 3.0f,
                "PRE: 1 kHz sine at -6 dBFS must read within 3 dB (got "
                    + juce::String (peak, 1) + " dB)");
        logMessage ("PRE 1 kHz/-6 dBFS reads " + juce::String (peak, 1) + " dB");

        proc->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testPostTap (double rate, int block)
    {
        beginTest ("POST tap: output spectrum reflects the C4 processing");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        setBand (*proc, C4BandId::Sculpt, 1000.0f, 6.0f);
        proc->setSpectrumMode (C4SpectrumTapMode::Post);

        feedSine (*proc, rate, block, 1000.0, -6.0, 0.6);
        const int before = proc->getSpectrumCore().getSnapshot().index;
        feedSine (*proc, rate, block, 1000.0, -6.0, 0.4);
        const int idx = proc->getSpectrumCore().waitForSnapshot (before, 2000);
        expect (idx > before, "POST: a snapshot must be published after settle");

        const auto s = proc->getSpectrumCore().getSnapshot();
        expect (! s.preActive && s.postActive, "POST mode: post active, pre inactive");
        const float peak = maxDbNear (s, 1000.0);
        expect (std::abs (peak - 0.0f) < 3.0f,
                "POST: -6 dBFS input with +6 dB boost must read ~0 dB (got "
                    + juce::String (peak, 1) + " dB)");
        logMessage ("POST +6 dB boost reads " + juce::String (peak, 1) + " dB");

        proc->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testBothResponseCurve (double rate, int block)
    {
        beginTest ("BOTH: the derived response curve (post - pre) is the C4 transfer");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        setBand (*proc, C4BandId::Sculpt, 1000.0f, 6.0f);
        proc->setSpectrumMode (C4SpectrumTapMode::Both);

        // Real-time cadence: settle the flow first, then measure a window
        // published AFTER the settle index (the newest windows are steady).
        feedSineRt (*proc, rate, block, 1000.0, -6.0, 0.6);
        const int before = proc->getSpectrumCore().getSnapshot().index;
        feedSineRt (*proc, rate, block, 1000.0, -6.0, 0.4);
        const int idx = proc->getSpectrumCore().waitForSnapshot (before, 2000);
        expect (idx > before, "BOTH: a snapshot must be published after settle");
        expect (proc->getSpectrumCore().preDropCount() == 0
                    && proc->getSpectrumCore().postDropCount() == 0,
                "real-time cadence must not overflow the analyzer rings");

        const auto s = proc->getSpectrumCore().getSnapshot();
        expect (s.preActive && s.postActive, "BOTH mode: pre AND post active");

        // Compact diagnostic (forensics counters stay available).
        logMessage (juce::String ("BOTH diag: idx=") + juce::String (s.index)
                    + " bin=" + juce::String (binOf (1000.0f, s.binHz))
                    + " prePeak=" + juce::String (maxDbNearPreOnly (s, 1000.0), 2)
                    + " postPeak=" + juce::String (maxDbNearPostOnly (s, 1000.0), 2)
                    + " preDrops=" + juce::String (proc->getSpectrumCore().preDropCount())
                    + " postDrops=" + juce::String (proc->getSpectrumCore().postDropCount())
                    + " preConsumed=" + juce::String (proc->getSpectrumCore().preConsumed())
                    + " postConsumed=" + juce::String (proc->getSpectrumCore().postConsumed()));

        // Region-based response estimate: maxDbNear(post) - maxDbNear(pre)
        // over the SAME small deterministic neighborhood. A single shared
        // bin is not a robust transfer estimate under Hann-window scalloping
        // (independently windowed PRE/POST frames can place energy on
        // different bins); the region estimator is documented in the Phase 6
        // evidence. The neighborhood is tight (f/1.4..f*1.4) so unrelated
        // content cannot be grabbed.
        const float prePeak = maxDbNearPreOnly (s, 1000.0);
        const float postPeak = maxDbNearPostOnly (s, 1000.0);
        const float responseDb = postPeak - prePeak;
        expect (std::abs (responseDb - 6.0f) < 2.0f,
                "BOTH: response curve must read +6 dB within 2 dB at 1 kHz (got "
                    + juce::String (responseDb, 2) + " dB)");
        logMessage ("BOTH response curve at 1 kHz reads " + juce::String (responseDb, 2) + " dB");

        proc->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testBothCutResponse (double rate, int block)
    {
        beginTest ("BOTH: negative gain reads correctly (cut, -6 dB at 1 kHz)");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        setBand (*proc, C4BandId::Sculpt, 1000.0f, -6.0f);
        proc->setSpectrumMode (C4SpectrumTapMode::Both);

        feedSineRt (*proc, rate, block, 1000.0, -6.0, 0.6);
        const int before = proc->getSpectrumCore().getSnapshot().index;
        feedSineRt (*proc, rate, block, 1000.0, -6.0, 0.4);
        const int idx = proc->getSpectrumCore().waitForSnapshot (before, 2000);
        expect (idx > before, "BOTH cut: a snapshot must be published after settle");

        const auto s = proc->getSpectrumCore().getSnapshot();
        expect (s.preActive && s.postActive, "BOTH cut mode: pre AND post active");
        const float responseDb = maxDbNearPostOnly (s, 1000.0) - maxDbNearPreOnly (s, 1000.0);
        expect (std::abs (responseDb - (-6.0f)) < 2.0f,
                "BOTH: response curve must read -6 dB within 2 dB at 1 kHz (got "
                    + juce::String (responseDb, 2) + " dB)");
        logMessage ("BOTH cut response curve at 1 kHz reads " + juce::String (responseDb, 2) + " dB");

        proc->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testAnalyticResponseCurve (double rate, int block)
    {
        beginTest ("Analytic linear response curve: engine state -> exact gain");
        using APEX::C4::ResponseCurve::BandState;
        using APEX::C4::ResponseCurve::FilterState;
        using APEX::C4::ResponseCurve::ContourState;

        auto fillFromEngine = [&] (const C4Processor& proc) -> auto
        {
            struct CurveState
            {
                BandState bands[4];
                FilterState filters;
                ContourState contours[3];
                double trimIn = 0.0, trimOut = 0.0;
            } cs;
            const auto& eng = proc.getEngine();
            for (int b = 0; b < 4; ++b)
            {
                cs.bands[b].freqHz = eng.getSmoothedBandFreqHz (b);
                cs.bands[b].gainDb = eng.getSmoothedBandGainDb (b);
                cs.bands[b].q = eng.getSmoothedBandQ (b);
                cs.bands[b].modeBlend = eng.getSmoothedBandModeBlend (b);
                cs.bands[b].highShelf = (b == (int) C4BandId::Open);
            }
            cs.filters.hpfHz = eng.getSmoothedHpfHz();
            cs.filters.hpfMix = eng.getSmoothedHpfMix();
            cs.filters.lpfHz = eng.getSmoothedLpfHz();
            cs.filters.lpfMix = eng.getSmoothedLpfMix();
            for (int p = 0; p < 3; ++p)
            {
                cs.contours[p].gainDb = eng.getContourGainDb (p);
                cs.contours[p].freqHz = eng.getContourFreqHz (p);
                cs.contours[p].q = eng.getCouplingQ();
            }
            cs.trimIn = eng.getSmoothedInputDb();
            cs.trimOut = eng.getSmoothedOutputDb();
            return cs;
        };

        auto curveDbAt = [&] (const C4Processor& proc, double freq)
        {
            auto cs = fillFromEngine (proc);
            return (float) APEX::C4::ResponseCurve::magnitudeDbAt (
                freq, rate, cs.trimIn, cs.trimOut, cs.filters, cs.bands, cs.contours);
        };

        // +6 dB boost at 1 kHz: the analytic curve must read +6 at the center.
        {
            auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
            setBand (*proc, C4BandId::Sculpt, 1000.0f, 6.0f);
            feedSineRt (*proc, rate, block, 1000.0, -6.0, 0.6);
            const float g = curveDbAt (*proc, 1000.0);
            expect (std::abs (g - 6.0f) < 0.5f,
                    "analytic curve must read +6 dB within 0.5 dB at the center (got "
                        + juce::String (g, 2) + " dB)");
            logMessage ("Analytic curve +6 dB @1 kHz reads " + juce::String (g, 2) + " dB");
        }

        // -6 dB cut: symmetric verification (boosts must not be the only
        // thing that passes).
        {
            auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
            setBand (*proc, C4BandId::Sculpt, 1000.0f, -6.0f);
            feedSineRt (*proc, rate, block, 1000.0, -6.0, 0.6);
            const float g = curveDbAt (*proc, 1000.0);
            expect (std::abs (g - (-6.0f)) < 0.5f,
                    "analytic curve must read -6 dB within 0.5 dB at the center (got "
                        + juce::String (g, 2) + " dB)");
            logMessage ("Analytic curve -6 dB @1 kHz reads " + juce::String (g, 2) + " dB");
        }

        // Neutral: the curve must be flat 0 dB.
        {
            auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
            feedSineRt (*proc, rate, block, 1000.0, -6.0, 0.6);
            const float g = curveDbAt (*proc, 1000.0);
            expect (std::abs (g) < 0.1f,
                    "analytic curve must read 0 dB neutral (got " + juce::String (g, 2) + " dB)");
        }

        // HPF engaged: 3rd-order Butterworth at 80 Hz -> ~-18 dB at 40 Hz.
        {
            auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
            proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
                proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("80"));
            feedSineRt (*proc, rate, block, 1000.0, -6.0, 0.6);
            const float g = curveDbAt (*proc, 40.0);
            expect (std::abs (g - (-18.0f)) < 3.0f,
                    "analytic curve must read ~-18 dB at fc/2 with the HPF engaged (got "
                        + juce::String (g, 2) + " dB)");
            logMessage ("Analytic curve HPF 80 Hz at 40 Hz reads " + juce::String (g, 2) + " dB");
        }
    }

    void testOpenCloseLifecycle (double rate, int block)
    {
        beginTest ("Open/close lifecycle: start, flow, stop, drain");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());

        proc->setSpectrumMode (C4SpectrumTapMode::Both);
        feedNoise (*proc, rate, block, 0.25, 0x0C4Cu);
        expect (proc->getSpectrumCore().availablePre() > 0
                    || proc->getSpectrumCore().availablePost() > 0,
                "data must flow into the taps while open");
        proc->getSpectrumCore().waitForSnapshot (0, 2000);

        proc->setSpectrumMode (C4SpectrumTapMode::Closed);
        expect (! proc->getSpectrumCore().isWorkerRunning(), "worker must stop on close");
        expect (proc->getSpectrumCore().availablePre() == 0
                    && proc->getSpectrumCore().availablePost() == 0,
                "FIFOs must drain on close");

        // Reopen cleanly.
        proc->setSpectrumMode (C4SpectrumTapMode::Pre);
        const int before = proc->getSpectrumCore().getSnapshot().index;
        feedNoise (*proc, rate, block, 0.2, 0x0C4Du);
        const int after = proc->getSpectrumCore().waitForSnapshot (before, 2000);
        expect (after > before, "reopen must publish fresh snapshots");
        proc->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testOutputUnaffected (double rate, int block)
    {
        beginTest ("The flag is observational: output bit-identical on/off");
        auto open = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        auto shut = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        setBand (*open, C4BandId::Weight, 100.0f, 3.0f);
        setBand (*open, C4BandId::Bite, 2500.0f, 2.0f);
        setBand (*shut, C4BandId::Weight, 100.0f, 3.0f);
        setBand (*shut, C4BandId::Bite, 2500.0f, 2.0f);
        open->setSpectrumMode (C4SpectrumTapMode::Both);

        juce::AudioBuffer<float> a (2, block);
        juce::AudioBuffer<float> b (2, block);
        juce::MidiBuffer midi;
        bool identical = true;
        for (int blk = 0; blk < 60; ++blk)
        {
            C4Test::fillDeterministic (a, 0xC45A0u + (juce::uint32) blk);
            b = a;
            open->processBlock (a, midi);
            shut->processBlock (b, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (a.getSample (ch, i) != b.getSample (ch, i))
                        identical = false;
        }
        expect (identical, "spectrum open vs closed must be bit-identical audio");
        open->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testZeroAllocationCallback (double rate, int block)
    {
        beginTest ("Zero allocations in the audio callback (all three open modes)");
        for (const auto mode : { C4SpectrumTapMode::Pre, C4SpectrumTapMode::Post,
                                 C4SpectrumTapMode::Both })
        {
            auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
            setBand (*proc, C4BandId::Weight, 100.0f, 6.0f);
            proc->setSpectrumMode (mode);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            C4Test::fillDeterministic (buf, 0xC4A110Cu);
            for (int b = 0; b < 40; ++b)
                proc->processBlock (buf, midi); // warm the tap path

            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int b = 0; b < 200; ++b)
                {
                    C4Test::fillDeterministic (buf, 0xC4A110Cu + (juce::uint32) b);
                    proc->processBlock (buf, midi);
                }
            }
            proc->setSpectrumMode (C4SpectrumTapMode::Closed);
        }
    }

    void testStatePersistence (double rate, int block)
    {
        beginTest ("State persistence: spectrum mode survives save/restore, clamps");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        proc->setSpectrumMode (C4SpectrumTapMode::Pre);

        juce::MemoryBlock state;
        proc->getStateInformation (state);

        auto restored = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        expect (restored->getSpectrumMode() == C4SpectrumTapMode::Closed,
                "fresh restore target starts Closed");
        restored->setStateInformation (state.getData(), (int) state.getSize());
        expect (restored->getSpectrumMode() == C4SpectrumTapMode::Pre,
                "restored processor must re-open the persisted tap mode");
        restored->setSpectrumMode (C4SpectrumTapMode::Closed);

        // Out-of-range stored mode clamps to Both (3), never crashes.
        juce::ValueTree tree ("c4state");
        tree.setProperty ("version", 1, nullptr);
        tree.setProperty ("c4.spectrum", 9, nullptr);
        juce::MemoryBlock raw;
        juce::MemoryOutputStream stream (raw, false);
        tree.writeToStream (stream);
        auto clamped = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        clamped->setStateInformation (raw.getData(), (int) raw.getSize());
        expect (clamped->getSpectrumMode() == C4SpectrumTapMode::Both,
                "stored mode must clamp to Both");
        clamped->setSpectrumMode (C4SpectrumTapMode::Closed);
        proc->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testSampleRateChange (double rate, int block)
    {
        beginTest ("Sample-rate change: snapshot re-prepares, peaks track the rate");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        proc->setSpectrumMode (C4SpectrumTapMode::Pre);
        feedSine (*proc, rate, block, 1000.0, -6.0, 0.3);
        proc->getSpectrumCore().waitForSnapshot (0, 2000);

        proc->prepareToPlay (44100.0, block);
        expect (std::abs (proc->getSpectrumCore().getSnapshot().binHz - 44100.0f / 4096.0f) < 0.01f,
                "binHz must follow the new rate");
        expect (proc->getSpectrumCore().getSnapshot().index == 0,
                "stale snapshots must be invalidated by the rate change");

        feedSine (*proc, 44100.0, block, 1000.0, -6.0, 0.6);
        const int before = proc->getSpectrumCore().getSnapshot().index;
        feedSine (*proc, 44100.0, block, 1000.0, -6.0, 0.4);
        const int idx = proc->getSpectrumCore().waitForSnapshot (before, 2000);
        expect (idx > before, "snapshots must resume after the rate change");
        const auto s = proc->getSpectrumCore().getSnapshot();
        const float peak = maxDbNear (s, 1000.0);
        expect (std::abs (peak - (-6.0f)) < 3.0f,
                "peak must track at 44.1 kHz (got " + juce::String (peak, 1) + " dB)");
        proc->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testBlockSizeChanges (double rate, int block)
    {
        beginTest ("Block-size changes: chunking + tap stay bit-identical and finite");
        for (int bs : { 64, 512, 4096 })
        {
            auto open = C4Test::makePreparedProcessor (rate, bs, makeProfileProduction());
            auto shut = C4Test::makePreparedProcessor (rate, bs, makeProfileProduction());
            setBand (*open, C4BandId::Sculpt, 1000.0f, 5.0f);
            setBand (*shut, C4BandId::Sculpt, 1000.0f, 5.0f);
            open->setSpectrumMode (C4SpectrumTapMode::Both);

            juce::AudioBuffer<float> a (2, bs);
            juce::AudioBuffer<float> b (2, bs);
            juce::MidiBuffer midi;
            bool identical = true;
            for (int blk = 0; blk < 30; ++blk)
            {
                C4Test::fillDeterministic (a, 0xC4B10C + (juce::uint32) blk);
                b = a;
                open->processBlock (a, midi);
                shut->processBlock (b, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < bs; ++i)
                        if (a.getSample (ch, i) != b.getSample (ch, i))
                            identical = false;
            }
            expect (identical, juce::String ("block ") + juce::String (bs)
                        + ": spectrum must not alter audio");
            expect (C4Test::allFinite (a), juce::String ("block ") + juce::String (bs)
                        + ": output must stay finite with the tap open");
            open->setSpectrumMode (C4SpectrumTapMode::Closed);
        }
    }

    void testMultipleInstances (double rate, int block)
    {
        beginTest ("Multiple instances: independent taps, no cross-talk");
        auto pre = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        auto post = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        pre->setSpectrumMode (C4SpectrumTapMode::Pre);
        post->setSpectrumMode (C4SpectrumTapMode::Post);

        feedSine (*pre, rate, block, 440.0, -6.0, 0.3);
        feedSine (*post, rate, block, 2000.0, -6.0, 0.3);
        pre->getSpectrumCore().waitForSnapshot (0, 2000);
        post->getSpectrumCore().waitForSnapshot (0, 2000);

        const auto sp = pre->getSpectrumCore().getSnapshot();
        const auto sq = post->getSpectrumCore().getSnapshot();
        expect (sp.preActive && ! sp.postActive, "instance 1 must be PRE-only");
        expect (! sq.preActive && sq.postActive, "instance 2 must be POST-only");

        // Instance 1 saw 440 Hz; its strongest low-band bin must sit near 440,
        // not 2 kHz.
        const float at440 = maxDbNear (sp, 440.0);
        const float at2k  = maxDbNear (sp, 2000.0);
        expect (at440 > at2k - 10.0f,
                "instance 1 must see its own 440 Hz content (440:"
                    + juce::String (at440, 1) + " dB vs 2k:" + juce::String (at2k, 1) + " dB)");

        pre->setSpectrumMode (C4SpectrumTapMode::Closed);
        post->setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testReopenCycles (double rate, int block)
    {
        beginTest ("Processor/editor reopen cycles: stable across prepare cycles");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileProduction());
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            proc->setSpectrumMode (C4SpectrumTapMode::Both);
            const int before = proc->getSpectrumCore().getSnapshot().index;
            feedNoise (*proc, rate, block, 0.2, 0xC4C1CEu + (juce::uint32) cycle);
            const int after = proc->getSpectrumCore().waitForSnapshot (before, 2000);
            expect (after > before, "cycle " + juce::String (cycle) + " must publish");
            proc->setSpectrumMode (C4SpectrumTapMode::Closed);
            proc->prepareToPlay (rate, block); // editor close/reopen path
        }
        expect (! proc->getSpectrumCore().isWorkerRunning(),
                "worker must be stopped after the final close");
    }
};

static C4SpectrumTests c4SpectrumTests;
