#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>
#include <cstring>
#include <vector>

// ============================================================================
// C4LifecycleTests — permanent regression for the C4 engine's lifecycle and
// the closed growing-IR defect.
//
// Historical root cause (measured, closed): JUCE's AudioSampleBuffer::clear()
// early-returns when the buffer is flagged cleared (isClear == true). The
// flag is set by clear() itself and reset ONLY by JUCE write-accessors
// (setSample / getWritePointer / getArrayOfWritePointers / ...). A DSP engine
// that writes through raw float** pointers (the correct realtime pattern)
// never resets the flag, so the NEXT flagged clear() is silently skipped and
// the engine's own previous output feeds back as input — exponential growth
// at the loop gain (measured: 132.4 tail instead of a decaying bell).
//
// The engine is correct; the harness was not. Every DIRECT-ENGINE test here
// therefore clears with C4Test::clearBufferExplicit() (unconditional
// zero-fill, immune to the flag). The suite proves:
//
//   construct -> prepare -> process
//   prepare -> reset -> process
//   repeated reset/process (deterministic, bit-identical IRs)
//   direct engine vs processor engine agreement
//   long impulse decay (the original reproducer, permanently)
//   long silence
//   long bounded deterministic input
//   mono and stereo
//   variable block sizes (block-size independent, bit-exact)
//   every supported sample rate
//   HPF disabled lifecycle (default / 1 Hz / 1500 Hz / changed while
//     disabled / previously enabled then disabled) — output bit-neutral
//   LPF disabled lifecycle — equivalent
//   Bell/Shelf repeated lifecycle transitions — finite, bounded, settle exact
//   long-run state monitor — no filter state grows without excitation
// ============================================================================

using APEX::C4::C4EngineCore;
using APEX::C4::C4TuningProfile;
using APEX::C4::makeProfileVariantB;
using APEX::C4::kNumBands;

class C4LifecycleTests final : public juce::UnitTest
{
public:
    C4LifecycleTests() : juce::UnitTest ("C4.Lifecycle", "APEX.C4") {}

    void runTest() override
    {
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

        for (double rate : rates)
        {
            beginTest ("Impulse decays through the lifecycle at " + juce::String (rate));
            {
                // construct -> prepare -> process
                auto eng = makeEngine (rate, 512);
                const float tail = runImpulseDecay (*eng, 124, 100);
                expect (std::isfinite (tail) && std::abs (tail) < 1.0e-4f,
                        "impulse must decay (growing-IR regression): tail="
                            + juce::String (tail, 8));
                expect (C4Test::allFinite (lastOutput()),
                        "output must stay finite");

                // prepare -> reset -> process (fresh deterministic behavior)
                auto engB = makeEngine (rate, 512);
                engB->reset();
                const float tailB = runImpulseDecay (*engB, 124, 100);
                expect (std::isfinite (tailB) && std::abs (tailB) < 1.0e-4f,
                        "reset must restore fresh behavior: tail="
                            + juce::String (tailB, 8));

                // repeated reset/process: identical IRs, bit-for-bit
                const auto ir1 = captureIr (*eng, rate, 512, 8192);
                eng->reset();
                applyBandSetup (*eng);
                const auto ir2 = captureIr (*eng, rate, 512, 8192);
                expect (bitEqual (ir1, ir2),
                        "IR after reset must be bit-identical to fresh IR");
            }
        }

        beginTest ("Repeated reset/process cycles stay deterministic");
        {
            auto eng = makeEngine (48000.0, 512);
            const auto ir0 = captureIr (*eng, 48000.0, 512, 8192);
            for (int cycle = 0; cycle < 5; ++cycle)
            {
                eng->reset();
                applyBandSetup (*eng);
                const auto ir = captureIr (*eng, 48000.0, 512, 8192);
                expect (bitEqual (ir0, ir),
                        "cycle " + juce::String (cycle) + " must be bit-identical");
            }
        }

        beginTest ("Direct engine vs processor engine agreement");
        {
            auto proc = C4Test::makePreparedProcessor (48000.0, 512);
            // Phase 5: the production BLOOM default is 4.0; the direct
            // engine's bloom target is 0 by construction, so the processor
            // must sit at BLOOM 0 for this agreement comparison.
            proc->getC4Parameter (APEX::C4::C4ParamIndex::kBloom)->setValue (0.0f);
            proc->getC4Parameter (APEX::C4::C4ParamIndex::kWeightGain)->setValue (
                proc->getC4Parameter (APEX::C4::C4ParamIndex::kWeightGain)->getValueForText ("6.0"));

            auto eng = makeEngine (48000.0, 512);

            juce::AudioBuffer<float> pbuf (2, 512);
            juce::AudioBuffer<float> ebuf (2, 512);
            juce::MidiBuffer midi;
            float* eChans[2] = { ebuf.getWritePointer (0), ebuf.getWritePointer (1) };

            bool imp = false;
            bool equal = true;
            int bitOnlyZeroSignDiffs = 0;
            for (int b = 0; b < 124; ++b)
            {
                C4Test::clearBufferExplicit (pbuf);
                C4Test::clearBufferExplicit (ebuf);
                if (b == 100 && ! imp)
                {
                    pbuf.setSample (0, 0, 1.0f);
                    pbuf.setSample (1, 0, 1.0f);
                    ebuf.setSample (0, 0, 1.0f);
                    ebuf.setSample (1, 0, 1.0f);
                    imp = true;
                }
                proc->processBlock (pbuf, midi);
                eng->adoptTargets (0.0f);
                eng->processBlock (eChans, 2, 512);
                for (int s = 0; s < 512; ++s)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float p = pbuf.getSample (ch, s);
                        const float e = ebuf.getSample (ch, s);
                        if (p != e)
                        {
                            equal = false;
                        }
                        else if (floatBits (p) != floatBits (e))
                        {
                            // Float-equal but bit-different: only a signed-zero
                            // canonicalization (the processor blend writes
                            // dst*1 + dry*0, which can flip -0.0 to +0.0).
                            ++bitOnlyZeroSignDiffs;
                        }
                    }
            }
            expect (equal, "processor and direct engine must agree sample-for-sample");
            logMessage ("direct-vs-processor: signed-zero-only bit diffs = "
                        + juce::String (bitOnlyZeroSignDiffs));
        }

