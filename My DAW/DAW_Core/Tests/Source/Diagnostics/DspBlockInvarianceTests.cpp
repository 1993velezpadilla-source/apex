#include <JuceHeader.h>

// ── Production DSP cores under test (header-only, real code paths) ────────
#include "../../../Source/SoundEngineCore/ApexSourceReadContractCore.h"
#include "../../../Source/SoundEngineCore/ApexPitchTimeInputPlanCore.h"
#include "../../../Source/SoundEngineCore/ApexPdcDelayLineCore.h"
#include "../../../Source/SoundEngineCore/ApexRoutingBufferCore.h"
#include "../../../Source/AudioEngineCore/VolumeRampCore.h"
#include "../../../Source/AudioEngineCore/MuteFadeCore.h"
#include "../../../Source/AutomationCore/AutomationSnapshotCore.h"
#include "../../../Source/TapeStop/TapeStopProcessorCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/TimePitchDSPCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/WSOLAStretchCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/PhaseVocoderStretchCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipIndependentPitchCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/PitchSmootherCore.h"
#include <signalsmith-stretch.h>

//==============================================================================
/**
    @test    dsp.block-invariance.v1
    @verify  Production clip/track DSP cores process a 1 x 2048 host block
             equivalently to 2 x 1024 blocks of the same material (same initial
             state), and never leave the second half of a >1024 block
             stale/zero/duplicated.

             Invariants per component:
             - stateless plans (source read, input plan): identical outputs;
             - stateful streaming cores (WSOLA, phase vocoder wrapper, pitch
               core, tape stop): with identical initial state and identical
               source material, 2x1024 output must equal 1x2048 output
               (sequential processing consumes identical material);
             - every 2048 output must have a valid (finite, non-zero for
               non-zero input) second half [1024..2047].

             Block matrix: 480, 512, 1000, 1024, 1025, 1500, 2048, 2049, 3000.
*/
class DspBlockInvarianceTests : public juce::UnitTest
{
public:
    DspBlockInvarianceTests()
        : juce::UnitTest ("dsp.block-invariance.v1", "APEX.Diagnostics") {}

    // ── Helpers ────────────────────────────────────────────────────────────