        beginTest ("Long silence: zero output, bounded states");
        {
            auto eng = makeEngine (48000.0, 512);
            for (int b = 0; b < kNumBands; ++b)
            {
                eng->setBandGainTargetDb (b, 15.0f);
                eng->setBandFreqTargetHz (b, 20.0f);
            }
            runSilence (*eng, 48000.0, 512, 10.0);
            bool allZero = true;
            const auto& out = lastOutput();
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (out.getSample (0, i) != 0.0f)
                    allZero = false;
            expect (allZero, "silence must produce exactly zero output");
            expect (statesBounded (*eng, 1.0e-6f),
                    "all filter states must decay below 1e-6 during silence");
        }

        beginTest ("Long bounded deterministic input: finite and bounded");
        {
            auto eng = makeEngine (48000.0, 512);
            for (int b = 0; b < kNumBands; ++b)
                eng->setBandGainTargetDb (b, 15.0f);
            juce::AudioBuffer<float> buf (1, 512);
            float* chans[1] = { buf.getWritePointer (0) };
            float maxOut = 0.0f;
            bool finite = true;
            juce::uint32 seed = 0xC0FFEEu;
            const int total = (int) (48000.0 * 5.0);
            int pos = 0;
            while (pos < total)
            {
                for (int i = 0; i < 512; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    buf.setSample (0, i, 0.25f * ((float) (seed / 4294967296.0) * 2.0f - 1.0f));
                }
                eng->adoptTargets (0.0f);
                eng->processBlock (chans, 1, 512);
                finite = finite && C4Test::allFinite (buf);
                for (int i = 0; i < 512; ++i)
                    maxOut = juce::jmax (maxOut, std::abs (buf.getSample (0, i)));
                pos += 512;
            }
            expect (finite, "bounded input must stay finite");
            // Physically justified envelope: the worst case is every band's
            // gain fully in phase (|x| * SUM(A_b)); four +15 dB bands sum to
            // 22.5x. Measured 1.78 (skirt overlap of a few bands) sits well
            // inside the bound.
            float sumA = 0.0f;
            for (int b = 0; b < kNumBands; ++b)
                sumA += APEX::C4::dbToGain (15.0f);
            expect (maxOut <= 0.25f * sumA * 1.1f,
                    "bounded input must stay bounded: max |out| = "
                        + juce::String (maxOut, 4) + " (envelope "
                        + juce::String (0.25f * sumA * 1.1f, 3) + ")");
        }

        beginTest ("Variable block sizes: bit-identical output");
        {
            auto engA = makeEngine (48000.0, 512);
            auto engB = makeEngine (48000.0, 32);
            auto engC = makeEngine (48000.0, 2048);
            const auto irA = captureIr (*engA, 48000.0, 512, 8192);
            const auto irB = captureIr (*engB, 48000.0, 32, 8192);
            const auto irC = captureIr (*engC, 48000.0, 2048, 8192);
            expect (bitEqual (irA, irB) && bitEqual (irA, irC),
                    "block size must not change the output (bit-exact)");
        }

        beginTest ("Mono matches stereo-left");
        {
            auto engM = makeEngine (48000.0, 512);
            auto engS = makeEngine (48000.0, 512);
            juce::AudioBuffer<float> monoBuf (1, 512);
            juce::AudioBuffer<float> stereoBuf (2, 512);
            float* mChans[1] = { monoBuf.getWritePointer (0) };
            float* sChans[2] = { stereoBuf.getWritePointer (0), stereoBuf.getWritePointer (1) };
            juce::uint32 seed = 0x5EEDu;
            for (int b = 0; b < 100; ++b)
            {
                C4Test::clearBufferExplicit (monoBuf);
                C4Test::clearBufferExplicit (stereoBuf);
                for (int i = 0; i < 512; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    const float v = (float) (seed / 4294967296.0) * 2.0f - 1.0f;
                    monoBuf.setSample (0, i, v);
                    stereoBuf.setSample (0, i, v);
                    stereoBuf.setSample (1, i, v);
                }
                engM->adoptTargets (0.0f);
                engM->processBlock (mChans, 1, 512);
                engS->adoptTargets (0.0f);
                engS->processBlock (sChans, 2, 512);
                for (int i = 0; i < 512; ++i)
                {
                    expect (monoBuf.getSample (0, i) == stereoBuf.getSample (0, i)
                            && stereoBuf.getSample (0, i) == stereoBuf.getSample (1, i),
                            "mono must equal stereo-left at block "
                                + juce::String (b) + " sample " + juce::String (i));
                }
            }
        }

        beginTest ("HPF disabled lifecycle: output bit-neutral vs reference");
        {
            runFilterLifecycle (false);
        }

        beginTest ("LPF disabled lifecycle: output bit-neutral vs reference");
        {
            runFilterLifecycle (true);
        }

        beginTest ("Bell/Shelf repeated lifecycle transitions");
        {
            auto eng = makeEngine (48000.0, 512);
            eng->setBandFreqTargetHz (0, 100.0f);
            eng->setBandGainTargetDb (0, 6.0f);

            juce::AudioBuffer<float> buf (1, 512);
            float* chans[1] = { buf.getWritePointer (0) };
            float maxOut = 0.0f;
            bool finite = true;
            const double inPeak = 0.25;
            const int total = (int) (48000.0 * 3.0);
            int pos = 0;
            int block = 0;
            while (pos < total)
            {
                // Toggle bell/shelf every 0.2 s.
                eng->setBandModeTarget (0, ((pos / (int) (48000.0 * 0.2)) % 2) == 1);
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, (float) (inPeak * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 1000.0 * (pos + i) / 48000.0)));
                eng->adoptTargets (0.0f);
                eng->processBlock (chans, 1, 512);
                finite = finite && C4Test::allFinite (buf);
                for (int i = 0; i < 512; ++i)
                    maxOut = juce::jmax (maxOut, std::abs (buf.getSample (0, i)));
                pos += 512;
                ++block;
            }
            expect (finite, "transitions must stay finite");
            expect (maxOut <= inPeak * 2.5f + 0.01f,
                    "no click/spike during transitions: max |out| = "
                        + juce::String (maxOut, 4));

            // Settled end-state must equal a directly requested state.
            eng->setBandModeTarget (0, true); // shelf
            const auto shelfIr = captureIr (*eng, 48000.0, 512, 8192);
            auto engDirect = makeEngine (48000.0, 512);
            engDirect->setBandFreqTargetHz (0, 100.0f);
            engDirect->setBandGainTargetDb (0, 6.0f);
            engDirect->setBandModeTarget (0, true);
            const auto directIr = captureIr (*engDirect, 48000.0, 512, 8192);
            expect (bitEqual (shelfIr, directIr),
                    "settled shelf after transitions must equal direct shelf");
        }

        beginTest ("Long-run state monitor: no state grows without excitation");
        {
            auto eng = makeEngine (48000.0, 512);
            for (int b = 0; b < kNumBands; ++b)
            {
                eng->setBandGainTargetDb (b, 15.0f);
                eng->setBandFreqTargetHz (b, 20.0f);
                eng->setBandQTarget (b, 10.0f);
            }
            eng->setHpfTarget (100.0f, true);
            eng->setLpfTarget (2000.0f, true);
            // Impulse excitation, then a long decay under maximum settings.
            runImpulseDecay (*eng, (int) (48000.0 * 0.1 / 512.0), 0);
            runSilence (*eng, 48000.0, 512, 30.0);
            expect (statesBounded (*eng, 1.0e-6f),
                    "all filter states must decay below 1e-6 after 30 s silence");
        }
    }