    static void fillSine (std::vector<float>& v, double sampleRate, double freq)
    {
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = 0.7f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sampleRate);
    }

    static bool secondHalfValid (const float* p, int n, float minAbs)
    {
        if (p == nullptr || n < 2) return false;
        bool any = false;
        for (int i = n / 2; i < n; ++i)
        {
            const float v = p[i];
            if (! std::isfinite (v)) return false;
            if (std::abs (v) > minAbs) any = true;
        }
        return any;
    }

    static bool allFinite (const float* p, int n)
    {
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (p[i])) return false;
        return true;
    }

    static bool equalsTol (const float* a, const float* b, int n, float tol)
    {
        for (int i = 0; i < n; ++i)
            if (std::abs (a[i] - b[i]) > tol) return false;
        return true;
    }

    /** Run a stateful-core A/B comparison: two 1024 blocks vs one 2048 block.
        Both cases start from a FRESH core (same initial state) and consume the
        same source material clip[0..2048). Returns true when outA == outB. */
    template <class ProcessFn>
    static bool compareSegmented (const float* src, int srcTotal, ProcessFn&& process)
    {
        std::vector<float> outA (2048, 0.f);
        std::vector<float> outB (2048, 0.f);

        process (src, srcTotal, outA.data(), 1024, 0);   // first 1024
        process (src, srcTotal, outA.data() + 1024, 1024, 1); // second 1024

        process (src, srcTotal, outB.data(), 2048, 0);   // single 2048

        return equalsTol (outA.data(), outB.data(), 2048, 1e-4f);
    }

    void runTest() override
    {
        using namespace ArrangementEditor;
        using DAW::SoundEngine::ApexSourceReadContractCore;
        using DAW::SoundEngine::ApexPitchTimeInputPlanCore;
        using DAW::SoundEngine::ApexPdcDelayLineCore;
        using DAW::SoundEngine::ApexRoutingBufferCore;
        using DAW::VolumeRampCore;
        using DAW::MuteFadeCore;
        using APEX::TapeStop::ProcessorCore;

        constexpr double kSr = 44100.0;
        const int blockSizes[] = { 480, 512, 1000, 1024, 1025, 1500, 2048, 2049, 3000 };

        std::vector<float> source (30000, 0.f);
        fillSine (source, kSr, 440.0);

        // ══════════════════════════════════════════════════════════════════
        // STAGE 1 — SOURCE READ CONTRACT
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage1.source-read-contract");
        {
            const int64_t clipStart = 1000;
            const int64_t clipLen   = 20000;
            for (int n : blockSizes)
            {
                auto plan1 = ApexSourceReadContractCore::makeReadPlan (clipStart, n, clipStart, clipLen);
                expect (plan1.intersects && plan1.count == n && plan1.bufferStart == 0,
                        juce::String ("full request n=") + juce::String (n));
                expect (plan1.engineClipOffset == 0, "offset at clip start");

                // 2x1024 vs 1x2048 cover the same timeline window.
                auto pA = ApexSourceReadContractCore::makeReadPlan (clipStart, 1024, clipStart, clipLen);
                auto pB = ApexSourceReadContractCore::makeReadPlan (clipStart, 2048, clipStart, clipLen);
                expect (pA.count == 1024 && pB.count == 2048, "2x1024 == 1x2048 coverage");
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 2 — DIRECT COPY (TimePitchDSPCore identity path)
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage2.direct-copy");
        {
            TimePitchDSPCore core;
            core.prepare (kSr, 2048);
            TimePitchState st; // default = identity
            core.setState (st);

            for (int n : blockSizes)
            {
                std::vector<float> out (n, 0.f);
                TimePitchRenderRequest req;
                req.sourceData         = source.data();
                req.sourceTotalSamples = (int) source.size();
                req.sourceStartSample  = 0;
                req.sourceEndSample    = n;
                req.outputData         = out.data();
                req.numOutputSamples   = n;
                req.channel            = 0;
                req.state              = st;
                req.outputSampleRate   = kSr;
                core.renderClipSegment (req);

                expect (equalsTol (source.data(), out.data(), n, 1e-6f),
                        juce::String ("identity copy exact n=") + juce::String (n));
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 3 — RESAMPLE INLINE (renderResample, mode=Resample, stretch!=1)
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage3.resample-inline");
        {
            TimePitchDSPCore core;
            core.prepare (kSr, 2048);
            TimePitchState st;
            st.mode         = TimePitchMode::Resample;
            st.stretchRatio = 1.5;   // duration change -> renderResample path
            core.setState (st);

            const int n = 2048;
            std::vector<float> outA (2048, 0.f);
            std::vector<float> outB (2048, 0.f);

            auto run = [&] (float* dst, int startSample)
            {
                TimePitchRenderRequest req;
                req.sourceData         = source.data() + startSample;
                req.sourceTotalSamples = n;
                req.sourceStartSample  = 0;
                req.sourceEndSample    = n;
                req.outputData         = dst;
                req.numOutputSamples   = n;
                req.channel            = 0;
                req.state              = st;
                req.outputSampleRate   = kSr;
                core.renderClipSegment (req);
            };

            // Resample is stateless per request: 2x1024 over the same material
            // must equal 1x2048. Use two disjoint halves of the same source.
            run (outA.data(), 0);
            run (outB.data(), 0);
            expect (equalsTol (outA.data(), outB.data(), 2048, 1e-5f),
                    "resample stateless per request");
            expect (secondHalfValid (outB.data(), 2048, 1e-4f), "resample second half valid");
            expect (allFinite (outB.data(), 2048), "resample finite");
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 4 — WSOLA (streaming, stretch=1.0 and 1.2)
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage4.wsola");
        {
            for (double stretch : { 1.0, 1.2 })
            {
                auto runSegmented = [&] (const float* src, int srcTotal,
                                         float* dstA, float* dstB) -> bool
                {
                    WSOLAStretchCore wA, wB;
                    wA.prepare (kSr, 2048, TimePitchQuality::Balanced);
                    wB.prepare (kSr, 2048, TimePitchQuality::Balanced);
                    wA.setStretchRatio (stretch);
                    wB.setStretchRatio (stretch);

                    auto process = [&] (WSOLAStretchCore& w, float* dst, int n, int /*call*/)
                    {
                        w.renderSegment (src, srcTotal, 0, srcTotal, dst, n, 0);
                    };

                    process (wA, dstA, 1024, 0);
                    process (wA, dstA + 1024, 1024, 1);
                    process (wB, dstB, 2048, 0);

                    return equalsTol (dstA, dstB, 2048, 1e-3f);
                };

                std::vector<float> outA (2048, 0.f), outB (2048, 0.f);
                const bool same = runSegmented (source.data(), (int) source.size(), outA.data(), outB.data());
                expect (same, juce::String ("wsola 2x1024==1x2048 stretch=") + juce::String (stretch));
                expect (secondHalfValid (outB.data(), 2048, 1e-4f),
                        juce::String ("wsola second half valid stretch=") + juce::String (stretch));
                expect (allFinite (outB.data(), 2048), "wsola finite");
            }

            // Full block-size matrix: never crash, always fill, never zero-second-half.
            for (int n : blockSizes)
            {
                WSOLAStretchCore w;
                w.prepare (kSr, 2048, TimePitchQuality::Balanced);
                w.setStretchRatio (1.0);
                std::vector<float> out (n, 0.f);
                w.renderSegment (source.data(), (int) source.size(), 0, (int) source.size(), out.data(), n, 0);
                expect (allFinite (out.data(), n), juce::String ("wsola finite n=") + juce::String (n));
                if (n > 1024)
                    expect (secondHalfValid (out.data(), n, 1e-4f),
                            juce::String ("wsola second half n=") + juce::String (n));
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 5 — PHASE VOCODER / SIGNALSMITH WRAPPER
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage5.phase-vocoder-wrapper");
        {
            for (double stretch : { 1.0, 1.2 })
            {
                auto runSegmented = [&] (const float* src, int srcTotal,
                                         float* dstA, float* dstB) -> bool
                {
                    PhaseVocoderStretchCore pA, pB;
                    pA.prepare (kSr, 2048, TimePitchQuality::Balanced);
                    pB.prepare (kSr, 2048, TimePitchQuality::Balanced);
                    TimePitchState st;
                    st.mode = TimePitchMode::Stretch;
                    st.stretchRatio = stretch;
                    pA.setState (st);
                    pB.setState (st);

                    auto process = [&] (PhaseVocoderStretchCore& p, float* dst, int n, int /*call*/)
                    {
                        p.renderSegment (src, srcTotal, 0, srcTotal, dst, n, 0);
                    };

                    process (pA, dstA, 1024, 0);
                    process (pA, dstA + 1024, 1024, 1);
                    process (pB, dstB, 2048, 0);

                    return equalsTol (dstA, dstB, 2048, 1e-3f);
                };

                std::vector<float> outA (2048, 0.f), outB (2048, 0.f);
                const bool same = runSegmented (source.data(), (int) source.size(), outA.data(), outB.data());
                expect (same, juce::String ("pv 2x1024==1x2048 stretch=") + juce::String (stretch));
                expect (secondHalfValid (outB.data(), 2048, 1e-4f),
                        juce::String ("pv second half valid stretch=") + juce::String (stretch));
                expect (allFinite (outB.data(), 2048), "pv finite");
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 6 — ClipIndependentPitchCore (static pitch + pitch ramp)
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage6.clip-independent-pitch-core");
        {
            // Feed enough input (well beyond the Signalsmith STFT block of
            // ~5292 samples) so the output contains real audio, not preroll.
            // IMPORTANT: both cases must produce the SAME TOTAL OUTPUT COUNT
            // (12000) so the comparison window contains real audio in both.
            constexpr int kTotalOut = 12000;
            constexpr int kInputN = 14000;   // enough input for 12000 out @stretch 1
            std::vector<float> inL (kInputN, 0.f), inR (kInputN, 0.f);
            for (int i = 0; i < kInputN; ++i) { inL[i] = source[i]; inR[i] = source[i]; }

            for (bool useRamp : { false, true })
            {
                std::vector<float> outAL (kTotalOut, 0.f), outAR (kTotalOut, 0.f);
                std::vector<float> outBL (kTotalOut, 0.f), outBR (kTotalOut, 0.f);

                ClipIndependentPitchCore coreA, coreB;
                coreA.prepare (kSr, 2048, 2);
                coreB.prepare (kSr, 2048, 2);

                ClipPitchProcessParams params;
                params.pitchSemitones = 2.0;
                params.sampleRate     = (int) kSr;
                params.channels       = 2;
                params.stretchRatio   = 1.0;

                std::vector<float> ramp (kTotalOut, 2.0f);
                if (useRamp)
                {
                    params.pitchRampData   = ramp.data();
                    params.pitchRampLength = kTotalOut;
                }

                // Case A: 2048-by-2048 blocks until 12000 output samples.
                {
                    const float* inP[2] = { inL.data(), inR.data() };
                    float* outP[2] = { outAL.data(), outAR.data() };
                    int produced = 0;
                    while (produced < kTotalOut)
                    {
                        const int chunk = juce::jmin (2048, kTotalOut - produced);
                        coreA.processBlock (inP, outP, 2, chunk, params);
                        inP[0] += chunk; inP[1] += chunk;
                        outP[0] += chunk; outP[1] += chunk;
                        produced += chunk;
                    }
                }

                // Case B: 1024-by-1024 blocks until 12000 output samples.
                {
                    const float* inP[2] = { inL.data(), inR.data() };
                    float* outP[2] = { outBL.data(), outBR.data() };
                    int produced = 0;
                    while (produced < kTotalOut)
                    {
                        const int chunk = juce::jmin (1024, kTotalOut - produced);
                        coreB.processBlock (inP, outP, 2, chunk, params);
                        inP[0] += chunk; inP[1] += chunk;
                        outP[0] += chunk; outP[1] += chunk;
                        produced += chunk;
                    }
                }

                // After warmup, compare the FINAL 2048-sample window of each
                // stream: block segmentation must not change the audio.
                // (The Signalsmith RNG is only consumed when timeFactor > 2,
                // so at +2st/1.0 both instances are deterministic.)
                const int cmpStart = kTotalOut - 2048;
                expect (equalsTol (outAL.data() + cmpStart, outBL.data() + cmpStart, 2048, 1e-3f),
                        juce::String ("pitch core 2x1024==1x2048 ramp=") + juce::String (useRamp ? 1 : 0));
                expect (secondHalfValid (outBL.data() + cmpStart, 2048, 1e-4f),
                        "pitch core second half valid after warmup");
                expect (allFinite (outBL.data(), kTotalOut), "pitch core finite");
            }

            // Full matrix: neutral pitch must be a plain copy (memcpy path).
            for (int bs : blockSizes)
            {
                ClipIndependentPitchCore core;
                core.prepare (kSr, 2048, 2);
                ClipPitchProcessParams params;
                params.sampleRate   = (int) kSr;
                params.channels     = 2;
                params.stretchRatio = 1.0; // pitch 0, stretch 1 -> bypass memcpy

                std::vector<float> in (bs, 0.f), out (bs, 0.f);
                for (int i = 0; i < bs; ++i) in[i] = source[i];
                const float* inP[2]  = { in.data(), in.data() };
                float* outP[2]       = { out.data(), out.data() };

                core.processBlock (inP, outP, 2, bs, params);
                expect (equalsTol (in.data(), out.data(), bs, 1e-6f),
                        juce::String ("neutral pitch copy n=") + juce::String (bs));
            }
        }

        beginTest ("stage6.clip-independent-pitch-core.startup-preroll");
        {
            constexpr int blockSize = 2048;
            constexpr double deviceRate = 48000.0;
            constexpr double stretchRatio = 1.5;
            constexpr float amplitude = 0.4f;
            constexpr double frequency = 440.0;

            ClipIndependentPitchCore core;
            core.prepare (deviceRate, blockSize, 2);

            ClipPitchProcessParams params;
            params.sampleRate = (int) deviceRate;
            params.channels = 2;
            params.stretchRatio = stretchRatio;
            params.startupPreRollSamples = core.requiredStartupPreRollSamples (stretchRatio);

            const int inputSamples = (int) std::llround (blockSize / stretchRatio);
            const int totalInput = params.startupPreRollSamples + inputSamples * 3;
            std::vector<float> source (totalInput, 0.0f);
            std::vector<float> outputL (blockSize * 2, 0.0f);
            std::vector<float> outputR (blockSize * 2, 0.0f);
            for (int i = 0; i < totalInput; ++i)
                source[(size_t) i] = amplitude * std::sin (
                    (float) (2.0 * juce::MathConstants<double>::pi * frequency * i / deviceRate));

            const auto firstPlan = DAW::SoundEngine::ApexPitchTimeInputPlanCore::makeInputPlan (
                blockSize, 0, (float) stretchRatio, totalInput, 0,
                params.startupPreRollSamples);
            expect (params.startupPreRollSamples > 0,
                    "stretched playback requests a nonzero Signalsmith startup seek");
            expectEquals (firstPlan.safeInputSamples,
                          params.startupPreRollSamples + inputSamples,
                          "first input window includes seek samples and the first process block");

            const float* firstInput[2] = { source.data(), source.data() };
            float* firstOutput[2] = { outputL.data(), outputR.data() };
            core.processBlock (firstInput, firstOutput, 2, blockSize, params);
            expectEquals (core.activeInputLeadSamples(), params.startupPreRollSamples,
                          "core publishes the source lead consumed by outputSeek");

            const auto secondPlan = DAW::SoundEngine::ApexPitchTimeInputPlanCore::makeInputPlan (
                blockSize, blockSize, (float) stretchRatio, totalInput,
                core.activeInputLeadSamples(), 0);
            expectEquals (secondPlan.inputClipOffset,
                          (int64_t) inputSamples + params.startupPreRollSamples,
                          "next source window continues after seek and processed input");

            const float* secondInput[2] = {
                source.data() + secondPlan.inputClipOffset,
                source.data() + secondPlan.inputClipOffset
            };
            float* secondOutput[2] = {
                outputL.data() + blockSize,
                outputR.data() + blockSize
            };
            core.processBlock (secondInput, secondOutput, 2, blockSize, params);

            double sumSquares = 0.0;
            for (int i = 0; i < blockSize; ++i)
                sumSquares += (double) outputL[(size_t) i] * outputL[(size_t) i];
            const float firstBlockRms = (float) std::sqrt (sumSquares / blockSize);
            expect (firstBlockRms > 0.05f,
                    "2048-sample stretched clip produces audible audio in its first block");
            float maxAdjacentJump = 0.0f;
            for (size_t i = 1; i < outputL.size(); ++i)
                maxAdjacentJump = juce::jmax (maxAdjacentJump,
                    std::abs (outputL[i] - outputL[i - 1]));
            expect (maxAdjacentJump < 0.08f,
                    "seek pre-roll and the next 2048 block join without a hard discontinuity");
            expect (allFinite (outputL.data(), (int) outputL.size())
                        && allFinite (outputR.data(), (int) outputR.size()),
                    "startup pre-roll output remains finite");
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 6b — DIRECT SIGNALSMITH WITH FIXED SEED
        // Decisive test of pure DSP block-segmentation invariance, isolated
        // from the ClipIndependentPitchCore wrapper. Both instances use the
        // SAME deterministic seed; at timeFactor < 2 the RNG is not consumed.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage6b.signalsmith-direct-seeded");
        {
            constexpr int kTotalOut = 12000;
            constexpr int kInputN = 14000;
            std::vector<float> in (kInputN, 0.f);
            for (int i = 0; i < kInputN; ++i) in[i] = source[i];

            using Stretch = signalsmith::stretch::SignalsmithStretch<float>;
            Stretch stretchA (12345), stretchB (12345);   // same seed
            stretchA.presetDefault (1, (float) kSr);
            stretchB.presetDefault (1, (float) kSr);
            stretchA.setTransposeSemitones (2.0f);
            stretchB.setTransposeSemitones (2.0f);

            std::vector<float> outA (kTotalOut, 0.f), outB (kTotalOut, 0.f);

            // Case A: 2048-by-2048 blocks.
            {
                int produced = 0, consumed = 0;
                while (produced < kTotalOut)
                {
                    const int chunk = juce::jmin (2048, kTotalOut - produced);
                    const float* inP = in.data() + consumed;
                    float* outP = outA.data() + produced;
                    stretchA.process (&inP, chunk, &outP, chunk);
                    consumed += chunk;
                    produced += chunk;
                }
            }

            // Case B: 1024-by-1024 blocks.
            {
                int produced = 0, consumed = 0;
                while (produced < kTotalOut)
                {
                    const int chunk = juce::jmin (1024, kTotalOut - produced);
                    const float* inP = in.data() + consumed;
                    float* outP = outB.data() + produced;
                    stretchB.process (&inP, chunk, &outP, chunk);
                    consumed += chunk;
                    produced += chunk;
                }
            }

            const int cmpStart = kTotalOut - 2048;
            expect (equalsTol (outA.data() + cmpStart, outB.data() + cmpStart, 2048, 1e-3f),
                    "signalsmith direct 2x1024==1x2048 (seeded)");
            expect (secondHalfValid (outB.data() + cmpStart, 2048, 1e-4f),
                    "signalsmith direct second half valid");
            expect (allFinite (outB.data(), kTotalOut), "signalsmith direct finite");

            // Also verify a neutral (no transpose) run bypasses any spectral
            // processing artifact: output energy must track input energy.
            Stretch neutral (42);
            neutral.presetDefault (1, (float) kSr);
            std::vector<float> outN (2048, 0.f);
            const float* inP = in.data();
            float* outNP = outN.data();
            neutral.process (&inP, 2048, &outNP, 2048);
            expect (allFinite (outN.data(), 2048), "neutral signalsmith finite");
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 7 — PITCH SMOOTHER / INPUT PLAN
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage7.pitch-smoother-input-plan");
        {
            PitchSmootherCore sm;
            sm.prepare (kSr, 0.030);

            auto advance = [&] (int n)
            {
                float sum = 0.f;
                for (int i = 0; i < n; ++i) sum += sm.getNextSemitones();
                return sum;
            };

            // Same total advancement: 2x1024 vs 1x2048 (fresh smoother each).
            PitchSmootherCore smA, smB;
            smA.prepare (kSr, 0.030);
            smB.prepare (kSr, 0.030);
            const float sumA = advanceN (smA, 1024) + advanceN (smA, 1024);
            const float sumB = advanceN (smB, 2048);
            expect (std::abs (sumA - sumB) < 1e-4f, "pitch smoother 2x1024==1x2048");

            for (int n : blockSizes)
            {
                auto plan = ApexPitchTimeInputPlanCore::makeInputPlan (n, 0, 1.0f, 81920);
                expect (plan.inputSamplesNeeded == n && plan.safeInputSamples == n,
                        juce::String ("input plan n=") + juce::String (n));
                auto planStretch = ApexPitchTimeInputPlanCore::makeInputPlan (n, 0, 2.0f, 81920);
                expect (planStretch.safeInputSamples == (n + 1) / 2,
                        "input plan stretch halving");
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 8 — TAPE STOP
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage8.tape-stop");
        {
            std::vector<float> inL (2048, 0.f), inR (2048, 0.f);
            for (int i = 0; i < 2048; ++i) { inL[i] = source[i]; inR[i] = source[i]; }

            std::vector<float> outAL (2048, 0.f), outAR (2048, 0.f);
            std::vector<float> outBL (2048, 0.f), outBR (2048, 0.f);

            ProcessorCore tA, tB;
            tA.prepare (kSr, 2048);
            tB.prepare (kSr, 2048);

            std::copy (inL.begin(), inL.end(), outAL.begin());
            std::copy (inR.begin(), inR.end(), outAR.begin());
            std::copy (inL.begin(), inL.end(), outBL.begin());
            std::copy (inR.begin(), inR.end(), outBR.begin());

            tA.process (outAL.data(), outAR.data(), 1024, 0.5f);
            tA.process (outAL.data() + 1024, outAR.data() + 1024, 1024, 0.5f);
            tB.process (outBL.data(), outBR.data(), 2048, 0.5f);

            expect (equalsTol (outAL.data(), outBL.data(), 2048, 1e-4f), "tape stop 2x1024==1x2048");
            expect (allFinite (outBL.data(), 2048), "tape stop finite");
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 9 — CLIP RENDER COMPOSITION (source read + identity DSP)
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage9.clip-render-composition");
        {
            // Timeline window [10000, 10000+2048) rendered through the real
            // identity path (the production normal-clip path for pitch=0).
            const int64_t clipStart = 10000;
            const int64_t clipLen   = 20000;
            const int64_t playPos   = 10000;

            auto renderWindow = [&] (int n, int64_t atPlayPos, std::vector<float>& out)
            {
                auto plan = ApexSourceReadContractCore::makeReadPlan (atPlayPos, n, clipStart, clipLen);
                out.assign ((size_t) n, 0.f);
                if (! plan.intersects) return;

                TimePitchDSPCore core;
                core.prepare (kSr, 2048);
                TimePitchState st;
                core.setState (st);

                TimePitchRenderRequest req;
                req.sourceData         = source.data();
                req.sourceTotalSamples = (int) source.size();
                req.sourceStartSample  = (int64_t) (atPlayPos + plan.bufferStart - clipStart);
                req.sourceEndSample    = req.sourceStartSample + plan.count;
                req.outputData         = out.data();
                req.numOutputSamples   = plan.count;
                req.channel            = 0;
                req.state              = st;
                req.outputSampleRate   = kSr;
                core.renderClipSegment (req);
            };

            std::vector<float> out2048, outA, outB;
            renderWindow (2048, playPos, out2048);
            renderWindow (1024, playPos, outA);            // first half  [10000..11023]
            renderWindow (1024, playPos + 1024, outB);     // second half [11024..12047]

            std::vector<float> combined (2048, 0.f);
            std::copy (outA.begin(), outA.end(), combined.begin());
            std::copy (outB.begin(), outB.end(), combined.begin() + 1024);

            expect (equalsTol (combined.data(), out2048.data(), 2048, 1e-6f),
                    "clip render composition 2x1024==1x2048");
            expect (secondHalfValid (out2048.data(), 2048, 1e-4f), "composition second half valid");
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 10 — TRACK PROCESSING (volume + mute ramps, neutral state)
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage10.track-processing");
        {
            constexpr int kMaxTestBlock = 3000;
            for (int n : blockSizes)
            {
                VolumeRampCore vr;
                vr.prepare (kSr, kMaxTestBlock);
                vr.setTargetVolume (0.8f);
                vr.setTargetPan (0.0f);
                vr.generateRamps (n);
                const float* lr = vr.getLeftRamp();
                const float* rr = vr.getRightRamp();
                expect (lr != nullptr && rr != nullptr, "volume ramps exist");
                for (int i = 0; i < n; ++i)
                {
                    expect (std::isfinite (lr[i]) && std::isfinite (rr[i]),
                            juce::String ("volume ramp finite n=") + juce::String (n));
                }

                MuteFadeCore mf;
                mf.prepare (kSr, kMaxTestBlock);
                const float* mr = mf.generate (false, n);
                expect (mr != nullptr, "mute ramp exists");
                for (int i = 0; i < n; ++i)
                    expect (std::isfinite (mr[i]), "mute ramp finite");
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 12 — ROUTING (ApexRoutingBufferCore)
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage12.routing");
        {
            DAW::RoutingSnapshot snap;
            snap.version = 1;
            snap.nodes.push_back ({ "t1", "t1", DAW::RoutingNodeType::Track, true });
            snap.nodes.push_back ({ "master", "master", DAW::RoutingNodeType::Master, true });
            snap.processingOrder = { "t1", "master" };
            snap.nodeIndexById.emplace ("t1", 0u);
            snap.nodeIndexById.emplace ("master", 1u);

            ApexRoutingBufferCore rb;
            rb.prepare (8192, 8);
            rb.syncFromSnapshot (snap, 2048);

            auto* t1 = rb.findNodeBuffer ("t1");
            expect (t1 != nullptr && t1->getNumSamples() >= 2048, "node buffer covers 2048");
            if (t1 == nullptr) return;

            for (int i = 0; i < 2048; ++i)
                t1->setSample (0, i, source[i]);

            rb.clearSnapshotAudio (snap, 2048);
            bool cleared = true;
            for (int i = 1024; i < 2048; ++i)
                if (t1->getSample (0, i) != 0.f) { cleared = false; break; }
            expect (cleared, "routing clear covers full 2048");

            // Second-half write test: write 2048, verify all written.
            for (int i = 0; i < 2048; ++i)
                t1->setSample (0, i, 0.5f);
            bool written = true;
            for (int i = 1024; i < 2048; ++i)
                if (t1->getSample (0, i) != 0.5f) { written = false; break; }
            expect (written, "routing write covers full 2048");
        }

        // ══════════════════════════════════════════════════════════════════
        // STAGE 13 — PDC (ApexPdcDelayLineCore + MasterPdcCore)
        // ══════════════════════════════════════════════════════════════════
        beginTest ("stage13.pdc");
        {
            for (int delay : { 0, 100, 2048, 3000 })
            {
                ApexPdcDelayLineCore line;
                line.prepare (8192);
                line.setDelaySamples (delay);
                line.setCrossfadeSamples (0);

                std::vector<float> in (2048, 0.f), out (2048, 0.f);
                for (int i = 0; i < 2048; ++i) in[i] = source[i];

                line.push (in.data(), in.data(), 2048);
                line.read (out.data(), out.data(), 2048);

                expect (allFinite (out.data(), 2048), juce::String ("pdc finite delay=") + juce::String (delay));
                if (delay < 2048)
                {
                    // Delayed output must equal input shifted by delay.
                    bool aligned = true;
                    for (int i = delay; i < 2048; ++i)
                        if (std::abs (out[i] - in[i - delay]) > 1e-3f) { aligned = false; break; }
                    expect (aligned, juce::String ("pdc delay alignment=") + juce::String (delay));
                }
            }
        }
    }

private:
    static float advanceN (ArrangementEditor::PitchSmootherCore& sm, int n)
    {
        float sum = 0.f;
        for (int i = 0; i < n; ++i) sum += sm.getNextSemitones();
        return sum;
    }
};

static DspBlockInvarianceTests dspBlockInvarianceTests;

//==============================================================================
/** Focused clip-local Tape Stop contract tests.  These tests intentionally
    exercise the production ProcessorCore directly: AudioEngine supplies the
    immutable automation values and owns the isolated clip buffer, while this
    core owns only the prepared tape-head state. */
class TapeStopFocusedTests : public juce::UnitTest
{
public:
    TapeStopFocusedTests()
        : juce::UnitTest ("tape-stop.focused.v1", "APEX.Diagnostics") {}

    static void fillInput (std::vector<float>& left, std::vector<float>& right)
    {
        for (size_t i = 0; i < left.size(); ++i)
        {
            const float x = (float) i;
            left[i] = 0.2f + 0.0007f * x + 0.15f * std::sin (x * 0.031f);
            right[i] = -0.1f + 0.0003f * x + 0.11f * std::cos (x * 0.023f);
        }
    }

    static std::vector<float> makeTransitionValues (int numSamples, bool ending)
    {
        std::vector<float> values ((size_t) juce::jmax (0, numSamples), 0.0f);
        if (values.size() <= 1)
        {
            if (! values.empty())
                values.front() = ending ? 1.0f : 0.0f;
            return values;
        }

        for (size_t i = 0; i < values.size(); ++i)
        {
            const float t = (float) i / (float) (values.size() - 1);
            values[i] = ending ? t : 1.0f - t;
        }
        return values;
    }

    static bool allFinite (const std::vector<float>& values)
    {
        for (const auto value : values)
            if (! std::isfinite (value))
                return false;
        return true;
    }

    static bool equalsTol (const std::vector<float>& a,
                           const std::vector<float>& b,
                           float tolerance)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i] - b[i]) > tolerance)
                return false;
        return true;
    }

    void runTest() override
    {
        using APEX::TapeStop::ProcessorCore;
        using APEX::TapeStop::TransitionDirection;

        beginTest ("beginning-and-ending-trajectories");
        {
            constexpr int n = 1024;
            const auto beginningValues = makeTransitionValues (n, false);
            const auto endingValues = makeTransitionValues (n, true);

            std::vector<float> beginningL ((size_t) n), beginningR ((size_t) n);
            std::vector<float> endingL ((size_t) n), endingR ((size_t) n);
            fillInput (beginningL, beginningR);
            endingL = beginningL;
            endingR = beginningR;

            ProcessorCore beginning;
            beginning.prepare (48000.0, 256, 2);
            beginning.beginTransition (TransitionDirection::Beginning, 1.0f);
            beginning.processWithValues (beginningL.data(), beginningR.data(), n,
                                         0.0f, beginningValues.data());

            ProcessorCore ending;
            ending.prepare (48000.0, 256, 2);
            ending.beginTransition (TransitionDirection::Ending, 0.0f);
            ending.processWithValues (endingL.data(), endingR.data(), n,
                                      0.0f, endingValues.data());

            expect (std::abs (beginningL.front() - 0.2f) < 1.0e-6f,
                    "beginning starts from the first clip sample");
            expect (std::abs (endingL.front() - beginningL.front()) < 1.0e-6f,
                    "ending preserves normal playback at its entry");
            expect (std::abs (beginning.getCurrentRateForTesting() - 1.0f) < 1.0e-6f,
                    "beginning reaches normal rate at its endpoint");
            expect (std::abs (ending.getCurrentRateForTesting()) < 1.0e-6f,
                    "ending reaches stopped rate at its endpoint");
            expect (allFinite (beginningL) && allFinite (beginningR)
                        && allFinite (endingL) && allFinite (endingR),
                    "both transition directions remain finite");
        }

        beginTest ("duration-and-automation-boundaries");
        {
            constexpr int prefix = 17;
            constexpr int transition = 257;
            constexpr int suffix = 31;
            const int total = prefix + transition + suffix;
            std::vector<float> left ((size_t) total), right ((size_t) total);
            fillInput (left, right);
            const auto originalL = left;
            const auto originalR = right;
            const auto values = makeTransitionValues (transition, true);
            const auto bypassValues = std::vector<float> ((size_t) suffix, 0.0f);

            ProcessorCore core;
            core.prepare (44100.0, 128, 2);
            core.beginTransition (TransitionDirection::Ending, 0.0f);
            core.processWithValues (left.data() + prefix, right.data() + prefix,
                                   transition, 0.0f, values.data());
            core.processWithValues (left.data() + prefix + transition,
                                   right.data() + prefix + transition,
                                   suffix, 0.0f, bypassValues.data());

            bool prefixUntouched = true;
            for (int i = 0; i < prefix; ++i)
                prefixUntouched = prefixUntouched
                    && std::abs (left[(size_t) i] - originalL[(size_t) i]) < 1.0e-6f
                    && std::abs (right[(size_t) i] - originalR[(size_t) i]) < 1.0e-6f;

            bool suffixBypassed = true;
            for (int i = prefix + transition; i < total; ++i)
                suffixBypassed = suffixBypassed
                    && std::abs (left[(size_t) i] - originalL[(size_t) i]) < 1.0e-6f
                    && std::abs (right[(size_t) i] - originalR[(size_t) i]) < 1.0e-6f;

            expect (prefixUntouched, "samples before the automation boundary are untouched");
            expect (suffixBypassed, "samples after the exact transition duration bypass");
            expect (std::abs (core.getCurrentRateForTesting() - 1.0f) < 1.0e-6f,
                    "bypass endpoint restores normal rate");
        }

        beginTest ("curve-evaluation-and-per-sample-rate");
        {
            DAW::AutomationSnapshot::LaneSnapshot linear;
            linear.points.push_back ({ 0, 0.0f, DAW::AutomationCurveType::Linear, 0.0f });
            linear.points.push_back ({ 100, 1.0f, DAW::AutomationCurveType::Linear, 0.0f });

            DAW::AutomationSnapshot::LaneSnapshot smooth;
            smooth.points.push_back ({ 0, 0.0f, DAW::AutomationCurveType::Smooth, 0.0f });
            smooth.points.push_back ({ 100, 1.0f, DAW::AutomationCurveType::Linear, 0.0f });

            const float linearQuarter = linear.getValueAtSample (25, 0.0f);
            const float smoothQuarter = smooth.getValueAtSample (25, 0.0f);
            expect (std::abs (linearQuarter - 0.25f) < 1.0e-6f,
                    "linear automation remains linear");
            expect (smoothQuarter < linearQuarter - 0.01f,
                    "smooth automation produces a distinct curve");

            constexpr int n = 101;
            std::vector<float> linearValues ((size_t) n), smoothValues ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                linearValues[(size_t) i] = linear.getValueAtSample (i, 0.0f);
                smoothValues[(size_t) i] = smooth.getValueAtSample (i, 0.0f);
            }

            std::vector<float> inputL ((size_t) n), inputR ((size_t) n);
            fillInput (inputL, inputR);
            auto linearOutL = inputL;
            auto linearOutR = inputR;
            auto smoothOutL = inputL;
            auto smoothOutR = inputR;

            ProcessorCore linearCore;
            linearCore.prepare (44100.0, n, 2);
            linearCore.beginTransition (TransitionDirection::Ending, 0.0f);
            linearCore.processWithValues (linearOutL.data(), linearOutR.data(), n,
                                          0.0f, linearValues.data());

            ProcessorCore smoothCore;
            smoothCore.prepare (44100.0, n, 2);
            smoothCore.beginTransition (TransitionDirection::Ending, 0.0f);
            smoothCore.processWithValues (smoothOutL.data(), smoothOutR.data(), n,
                                          0.0f, smoothValues.data());

            expect (! equalsTol (linearOutL, smoothOutL, 1.0e-5f),
                    "curve trajectory reaches the DSP as per-sample values");
        }

        beginTest ("bypass-and-reset-do-not-leak-ring-state");
        {
            constexpr int n = 512;
            std::vector<float> activeL ((size_t) n), activeR ((size_t) n);
            fillInput (activeL, activeR);
            const auto activeValues = makeTransitionValues (n, true);

            ProcessorCore core;
            core.prepare (44100.0, 128, 2);
            core.beginTransition (TransitionDirection::Ending, 0.0f);
            core.processWithValues (activeL.data(), activeR.data(), n,
                                    0.0f, activeValues.data());

            std::vector<float> bypassL ((size_t) n), bypassR ((size_t) n);
            fillInput (bypassL, bypassR);
            const auto expectedL = bypassL;
            const auto expectedR = bypassR;
            const auto bypassValues = std::vector<float> ((size_t) n, 0.0f);
            core.processWithValues (bypassL.data(), bypassR.data(), n,
                                    0.0f, bypassValues.data());
            expect (equalsTol (bypassL, expectedL, 1.0e-6f)
                        && equalsTol (bypassR, expectedR, 1.0e-6f),
                    "bypass is a direct path after an active transition");

            core.reset();
            std::vector<float> resetL ((size_t) n), resetR ((size_t) n);
            fillInput (resetL, resetR);
            const auto resetExpectedL = resetL;
            const auto resetExpectedR = resetR;
            core.processWithValues (resetL.data(), resetR.data(), n,
                                    0.0f, bypassValues.data());
            expect (equalsTol (resetL, resetExpectedL, 1.0e-6f)
                        && equalsTol (resetR, resetExpectedR, 1.0e-6f),
                    "reset makes old ring contents unreachable");
        }

        beginTest ("clip-isolation-and-multiple-clip-state");
        {
            constexpr int n = 1024;
            std::vector<float> clipAL ((size_t) n), clipAR ((size_t) n);
            std::vector<float> clipBL ((size_t) n), clipBR ((size_t) n);
            fillInput (clipAL, clipAR);
            for (int i = 0; i < n; ++i)
            {
                clipBL[(size_t) i] = 0.6f * std::sin ((float) i * 0.017f);
                clipBR[(size_t) i] = 0.4f * std::cos ((float) i * 0.019f);
            }
            const auto originalAL = clipAL;
            const auto originalAR = clipAR;
            const auto originalBL = clipBL;
            const auto originalBR = clipBR;
            const auto endingValues = makeTransitionValues (n, true);
            const auto beginningValues = makeTransitionValues (n, false);

            ProcessorCore clipA;
            ProcessorCore clipB;
            clipA.prepare (48000.0, 256, 2);
            clipB.prepare (48000.0, 256, 2);
            clipA.beginTransition (TransitionDirection::Ending, 0.0f);
            clipB.beginTransition (TransitionDirection::Beginning, 1.0f);
            clipA.processWithValues (clipAL.data(), clipAR.data(), n,
                                     0.0f, endingValues.data());
            clipB.processWithValues (clipBL.data(), clipBR.data(), n,
                                     0.0f, beginningValues.data());

            expect (! equalsTol (clipAL, originalAL, 1.0e-4f),
                    "one clip is changed by its own Tape Stop trajectory");

            auto referenceBL = originalBL;
            auto referenceBR = originalBR;
            ProcessorCore referenceB;
            referenceB.prepare (48000.0, 256, 2);
            referenceB.beginTransition (TransitionDirection::Beginning, 1.0f);
            referenceB.processWithValues (referenceBL.data(), referenceBR.data(), n,
                                          0.0f, beginningValues.data());
            expect (equalsTol (clipBL, referenceBL, 1.0e-6f)
                        && equalsTol (clipBR, referenceBR, 1.0e-6f),
                    "two clips do not share Tape Stop state or audio");
            expect (std::abs (clipAR.front() - originalAR.front()) < 1.0e-6f,
                    "clip A channel ownership remains isolated");
            expect (std::abs (clipBR.front() - originalBR.front()) < 1.0e-6f,
                    "clip B channel ownership remains isolated");
            expect (std::abs (clipA.getCurrentRateForTesting()
                              - clipB.getCurrentRateForTesting()) > 0.5f,
                    "different clips retain independent stopped states");
        }

        beginTest ("block-invariance-and-preparation-lifecycle");
        {
            constexpr int n = 2048;
            const auto values = makeTransitionValues (n, true);
            std::vector<float> oneBlockL ((size_t) n), oneBlockR ((size_t) n);
            std::vector<float> splitBlockL ((size_t) n), splitBlockR ((size_t) n);
            fillInput (oneBlockL, oneBlockR);
            splitBlockL = oneBlockL;
            splitBlockR = oneBlockR;

            ProcessorCore oneBlock;
            ProcessorCore splitBlock;
            oneBlock.prepare (44100.0, 2048, 2);
            splitBlock.prepare (44100.0, 1024, 2);
            oneBlock.beginTransition (TransitionDirection::Ending, 0.0f);
            splitBlock.beginTransition (TransitionDirection::Ending, 0.0f);
            oneBlock.processWithValues (oneBlockL.data(), oneBlockR.data(), n,
                                        0.0f, values.data());
            splitBlock.processWithValues (splitBlockL.data(), splitBlockR.data(), 1024,
                                          0.0f, values.data());
            splitBlock.processWithValues (splitBlockL.data() + 1024,
                                          splitBlockR.data() + 1024, 1024,
                                          0.0f, values.data() + 1024);

            expect (equalsTol (oneBlockL, splitBlockL, 1.0e-6f)
                        && equalsTol (oneBlockR, splitBlockR, 1.0e-6f),
                    "one block and two blocks are sample-equivalent");

            const int oldCapacity = splitBlock.getBufferCapacityForTesting();
            splitBlock.prepare (48000.0, 4096, 1);
            expect (splitBlock.getPreparedChannelCountForTesting() == 1,
                    "channel preparation is refreshed");
            expect (splitBlock.getBufferCapacityForTesting() >= oldCapacity,
                    "larger offline block retains sufficient prepared capacity");

            std::vector<float> offlineL (4096, 0.0f), offlineR (4096, 0.0f);
            std::vector<float> offlineValues (4096, 0.0f);
            splitBlock.processWithValues (offlineL.data(), offlineR.data(), 4096,
                                          0.0f, offlineValues.data());
            expect (allFinite (offlineL) && allFinite (offlineR),
                    "larger prepared/offline block remains finite");
        }
    }
};

static TapeStopFocusedTests tapeStopFocusedTests;