private:
    // ---- Helpers -----------------------------------------------------------

    static juce::uint32 floatBits (float v) noexcept
    {
        juce::uint32 bits = 0;
        std::memcpy (&bits, &v, sizeof (bits));
        return bits;
    }

    static bool bitEqual (const std::vector<float>& a, const std::vector<float>& b) noexcept
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (floatBits (a[i]) != floatBits (b[i]))
                return false;
        return true;
    }

    static std::unique_ptr<C4EngineCore> makeEngine (double rate, int block)
    {
        auto eng = std::make_unique<C4EngineCore>();
        eng->prepare (rate, block, 2, makeProfileVariantB());
        applyBandSetup (*eng);
        return eng;
    }

    static void applyBandSetup (C4EngineCore& eng) noexcept
    {
        eng.setBandFreqTargetHz (0, 100.0f);
        eng.setBandGainTargetDb (0, 6.0f);
        // Mirror the processor's default adoption (HPF/LPF controls at 0 ->
        // cutoff target 1 Hz, disabled).
        eng.setHpfTarget (0.0f, false);
        eng.setLpfTarget (0.0f, false);
    }

    juce::AudioBuffer<float> outputBuf_ { 2, 2048 };

    /** Run `blocks` blocks with explicit zero-fill; impulse at block
        `impulseBlock` on channel 0 sample 0. Returns sample 511 of the last
        block (the tail). */
    float runImpulseDecay (C4EngineCore& eng, int blocks, int impulseBlock)
    {
        if (outputBuf_.getNumSamples() < 512)
            outputBuf_.setSize (2, 2048, false, false, true);
        float* chans[2] = { outputBuf_.getWritePointer (0), outputBuf_.getWritePointer (1) };
        bool imp = false;
        for (int b = 0; b < blocks; ++b)
        {
            C4Test::clearBufferExplicit (outputBuf_);
            if (b == impulseBlock && ! imp)
            {
                outputBuf_.setSample (0, 0, 1.0f);
                imp = true;
            }
            eng.adoptTargets (0.0f);
            eng.processBlock (chans, 2, 512);
        }
        return outputBuf_.getSample (0, 511);
    }

    /** Capture an impulse response with explicit zero-fill (the permanent
        direct-engine reproducer: an uncleared buffer here regrows to ~132). */
    static std::vector<float> captureIr (C4EngineCore& eng, double rate,
                                         int blockSize, int irLength)
    {
        juce::AudioBuffer<float> buf (2, blockSize);
        float* chans[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };

        // Settle smoothing.
        const int settle = (int) (rate * 1.0);
        int done = 0;
        while (done < settle)
        {
            C4Test::clearBufferExplicit (buf);
            eng.adoptTargets (0.0f);
            eng.processBlock (chans, 2, blockSize);
            done += blockSize;
        }

        std::vector<float> ir (irLength, 0.0f);
        bool impulseSent = false;
        int written = 0;
        while (written < irLength)
        {
            C4Test::clearBufferExplicit (buf);
            if (! impulseSent)
            {
                buf.setSample (0, 0, 1.0f);
                impulseSent = true;
            }
            eng.adoptTargets (0.0f);
            eng.processBlock (chans, 2, blockSize);
            const int n = juce::jmin (blockSize, irLength - written);
            for (int i = 0; i < n; ++i)
                ir[(size_t) (written + i)] = buf.getSample (0, i);
            written += n;
        }
        return ir;
    }

    void runSilence (C4EngineCore& eng, double rate, int blockSize, double seconds)
    {
        juce::AudioBuffer<float> buf (1, blockSize);
        float* chans[1] = { buf.getWritePointer (0) };
        const int total = (int) (rate * seconds);
        int done = 0;
        while (done < total)
        {
            C4Test::clearBufferExplicit (buf);
            eng.adoptTargets (0.0f);
            eng.processBlock (chans, 1, blockSize);
            done += blockSize;
        }
        // Publish the final block into the fixed 2-channel scratch (channel 0
        // only; NEVER resize outputBuf_ — runImpulseDecay relies on it being
        // 2 channels wide).
        if (outputBuf_.getNumChannels() >= 1 && outputBuf_.getNumSamples() >= blockSize)
            outputBuf_.copyFrom (0, 0, buf, 0, 0, blockSize);
    }

    const juce::AudioBuffer<float>& lastOutput() const noexcept { return outputBuf_; }

    static bool statesBounded (const C4EngineCore& eng, float bound) noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto s = eng.getBandSvfStateForTest (b, ch);
                if (std::abs (s.ic1) > bound || std::abs (s.ic2) > bound)
                    return false;
            }
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto h = eng.getHpfSvfStateForTest (ch);
            const auto l = eng.getLpfSvfStateForTest (ch);
            if (std::abs (h.ic1) > bound || std::abs (h.ic2) > bound
                || std::abs (l.ic1) > bound || std::abs (l.ic2) > bound
                || std::abs (eng.getHpfOnePoleStateForTest (ch)) > bound)
                return false;
        }
        return true;
    }

    /** HPF (isLpf == false) or LPF disabled lifecycle: every variant's output
        must be bit-neutral against a reference engine whose filters are never
        touched. Variants: default target, 1 Hz, 1500 Hz, changed while
        disabled, previously enabled then disabled (primed active, then
        crossfaded back to dry and settled). */
    void runFilterLifecycle (bool isLpf)
    {
        const double rate = 48000.0;
        const int blockSize = 512;

        auto makeVariant = [&] (const std::function<void (C4EngineCore&)>& setupBefore,
                                const std::function<void (C4EngineCore&)>& setupAfter) -> std::vector<float>
        {
            auto eng = makeEngine (rate, blockSize);
            setupBefore (*eng);
            runSilence (*eng, rate, blockSize, 0.3);  // prime with phase-1 state
            setupAfter (*eng);
            runSilence (*eng, rate, blockSize, 1.0);  // settle phase-2 state
            return captureIr (*eng, rate, blockSize, 8192);
        };

        auto noop = [] (C4EngineCore&) {};
        auto setHz = [&] (float hz, bool enabled)
        {
            return [&, hz, enabled] (C4EngineCore& e)
            {
                if (isLpf) e.setLpfTarget (hz, enabled);
                else       e.setHpfTarget (hz, enabled);
            };
        };

        // Reference: filters never touched.
        const auto reference = makeVariant (noop, noop);

        const auto checkVariant = [&] (const juce::String& name,
                                       const std::function<void (C4EngineCore&)>& before,
                                       const std::function<void (C4EngineCore&)>& after)
        {
            const auto ir = makeVariant (before, after);
            bool equal = true;
            int zeroSignOnly = 0;
            for (size_t i = 0; i < ir.size(); ++i)
            {
                if (ir[i] != reference[i])
                    equal = false;
                else if (floatBits (ir[i]) != floatBits (reference[i]))
                    ++zeroSignOnly;
            }
            expect (equal, (isLpf ? "LPF " : "HPF ") + name
                           + ": output must be bit-neutral vs reference");
            logMessage ((isLpf ? "LPF " : "HPF ") + name
                        + ": signed-zero-only diffs = " + juce::String (zeroSignOnly));
        };

        checkVariant ("default-target-disabled", noop, noop);
        checkVariant ("1Hz-disabled", setHz (1.0f, false), noop);
        checkVariant ("1500Hz-disabled", setHz (1500.0f, false), noop);
        checkVariant ("changed-while-disabled", setHz (100.0f, false), setHz (2000.0f, false));
        checkVariant ("previously-enabled-then-disabled",
                      setHz (100.0f, true), setHz (800.0f, false));
    }
};

static C4LifecycleTests c4LifecycleTests;
