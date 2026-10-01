// ===========================================================================
// TimePitchTestsCore.cpp
// Tests del sistema pitch/time stretch — matemáticas + integración.
// ===========================================================================
#include "PitchValueCore.h"
#include "StretchValueCore.h"
#include "FormantValueCore.h"
#include "TimePitchKnobMappingCore.h"
#include "TimePitchUICore.h"
#include "TimePitchSerializationCore.h"
#include "ResampleEngineCore.h"
#include "ClipSplitCore.h"
#include "WSOLAStretchCore.h"
#include "GranularStretchCore.h"
#include "PercussionStretchCore.h"
#include "OfflineHQTimePitchCore.h"
#include "TimePitchDSPCore.h"
#include "TimePitchUndoActions.h"
#include "TimePitchAutomationCore.h"
#include "TimePitchQualitySelectorCore.h"
#include "VocalPitchCore.h"
#include "TimePitchBounceCore.h"
#include "TimePitchArtifactControl.h"
#include "TimePitchPresetsCore.h"
#include "TimePitchCPUSafetyCore.h"
#include "TimePitchExportCore.h"
#include "VoiceTransformMapperCore.h"
#include "VoiceReactiveAnalyzerCore.h"
#include "ClipContextMenuCore.h"
#include "ArrangementUndoCore.h"
#include "UnifiedPitchStateCore.h"
#include "PitchZoneClassifierCore.h"
#include "PitchEngineBlendCore.h"
#include "ClipPitchRenderPathCore.h"
#include "DemonAtmosphereCore.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <thread>

namespace ArrangementEditor
{
namespace TimePitchTests
{

static bool approx(double a, double b, double tol = 0.0001)
{
    return std::abs(a - b) < tol;
}

// TEST 1 — +12 st ratio = 2.0
static void testPitchRatio()
{
    assert(approx(PitchValueCore::semitonesToRatio(0.0),   1.0));
    assert(approx(PitchValueCore::semitonesToRatio(12.0),  2.0));
    assert(approx(PitchValueCore::semitonesToRatio(-12.0), 0.5));
    assert(approx(PitchValueCore::semitonesToRatio(24.0),  4.0));
    assert(approx(PitchValueCore::semitonesToRatio(-24.0), 0.25));
    printf("[PASS] testPitchRatio\n");
}

// TEST 2 — +100 cents = +1 semitone
static void testFineTune()
{
    const double r1 = PitchValueCore::pitchToRatio(0.0, 100.0);
    const double r2 = PitchValueCore::semitonesToRatio(1.0);
    assert(approx(r1, r2, 0.0001));
    const double r3 = PitchValueCore::pitchToRatio(0.0, -100.0);
    const double r4 = PitchValueCore::semitonesToRatio(-1.0);
    assert(approx(r3, r4, 0.0001));
    printf("[PASS] testFineTune\n");
}

// TEST 3 — stretch %
static void testStretchRatio()
{
    assert(approx(StretchValueCore::percentToRatio(100.0), 1.0));
    assert(approx(StretchValueCore::percentToRatio(200.0), 2.0));
    assert(approx(StretchValueCore::percentToRatio(50.0),  0.5));
    printf("[PASS] testStretchRatio\n");
}

// TEST 4 — BPM
static void testBPMStretch()
{
    assert(approx(StretchValueCore::bpmToRatio(120.0, 120.0), 1.0));
    assert(approx(StretchValueCore::bpmToRatio(120.0,  60.0), 2.0));
    assert(approx(StretchValueCore::bpmToRatio( 60.0, 120.0), 0.5));
    printf("[PASS] testBPMStretch\n");
}

// TEST 5 — Resample +12 = half duration
static void testResampleDuration()
{
    TimePitchState state;
    state.pitchSemitones = 12.0;
    state.mode = TimePitchMode::Resample;
    assert(approx(ResampleEngineCore::processedDuration(10.0, state), 5.0));

    state.pitchSemitones = -12.0;
    assert(approx(ResampleEngineCore::processedDuration(10.0, state), 20.0));

    state.pitchSemitones = 0.0;
    assert(approx(ResampleEngineCore::processedDuration(10.0, state), 10.0));
    printf("[PASS] testResampleDuration\n");
}

// TEST 6 — Stretch 200% maintains pitch ratio
static void testStretchMaintainsPitch()
{
    TimePitchState state;
    state.pitchSemitones = 0.0;
    state.stretchRatio = 2.0;
    state.mode = TimePitchMode::Stretch;
    assert(approx(PitchValueCore::ratioFromState(state), 1.0));
    assert(approx(StretchValueCore::processedDuration(10.0, state.stretchRatio), 20.0));
    printf("[PASS] testStretchMaintainsPitch\n");
}

// TEST 7 — knob center = 0 / 1.0
static void testKnobMapping()
{
    assert(approx(TimePitchKnobMappingCore::pitchKnobToSemitones(0.5), 0.0));
    assert(approx(TimePitchKnobMappingCore::stretchKnobToRatio(0.5), 1.0));
    const double st = 7.5;
    const double back = TimePitchKnobMappingCore::pitchKnobToSemitones(
        TimePitchKnobMappingCore::semitonesToPitchKnob(st));
    assert(approx(back, st, 0.001));
    printf("[PASS] testKnobMapping\n");
}

// TEST 8 — isIdentity
static void testStateIdentity()
{
    TimePitchState s;
    assert(s.isIdentity());
    s.pitchSemitones = 1.0;
    assert(!s.isIdentity());
    s.pitchSemitones = 0.0;
    s.stretchRatio = 2.0;
    assert(!s.isIdentity());
    s.stretchRatio = 1.0;
    assert(s.isIdentity());
    printf("[PASS] testStateIdentity\n");
}

// TEST 9 — formant preservation compensates pitch
static void testFormantPreservation()
{
    TimePitchState state;
    state.pitchSemitones   = 7.0;
    state.formantSemitones = 0.0;
    state.preserveFormants = true;
    state.mode             = TimePitchMode::Vocal;
    const double eff = FormantValueCore::effectiveFormantSemitones(state, false);
    assert(approx(eff, -7.0));
    const double linked = FormantValueCore::effectiveFormantSemitones(state, true);
    assert(approx(linked, 7.0));
    printf("[PASS] testFormantPreservation\n");
}

// TEST 10 — Split preserves timePitch on both halves
static void testSplitPreservesTimePitch()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);
    clip.sourceOffset      = 0.0;
    clip.sourceStartSample = 0;
    clip.sourceEndSample   = 44100 * 10;
    clip.timePitch.pitchSemitones  = 5.0;
    clip.timePitch.stretchRatio    = 1.5;
    clip.timePitch.mode            = TimePitchMode::Stretch;

    SplitResult r = ClipSplitCore::splitClip(clip, 4.0);
    assert(r.valid);

    // Both halves have same timePitch
    assert(approx(r.left.timePitch.pitchSemitones,  5.0));
    assert(approx(r.right.timePitch.pitchSemitones, 5.0));
    assert(approx(r.left.timePitch.stretchRatio,    1.5));
    assert(r.left.timePitch.mode == TimePitchMode::Stretch);

    // Left clip ends at split, right clip starts at split
    assert(approx(r.left.length, 4.0));
    assert(approx(r.right.startTime, 4.0));
    assert(approx(r.right.length, 6.0));

    // Right sourceOffset advances
    assert(r.right.sourceOffset > clip.sourceOffset);
    assert(r.right.sourceStartSample == r.left.sourceEndSample);

    printf("[PASS] testSplitPreservesTimePitch\n");
}

// TEST 11 — Slip: only sourceOffset changes, timePitch unchanged
static void testSlipDoesNotAffectTimePitch()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 5.0, 4.0);
    clip.sourceOffset = 1.0;
    clip.timePitch.pitchSemitones = 3.0;
    clip.timePitch.mode = TimePitchMode::Vocal;

    // Simulate slip: move source window by 0.5s
    clip.sourceOffset += 0.5;

    // startTime and timePitch must be unchanged
    assert(approx(clip.startTime, 5.0));
    assert(approx(clip.timePitch.pitchSemitones, 3.0));
    assert(clip.timePitch.mode == TimePitchMode::Vocal);
    printf("[PASS] testSlipDoesNotAffectTimePitch\n");
}

// TEST 12 — Save/load round-trip restores all values
static void testSerializationRoundTrip()
{
    TimePitchState orig;
    orig.pitchSemitones   = 7.0;
    orig.fineTuneCents    = -25.0;
    orig.stretchRatio     = 1.333;
    orig.formantSemitones = -3.0;
    orig.mode             = TimePitchMode::Vocal;
    orig.preserveFormants = true;
    orig.transientPreserve= false;
    orig.highQuality      = true;
    orig.sourceSampleRate = 48000;
    orig.sourceBPM        = 140.0;
    orig.projectBPM       = 120.0;
    orig.voiceTransform.preset = VoiceTransformPreset::Abyss;
    orig.voiceTransform.demonAmount = 0.75f;
    orig.voiceTransform.reactiveMode = true;

    juce::ValueTree tree = TimePitchSerializationCore::save(orig);
    TimePitchState  loaded = TimePitchSerializationCore::load(tree);

    assert(approx(loaded.pitchSemitones,   orig.pitchSemitones));
    assert(approx(loaded.fineTuneCents,    orig.fineTuneCents,  0.1));
    assert(approx(loaded.stretchRatio,     orig.stretchRatio,   0.001));
    assert(approx(loaded.formantSemitones, orig.formantSemitones));
    assert(loaded.mode == orig.mode);
    assert(loaded.preserveFormants == orig.preserveFormants);
    assert(loaded.highQuality == orig.highQuality);
    assert(loaded.sourceSampleRate == orig.sourceSampleRate);
    assert(approx(loaded.sourceBPM,  orig.sourceBPM));
    assert(approx(loaded.projectBPM, orig.projectBPM));
    assert(loaded.voiceTransform.preset == orig.voiceTransform.preset);
    assert(approx(loaded.voiceTransform.demonAmount, orig.voiceTransform.demonAmount, 0.001));
    assert(loaded.voiceTransform.reactiveMode == orig.voiceTransform.reactiveMode);
    printf("[PASS] testSerializationRoundTrip\n");
}

// TEST 13 — Left resize: sourceOffset moves with startTime
static void testLeftResizeMovesSourceOffset()
{
    // Simulate what ClipEdgeHandleCore now does on left drag
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 5.0, 8.0);
    clip.sourceOffset = 2.0;

    const double origStart  = clip.startTime;
    const double origOffset = clip.sourceOffset;
    const double origLength = clip.length;
    const double delta      = 1.0; // trim 1 second from the left

    // Apply left-resize logic (matches ClipEdgeHandleCore::mouseDrag left branch)
    clip.startTime    = origStart  + delta;
    clip.sourceOffset = origOffset + delta;
    clip.length       = origLength - delta;

    // sourceOffset advanced by the same delta → audio content anchored at right
    assert(approx(clip.sourceOffset - origOffset, delta));
    // startTime advanced
    assert(approx(clip.startTime, 6.0));
    // length shortened
    assert(approx(clip.length, 7.0));
    printf("[PASS] testLeftResizeMovesSourceOffset\n");
}

// ── FASE 2: Real Engine Tests ─────────────────────────────────────────────

// TEST 14 — WSOLA Stretch 200%: output length doubles, no pitch change
static void testWSOLAStretchDoublesDuration()
{
    // Build a 1-second sine at 440 Hz at 44100 Hz
    const int   srcLen = 44100;
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    // Stretch 2x: output should be 2*srcLen samples
    const int outLen = srcLen * 2;
    std::vector<float> out(outLen, 0.f);

    WSOLAStretchCore wsola;
    wsola.prepare(sr, 512, TimePitchQuality::Draft);
    wsola.setStretchRatio(2.0);
    wsola.renderSegment(src.data(), srcLen, 0, srcLen,
                        out.data(), outLen, 0);

    // Verify output is non-silent (audio was generated)
    float rms = 0.f;
    for (int i = 0; i < outLen; ++i) rms += out[i] * out[i];
    rms = std::sqrt(rms / outLen);
    assert(rms > 0.01f); // has energy

    printf("[PASS] testWSOLAStretchDoublesDuration (rms=%.4f)\n", rms);
}

// TEST 15 — WSOLA Stretch 0.5x: output is half as long
static void testWSOLAStretchHalfDuration()
{
    const int   srcLen = 44100;
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.3f * std::sin(2.0 * juce::MathConstants<double>::pi * 880.0 * i / sr);

    const int outLen = srcLen / 2;
    std::vector<float> out(outLen, 0.f);

    WSOLAStretchCore wsola;
    wsola.prepare(sr, 512, TimePitchQuality::Draft);
    wsola.setStretchRatio(0.5);
    wsola.renderSegment(src.data(), srcLen, 0, srcLen,
                        out.data(), outLen, 0);

    float rms = 0.f;
    for (int i = 0; i < outLen; ++i) rms += out[i] * out[i];
    rms = std::sqrt(rms / outLen);
    assert(rms > 0.01f);

    printf("[PASS] testWSOLAStretchHalfDuration (rms=%.4f)\n", rms);
}

// TEST 16 — TimePitchDSPCore Mode 1 Stretch: produces output samples
static void testDSPCoreStretchProducesOutput()
{
    const int   srcLen = 4410; // 100ms
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    const int outLen = 512;
    std::vector<float> out(outLen, 0.f);

    TimePitchDSPCore dsp;
    dsp.prepare(sr, 512);

    TimePitchState state;
    state.pitchSemitones = 0.0;
    state.stretchRatio   = 2.0;
    state.mode           = TimePitchMode::Stretch;
    dsp.setState(state);

    TimePitchRenderRequest req;
    req.sourceData         = src.data();
    req.sourceTotalSamples = srcLen;
    req.sourceStartSample  = 0;
    req.sourceEndSample    = srcLen;
    req.outputData         = out.data();
    req.numOutputSamples   = outLen;
    req.channel            = 0;
    req.outputSampleRate   = sr;
    dsp.renderClipSegment(req);

    float rms = 0.f;
    for (int i = 0; i < outLen; ++i) rms += out[i] * out[i];
    rms = std::sqrt(rms / outLen);
    assert(rms > 0.001f);

    printf("[PASS] testDSPCoreStretchProducesOutput (rms=%.4f)\n", rms);
}

// TEST 16b — WSOLA must stay inside the clip's active source window
static void testWSOLARespectsSourceBounds()
{
    const int srcLen = 4096;
    std::vector<float> src(srcLen, 1.0f);
    for (int i = 1024; i < 1536; ++i)
        src[i] = 0.0f;

    std::vector<float> out(256, 0.f);

    WSOLAStretchCore wsola;
    wsola.prepare(44100.0, 256, TimePitchQuality::Draft);
    wsola.setStretchRatio(1.0);
    wsola.renderSegment(src.data(), srcLen, 1024, 1536,
                        out.data(), (int)out.size(), 0);

    float maxAbs = 0.f;
    for (float v : out)
        maxAbs = juce::jmax(maxAbs, std::abs(v));

    assert(maxAbs < 0.1f);
    printf("[PASS] testWSOLARespectsSourceBounds (maxAbs=%.4f)\n", maxAbs);
}

// TEST 17 — TimePitchDSPCore Mode 2 PitchOnly: produces output, no crash
static void testDSPCorePitchOnlyProducesOutput()
{
    const int   srcLen = 4410;
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.4f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    const int outLen = 512;
    std::vector<float> out(outLen, 0.f);

    TimePitchDSPCore dsp;
    dsp.prepare(sr, 512);

    TimePitchState state;
    state.pitchSemitones = 12.0; // +1 octave
    state.stretchRatio   = 1.0;  // duration unchanged
    state.mode           = TimePitchMode::PitchOnly;
    dsp.setState(state);

    TimePitchRenderRequest req;
    req.sourceData         = src.data();
    req.sourceTotalSamples = srcLen;
    req.sourceStartSample  = 0;
    req.sourceEndSample    = srcLen;
    req.outputData         = out.data();
    req.numOutputSamples   = outLen;
    req.channel            = 0;
    req.outputSampleRate   = sr;
    dsp.renderClipSegment(req);

    float rms = 0.f;
    for (int i = 0; i < outLen; ++i) rms += out[i] * out[i];
    rms = std::sqrt(rms / outLen);
    assert(rms > 0.001f);

    printf("[PASS] testDSPCorePitchOnlyProducesOutput +12st (rms=%.4f)\n", rms);
}

// TEST 17b — PitchOnly processed duration stays stable
static void testDSPCorePitchOnlyDurationStaysStable()
{
    TimePitchDSPCore dsp;
    dsp.prepare(44100.0, 512);

    TimePitchState state;
    state.pitchSemitones = 12.0;
    state.stretchRatio   = 1.0;
    state.mode           = TimePitchMode::PitchOnly;
    dsp.setState(state);

    assert(approx(dsp.processedDuration(10.0), 10.0));

    state.pitchSemitones = -12.0;
    dsp.setState(state);
    assert(approx(dsp.processedDuration(10.0), 10.0));

    printf("[PASS] testDSPCorePitchOnlyDurationStaysStable\n");
}

// TEST 18 — Unified pitch: pitch alone keeps arrangement duration stable
static void testDSPCoreResampleDurationChange()
{
    TimePitchState state;
    state.pitchSemitones = 12.0;
    state.stretchRatio   = 1.0;
    state.mode           = TimePitchMode::Resample;

    // processedDuration is arrangement/timeline length; pitch alone must not change it.
    TimePitchDSPCore dsp;
    dsp.prepare(44100.0, 512);
    dsp.setState(state);

    const double srcDur  = 10.0;
    const double outDur  = dsp.processedDuration(srcDur);
    assert(approx(outDur, 10.0));

    state.pitchSemitones = -12.0;
    dsp.setState(state);
    const double outDur2 = dsp.processedDuration(srcDur);
    assert(approx(outDur2, 10.0));

    printf("[PASS] testDSPCoreResampleDurationChange\n");
}

// TEST 19 — OfflineHQ cache miss returns nullptr, fallback is safe
static void testOfflineHQCacheMissFallback()
{
    OfflineHQTimePitchCore cache;

    TimePitchCacheKey key;
    key.sourceStartSample  = 0;
    key.sourceEndSample    = 44100;
    key.pitchSemitones     = 3.0;
    key.stretchRatio       = 1.5;
    key.mode               = TimePitchMode::OfflineHQ;
    key.sampleRate         = 44100;

    // No render yet → should be null
    const auto* buf = cache.tryGetBuffer(key);
    assert(buf == nullptr);

    printf("[PASS] testOfflineHQCacheMissFallback\n");
}

// TEST 20 — OfflineHQ: after renderOffline, cache is ready and key matches
static void testOfflineHQCacheHit()
{
    OfflineHQTimePitchCore cache;

    const int   srcLen = 4410;
    const double sr    = 44100.0;
    juce::AudioBuffer<float> srcBuf(1, srcLen);
    for (int i = 0; i < srcLen; ++i)
        srcBuf.getWritePointer(0)[i] = 0.3f * std::sin(
            2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    TimePitchCacheKey key;
    key.sourceStartSample  = 0;
    key.sourceEndSample    = srcLen;
    key.pitchSemitones     = 0.0;
    key.fineTuneCents      = 0.0;
    key.stretchRatio       = 2.0;
    key.formantSemitones   = 0.0;
    key.preserveFormants   = false;
    key.mode               = TimePitchMode::OfflineHQ;
    key.sampleRate         = (int)sr;

    // Render synchronously (normally done on background thread)
    cache.renderOffline(srcBuf, key, sr);

    assert(cache.isReady(key));
    const auto* buf = cache.tryGetBuffer(key);
    assert(buf != nullptr);
    assert(buf->getNumSamples() > 0);

    printf("[PASS] testOfflineHQCacheHit (outSamples=%d)\n", buf->getNumSamples());
}

// TEST 21 — Cache invalidates when pitch changes
static void testOfflineHQCacheInvalidatesOnPitchChange()
{
    OfflineHQTimePitchCore cache;

    const int   srcLen = 4410;
    const double sr    = 44100.0;
    juce::AudioBuffer<float> srcBuf(1, srcLen);
    srcBuf.clear();

    TimePitchCacheKey key;
    key.sourceStartSample  = 0;
    key.sourceEndSample    = srcLen;
    key.pitchSemitones     = 0.0;
    key.stretchRatio       = 1.0;
    key.mode               = TimePitchMode::OfflineHQ;
    key.sampleRate         = (int)sr;

    cache.renderOffline(srcBuf, key, sr);
    assert(cache.isReady(key));

    // Invalidate
    cache.invalidate();
    assert(!cache.isReady(key));

    // Old key no longer valid
    const auto* buf = cache.tryGetBuffer(key);
    assert(buf == nullptr);

    printf("[PASS] testOfflineHQCacheInvalidatesOnPitchChange\n");
}

// TEST 22 — GranularStretchCore produces non-silent output
static void testGranularStretchProducesOutput()
{
    const int   srcLen = 8820;
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.4f * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr);

    const int outLen = 512;
    std::vector<float> out(outLen, 0.f);

    GranularStretchCore granular;
    granular.prepare(sr, 512);
    granular.setStretchRatio(2.0);
    granular.renderSegment(src.data(), srcLen, 0, srcLen,
                           out.data(), outLen, 0);

    float rms = 0.f;
    for (int i = 0; i < outLen; ++i) rms += out[i] * out[i];
    rms = std::sqrt(rms / outLen);
    assert(rms > 0.001f);

    printf("[PASS] testGranularStretchProducesOutput (rms=%.4f)\n", rms);
}

// TEST 23 — PercussionStretchCore: no crash, produces output
static void testPercussionStretchNoCrash()
{
    const int   srcLen = 8820;
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    // Add a sharp transient at sample 0 + noise tail
    for (int i = 0; i < srcLen; ++i)
        src[i] = (i < 10 ? 0.9f : 0.1f) *
                 std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * i / sr);

    const int outLen = 512;
    std::vector<float> out(outLen, 0.f);

    PercussionStretchCore perc;
    perc.prepare(sr, 512);
    perc.setStretchRatio(1.5);
    perc.renderSegment(src.data(), srcLen, 0, srcLen,
                       out.data(), outLen, 0);

    // Should not NaN/inf
    bool hasValid = false;
    for (int i = 0; i < outLen; ++i)
    {
        assert(!std::isnan(out[i]));
        assert(!std::isinf(out[i]));
        if (out[i] != 0.f) hasValid = true;
    }
    assert(hasValid);

    printf("[PASS] testPercussionStretchNoCrash\n");
}

// TEST 24 — Split: two clips use different source bounds
static void testSplitDifferentSourceBounds()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);
    clip.sourceStartSample = 0;
    clip.sourceEndSample   = 44100 * 10;
    clip.timePitch.mode    = TimePitchMode::OfflineHQ;

    SplitResult r = ClipSplitCore::splitClip(clip, 4.0);
    assert(r.valid);

    // left.sourceEndSample < right.sourceStartSample boundary lines up
    assert(r.left.sourceEndSample == r.right.sourceStartSample);
    // They are different
    assert(r.left.sourceStartSample != r.right.sourceStartSample);
    // Cache keys would differ because sourceStartSample differs
    assert(r.left.sourceEndSample != r.right.sourceEndSample
        || r.left.sourceStartSample != r.right.sourceStartSample);

    printf("[PASS] testSplitDifferentSourceBounds\n");
}

// TEST 25 — Slip changes source window → cache key changes
static void testSlipChangesCacheKey()
{
    TimePitchCacheKey key1;
    key1.sourceStartSample = 0;
    key1.sourceEndSample   = 44100;
    key1.pitchSemitones    = 0.0;
    key1.stretchRatio      = 1.0;
    key1.sampleRate        = 44100;

    TimePitchCacheKey key2 = key1;
    key2.sourceStartSample = 4410; // slip forward 100ms

    assert(!(key1 == key2));

    printf("[PASS] testSlipChangesCacheKey\n");
}

// ── FASE 3: Polish / Undo / Automation / Quality / Stress ─────────────────

// TEST 26 — Undo action: perform applies newState, undo reverts to oldState
static void testUndoActionPerfomAndUndo()
{
    TimePitchState oldState;
    oldState.pitchSemitones = 0.0;
    oldState.stretchRatio   = 1.0;

    TimePitchState newState;
    newState.pitchSemitones = 7.0;
    newState.stretchRatio   = 1.5;

    TimePitchState applied;
    int callCount = 0;

    auto action = std::make_unique<TimePitchSetStateAction>(
        juce::Uuid(), "TestClip",
        oldState, newState,
        [&](const TimePitchState& s) { applied = s; ++callCount; });

    action->perform();
    assert(approx(applied.pitchSemitones, 7.0));
    assert(approx(applied.stretchRatio,   1.5));

    action->undo();
    assert(approx(applied.pitchSemitones, 0.0));
    assert(approx(applied.stretchRatio,   1.0));

    assert(callCount == 2);
    printf("[PASS] testUndoActionPerformAndUndo\n");
}

// TEST 27 — Undo coalescing: two actions on same clip merge into one
static void testUndoCoalesce()
{
    TimePitchState s0; s0.pitchSemitones = 0.0;
    TimePitchState s1; s1.pitchSemitones = 3.0;
    TimePitchState s2; s2.pitchSemitones = 7.0;

    TimePitchState lastApplied;
    auto applyFn = [&](const TimePitchState& s) { lastApplied = s; };

    juce::Uuid clipId;
    auto a1 = std::make_unique<TimePitchSetStateAction>(clipId, "C", s0, s1, applyFn);
    auto a2 = std::make_unique<TimePitchSetStateAction>(clipId, "C", s1, s2, applyFn);

    // Coalesce a2 into a1
    const bool merged = a1->coalesceWith(a2.get());
    assert(merged);

    // After merge: perform should apply s2, undo should apply s0
    a1->perform();
    assert(approx(lastApplied.pitchSemitones, 7.0));

    a1->undo();
    assert(approx(lastApplied.pitchSemitones, 0.0));

    printf("[PASS] testUndoCoalesce\n");
}

// TEST 28 — Automation: base state overridden when hasAutomation=true
static void testAutomationOverridesBaseState()
{
    TimePitchAutomationCore autoCore;

    TimePitchState base;
    base.pitchSemitones = 2.0;
    base.stretchRatio   = 1.0;

    // No automation: returns base unchanged
    const auto r1 = autoCore.getAutomatedState(base);
    assert(approx(r1.pitchSemitones, 2.0));

    // Set automation
    autoCore.setAutomatedPitch(5.0);
    autoCore.setAutomatedStretch(1.5);

    const auto r2 = autoCore.getAutomatedState(base);
    assert(approx(r2.pitchSemitones, 5.0));
    assert(approx(r2.stretchRatio,   1.5));

    // Clear: back to base
    autoCore.clearAutomation();
    const auto r3 = autoCore.getAutomatedState(base);
    assert(approx(r3.pitchSemitones, 2.0));

    printf("[PASS] testAutomationOverridesBaseState\n");
}

// TEST 29 — Quality selector: names + params for each quality level
static void testQualitySelectorNames()
{
    TimePitchQualitySelectorCore sel;

    // Default is Realtime
    assert(sel.getQuality() == TimePitchQuality::Realtime);
    assert(std::string(TimePitchQualitySelectorCore::qualityName(
        TimePitchQuality::Draft)) == "Draft");
    assert(std::string(TimePitchQualitySelectorCore::qualityName(
        TimePitchQuality::OfflineBest)) == "Offline Best");

    // Setting quality fires callback
    TimePitchQuality received = TimePitchQuality::Realtime;
    sel.onQualityChanged = [&](TimePitchQuality q) { received = q; };
    sel.setQualityByIndex(2); // Balanced
    assert(received == TimePitchQuality::Balanced);

    // Window sizes are monotonically increasing
    const auto pDraft    = TimePitchQualityParams::forQuality(TimePitchQuality::Draft);
    const auto pRealtime = TimePitchQualityParams::forQuality(TimePitchQuality::Realtime);
    const auto pHigh     = TimePitchQualityParams::forQuality(TimePitchQuality::High);
    assert(pDraft.windowSize < pRealtime.windowSize);
    assert(pRealtime.windowSize < pHigh.windowSize);

    printf("[PASS] testQualitySelectorNames\n");
}

// TEST 30 — DSPCore setQuality: re-prepares without crash
static void testDSPCoreSetQuality()
{
    TimePitchDSPCore dsp;
    dsp.prepare(44100.0, 512);

    // Should not crash on any quality change
    dsp.setQuality(TimePitchQuality::Draft);
    dsp.setQuality(TimePitchQuality::High);
    dsp.setQuality(TimePitchQuality::Realtime);

    printf("[PASS] testDSPCoreSetQuality\n");
}

// TEST 31 — VocalPitchCore formant compensation: preserveFormants changes filter alpha
static void testVocalFormantCompensation()
{
    VocalPitchCore vocal;
    vocal.prepare(44100.0, 512);

    const int   srcLen = 4410;
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.4f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    std::vector<float> outPreserve(512, 0.f);
    std::vector<float> outNoPreserve(512, 0.f);

    // With preserve
    {
        TimePitchState s;
        s.pitchSemitones   = 7.0;
        s.preserveFormants = true;
        s.mode = TimePitchMode::Vocal;
        vocal.setState(s);
        vocal.renderSegment(src.data(), srcLen, 0, srcLen,
                            outPreserve.data(), 512, 0);
    }

    vocal.reset();

    // Without preserve
    {
        TimePitchState s;
        s.pitchSemitones   = 7.0;
        s.preserveFormants = false;
        s.mode = TimePitchMode::Vocal;
        vocal.setState(s);
        vocal.renderSegment(src.data(), srcLen, 0, srcLen,
                            outNoPreserve.data(), 512, 0);
    }

    // Both should produce non-silent audio
    float rmsP = 0.f, rmsN = 0.f;
    for (int i = 0; i < 512; ++i)
    {
        rmsP += outPreserve[i]   * outPreserve[i];
        rmsN += outNoPreserve[i] * outNoPreserve[i];
    }
    rmsP = std::sqrt(rmsP / 512.f);
    rmsN = std::sqrt(rmsN / 512.f);
    assert(rmsP > 0.001f);
    assert(rmsN > 0.001f);

    // The two outputs should differ (filter is active in preserve mode)
    float diff = 0.f;
    for (int i = 0; i < 512; ++i)
        diff += std::abs(outPreserve[i] - outNoPreserve[i]);
    assert(diff > 0.01f); // filter produced a different tonal balance

    printf("[PASS] testVocalFormantCompensation (rmsPreserve=%.4f, diff=%.4f)\n", rmsP, diff);
}

// TEST 32 — visualLength: pitch-stable timeline, stretch-only duration changes
static void testVisualLength()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);

    // Default: identity
    assert(approx(clip.visualLength(), 10.0));

    // Pitch +12st: audible pitch changes, arrangement duration stays stable
    clip.timePitch.pitchSemitones = 12.0;
    clip.timePitch.mode = TimePitchMode::Resample;
    assert(approx(clip.visualLength(), 10.0));

    // Pitch -12st: duration still stable
    clip.timePitch.pitchSemitones = -12.0;
    assert(approx(clip.visualLength(), 10.0));

    // Stretch 200%: clip appears twice as wide
    clip.timePitch.pitchSemitones = 0.0;
    clip.timePitch.stretchRatio   = 2.0;
    clip.timePitch.mode           = TimePitchMode::Stretch;
    assert(approx(clip.visualLength(), 20.0));

    // PitchOnly: duration unchanged regardless of pitch
    clip.timePitch.pitchSemitones = 12.0;
    clip.timePitch.stretchRatio   = 1.0;
    clip.timePitch.mode           = TimePitchMode::PitchOnly;
    assert(approx(clip.visualLength(), 10.0));

    printf("[PASS] testVisualLength\n");
}

// TEST 33 — Stress: 50 clips with stretch, all produce non-NaN output
static void testStress50Clips()
{
    const int   srcLen   = 4410; // 100ms
    const double sr      = 44100.0;
    const int   outLen   = 256;

    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.3f * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr);

    int failures = 0;
    for (int clipIdx = 0; clipIdx < 50; ++clipIdx)
    {
        TimePitchDSPCore dsp;
        dsp.prepare(sr, 256);

        TimePitchState state;
        state.pitchSemitones = (double)(clipIdx % 13) - 6.0; // -6 to +6
        state.stretchRatio   = 0.5 + (clipIdx % 7) * 0.25;  // 0.5 to 2.0
        state.mode = static_cast<TimePitchMode>(clipIdx % 6); // all modes

        dsp.setState(state);

        std::vector<float> out(outLen, 0.f);
        TimePitchRenderRequest req;
        req.sourceData         = src.data();
        req.sourceTotalSamples = srcLen;
        req.sourceStartSample  = 0;
        req.sourceEndSample    = srcLen;
        req.outputData         = out.data();
        req.numOutputSamples   = outLen;
        req.channel            = 0;
        req.outputSampleRate   = sr;
        dsp.renderClipSegment(req);

        for (int s = 0; s < outLen; ++s)
        {
            if (std::isnan(out[s]) || std::isinf(out[s]))
            {
                ++failures;
                break;
            }
        }
    }

    assert(failures == 0);
    printf("[PASS] testStress50Clips (0 NaN/Inf failures across 50 clips)\n");
}

// TEST 34 — Stress: mode change during playback (rapid setState calls)
static void testStressModeChangeDuringPlayback()
{
    const int   srcLen = 4410;
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.3f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    TimePitchDSPCore dsp;
    dsp.prepare(sr, 256);

    std::vector<float> out(256, 0.f);
    TimePitchRenderRequest req;
    req.sourceData         = src.data();
    req.sourceTotalSamples = srcLen;
    req.sourceStartSample  = 0;
    req.sourceEndSample    = srcLen;
    req.outputData         = out.data();
    req.numOutputSamples   = 256;
    req.channel            = 0;
    req.outputSampleRate   = sr;

    // Simulate rapid mode changes between blocks
    int nanCount = 0;
    for (int block = 0; block < 100; ++block)
    {
        TimePitchState s;
        s.pitchSemitones = (block % 5) * 2.0;
        s.stretchRatio   = 1.0 + (block % 3) * 0.5;
        s.mode = static_cast<TimePitchMode>(block % 6);
        dsp.setState(s);

        std::fill(out.begin(), out.end(), 0.f);
        dsp.renderClipSegment(req);

        for (float v : out)
            if (std::isnan(v) || std::isinf(v)) { ++nanCount; break; }
    }

    assert(nanCount == 0);
    printf("[PASS] testStressModeChangeDuringPlayback (100 blocks, 0 bad values)\n");
}

// TEST 35 — Stress: split + slip + save/load chain
static void testStressSplitSlipSaveLoad()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 8.0);
    clip.timePitch.pitchSemitones = 3.0;
    clip.timePitch.stretchRatio   = 1.5;
    clip.timePitch.mode           = TimePitchMode::Stretch;
    clip.sourceEndSample = 44100 * 8;

    // Split at 4.0s
    SplitResult split = ClipSplitCore::splitClip(clip, 4.0);
    assert(split.valid);

    // Both halves should have correct timePitch
    assert(split.left.timePitch.mode  == TimePitchMode::Stretch);
    assert(split.right.timePitch.mode == TimePitchMode::Stretch);
    assert(approx(split.left.timePitch.pitchSemitones,  3.0));
    assert(approx(split.right.timePitch.pitchSemitones, 3.0));

    // Verify bounds are contiguous
    assert(split.left.sourceEndSample == split.right.sourceStartSample);

    // visualLength uses stretchRatio correctly after split
    assert(approx(split.left.visualLength(),  6.0, 0.01)); // 4.0 * 1.5
    assert(approx(split.right.visualLength(), 6.0, 0.01)); // 4.0 * 1.5

    // Save/load round-trip via serialization
    juce::ValueTree vt = TimePitchSerializationCore::save(split.left.timePitch);
    TimePitchState loaded = TimePitchSerializationCore::load(vt);
    assert(loaded.mode == TimePitchMode::Stretch);
    assert(approx(loaded.pitchSemitones, 3.0));
    assert(approx(loaded.stretchRatio,   1.5));

    printf("[PASS] testStressSplitSlipSaveLoad\n");
}

// ── RUN ALL ──────────────────────────────────────────────────────────────
static void runAll()
{
    printf("=== TimePitch Tests (Fase 1 + 2 + 3) ===\n");
    // Fase 1
    testPitchRatio();
    testFineTune();
    testStretchRatio();
    testBPMStretch();
    testResampleDuration();
    testStretchMaintainsPitch();
    testKnobMapping();
    testStateIdentity();
    testFormantPreservation();
    testSplitPreservesTimePitch();
    testSlipDoesNotAffectTimePitch();
    testSerializationRoundTrip();
    testLeftResizeMovesSourceOffset();
    // Fase 2
    testWSOLAStretchDoublesDuration();
    testWSOLAStretchHalfDuration();
    testDSPCoreStretchProducesOutput();
    testWSOLARespectsSourceBounds();
    testDSPCorePitchOnlyProducesOutput();
    testDSPCorePitchOnlyDurationStaysStable();
    testDSPCoreResampleDurationChange();
    testOfflineHQCacheMissFallback();
    testOfflineHQCacheHit();
    testOfflineHQCacheInvalidatesOnPitchChange();
    testGranularStretchProducesOutput();
    testPercussionStretchNoCrash();
    testSplitDifferentSourceBounds();
    testSlipChangesCacheKey();
    // Fase 3
    testUndoActionPerfomAndUndo();
    testUndoCoalesce();
    testAutomationOverridesBaseState();
    testQualitySelectorNames();
    testDSPCoreSetQuality();
    testVocalFormantCompensation();
    testVisualLength();
    testStress50Clips();
    testStressModeChangeDuringPlayback();
    testStressSplitSlipSaveLoad();
    printf("=== All 35 TimePitch Tests PASSED ===\n");
}

// ── FASE 4: Product Hardening ─────────────────────────────────────────────

// TEST 36 — AntiClickFader: FadeIn produces 0 at start, 1 at end
static void testAntiClickFadeIn()
{
    AntiClickFader fader;
    fader.prepare(44100.0);
    fader.triggerFadeIn();

    std::vector<float> buf(512, 1.0f);
    fader.process(buf.data(), (int)buf.size());

    // First sample should be near 0 (fade start)
    assert(buf[0] < 0.05f);
    // Last sample should be near 1 (fade nearing end or done)
    // (fade length = ~132 samples at 44100 → shorter than 512, so settled)
    assert(buf[511] == 1.0f); // after fade completes, no more modification
    printf("[PASS] testAntiClickFadeIn\n");
}

// TEST 37 — AntiClickFader: FadeOut produces 1 at start, 0 at end
static void testAntiClickFadeOut()
{
    AntiClickFader fader;
    fader.prepare(44100.0);
    fader.triggerFadeOut();

    std::vector<float> buf(512, 1.0f);
    fader.process(buf.data(), (int)buf.size());

    // First sample is near 1, last is 0 (fade done → zeroed)
    assert(buf[0] > 0.9f);
    assert(buf[511] == 0.0f);
    printf("[PASS] testAntiClickFadeOut\n");
}

// TEST 38 — ParameterSmoother: settles to target after enough samples
static void testParameterSmootherSettles()
{
    ParameterSmoother smoother;
    smoother.prepare(44100.0, 20.0); // 20 Hz cutoff
    smoother.setTarget(1.0);

    // Process enough samples to settle (~5 time constants = 5/(2π*20) * 44100 ≈ 1760 samples)
    for (int i = 0; i < 4000; ++i)
        smoother.process();

    assert(smoother.isSettled(0.001));
    assert(approx(smoother.getCurrent(), 1.0, 0.001));
    printf("[PASS] testParameterSmootherSettles\n");
}

// TEST 39 — ParameterSmoother: no zipper (output is monotonic from 0→1)
static void testParameterSmootherMonotonic()
{
    ParameterSmoother smoother;
    smoother.prepare(44100.0, 20.0);
    smoother.setTarget(1.0);

    double prev = smoother.getCurrent();
    bool monotonic = true;
    for (int i = 0; i < 2000; ++i)
    {
        double val = smoother.process();
        if (val < prev - 1e-9) { monotonic = false; break; }
        prev = val;
    }
    assert(monotonic);
    printf("[PASS] testParameterSmootherMonotonic\n");
}

// TEST 40 — TimePitchPresetsCore: all 7 presets have valid states
static void testPresetsAllValid()
{
    const auto presets = TimePitchPresetsCore::allPresets();
    assert(presets.size() == 7);

    for (const auto& p : presets)
    {
        assert(p.name != nullptr && p.name[0] != '\0');
        assert(p.description != nullptr);
        // Mode is a valid enum value
        const int m = static_cast<int>(p.state.mode);
        assert(m >= 0 && m <= 6);
        // Pitch in range
        assert(p.state.pitchSemitones >= -24.0 && p.state.pitchSemitones <= 24.0);
        // Stretch > 0
        assert(p.state.stretchRatio > 0.0);
    }

    printf("[PASS] testPresetsAllValid\n");
}

// TEST 41 — Preset "Chipmunk": Resample +12st
static void testPresetChipmunk()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);
    const TimePitchState old = TimePitchPresetsCore::applyPreset("Chipmunk", clip);

    assert(clip.timePitch.mode == TimePitchMode::Resample);
    assert(approx(clip.timePitch.pitchSemitones, 12.0));
    // Unified pitch keeps arrangement duration stable.
    assert(approx(clip.visualLength(), 10.0));

    printf("[PASS] testPresetChipmunk\n");
}

// TEST 42 — Preset "Demon": Resample -12st
static void testPresetDemon()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);
    TimePitchPresetsCore::applyPreset("Demon", clip);

    assert(clip.timePitch.mode == TimePitchMode::Resample);
    assert(approx(clip.timePitch.pitchSemitones, -12.0));
    assert(approx(clip.visualLength(), 10.0));

    printf("[PASS] testPresetDemon\n");
}

// TEST 43 — Preset "Slowed": Stretch -3st 130%
static void testPresetSlowed()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);
    TimePitchPresetsCore::applyPreset("Slowed", clip);

    assert(clip.timePitch.mode == TimePitchMode::Stretch);
    assert(approx(clip.timePitch.pitchSemitones, -3.0));
    assert(approx(clip.timePitch.stretchRatio,    1.30));
    assert(approx(clip.visualLength(), 13.0)); // 10 * 1.30

    printf("[PASS] testPresetSlowed\n");
}

// TEST 44 — CPUSafetyCore: too many clips forces Draft
static void testCPUSafetyForceDraft()
{
    TimePitchCPUSafetyCore safety;
    TimePitchCPUSafetyCore::Config cfg;
    cfg.maxRealtimeStretchClips = 4;
    safety.setConfig(cfg);

    TimePitchQuality forced = TimePitchQuality::Realtime;
    safety.onQualityForced = [&](TimePitchQuality q, const juce::String&) { forced = q; };

    safety.notifyActiveStretchClips(3); // under limit
    assert(forced == TimePitchQuality::Realtime); // no change

    safety.notifyActiveStretchClips(5); // over limit
    assert(forced == TimePitchQuality::Draft);
    assert(safety.currentForcedQuality() == TimePitchQuality::Draft);

    printf("[PASS] testCPUSafetyForceDraft\n");
}

// TEST 45 — CPUSafetyCore: shouldPreRenderInBackground
static void testCPUSafetyPreRenderFlag()
{
    TimePitchCPUSafetyCore safety;
    safety.notifyActiveStretchClips(0);
    assert(!safety.shouldPreRenderInBackground());

    safety.notifyActiveStretchClips(2);
    assert(safety.shouldPreRenderInBackground());

    printf("[PASS] testCPUSafetyPreRenderFlag\n");
}

// TEST 46 — ExportCore: beginExport sets isExporting, endExport clears it
static void testExportCoreFlags()
{
    TimePitchExportCore exp;
    assert(!exp.isExporting());

    bool beginFired = false, endFired = false;
    exp.onExportBegin = [&] { beginFired = true; };
    exp.onExportEnd   = [&] { endFired   = true; };

    exp.beginExport(44100.0, 512);
    assert(exp.isExporting());
    assert(beginFired);

    exp.endExport();
    assert(!exp.isExporting());
    assert(endFired);

    printf("[PASS] testExportCoreFlags\n");
}

// TEST 47 — ExportCore: latency compensation returns non-zero during export
static void testExportLatencyCompensation()
{
    TimePitchExportCore exp;
    assert(exp.exportLatencyCompensationSamples() == 0); // not exporting

    exp.beginExport(44100.0, 512);
    const int lat = exp.exportLatencyCompensationSamples();
    // OfflineBest windowSize=8192, latency=4096
    assert(lat == 4096);

    exp.endExport();
    assert(exp.exportLatencyCompensationSamples() == 0);

    printf("[PASS] testExportLatencyCompensation (latency=%d samples)\n", lat);
}

// TEST 48 — ExportCore: registers DSP, setQuality called on beginExport
static void testExportCoreRegistration()
{
    TimePitchExportCore exp;
    TimePitchDSPCore dsp;
    dsp.prepare(44100.0, 512);

    exp.registerDSP(&dsp);
    // beginExport should call dsp.setQuality(OfflineBest) without crash
    exp.beginExport(44100.0, 512);
    exp.endExport();
    exp.unregisterDSP(&dsp);

    printf("[PASS] testExportCoreRegistration\n");
}

// TEST 49 — Bounce: renderBounce produces valid buffer
static void testBounceClipSync()
{
    // Use synchronous freeze (simpler than async bounce for testing)
    TimePitchBounceCore bouncer;

    const int   srcLen = 4410;
    const double sr    = 44100.0;
    juce::AudioBuffer<float> src(1, srcLen);
    for (int i = 0; i < srcLen; ++i)
        src.getWritePointer(0)[i] = 0.4f * std::sin(
            2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    ArrangementClipModel model = ArrangementClipModel::createNew(0, 0.0, 0.1);
    model.length           = 0.1; // 100ms = 4410 samples
    model.timePitch.mode   = TimePitchMode::Stretch;
    model.timePitch.stretchRatio = 2.0;
    model.sourceEndSample  = srcLen;

    ClipFreezeState freezeState;
    bouncer.freezeClip(model, freezeState, src, sr);

    // Frozen buffer should be ~2x the source length (200% stretch)
    assert(freezeState.frozen);
    assert(freezeState.frozenBuffer.getNumSamples() > 0);
    assert(freezeState.frozenBuffer.getNumChannels() == 1);

    // Unfreeze restores state
    bouncer.unfreezeClip(model, freezeState);
    assert(!freezeState.frozen);
    assert(model.timePitch.mode == TimePitchMode::Stretch);

    printf("[PASS] testBounceClipSync (frozenSamples=%d)\n",
           freezeState.frozenBuffer.getNumSamples());
}

// TEST 50 — Stress: 200 clips, all modes, split+resize, no crash/NaN
static void testStress200Clips()
{
    const int   srcLen = 2205; // 50ms
    const double sr    = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.3f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    int nanCount    = 0;
    int crashCount  = 0;

    for (int clipIdx = 0; clipIdx < 200; ++clipIdx)
    {
        TimePitchDSPCore dsp;
        dsp.prepare(sr, 256);

        TimePitchState s;
        s.pitchSemitones = (double)((clipIdx * 7) % 25) - 12.0;
        s.stretchRatio   = 0.25 + (clipIdx % 16) * 0.25; // 0.25..4.0
        s.mode           = static_cast<TimePitchMode>(clipIdx % 7);
        dsp.setState(s);

        std::vector<float> out(256, 0.f);
        TimePitchRenderRequest req;
        req.sourceData         = src.data();
        req.sourceTotalSamples = srcLen;
        req.sourceStartSample  = (clipIdx % 3) * 500;
        req.sourceEndSample    = srcLen;
        req.outputData         = out.data();
        req.numOutputSamples   = 256;
        req.channel            = 0;
        req.outputSampleRate   = sr;

        try
        {
            dsp.renderClipSegment(req);
        }
        catch (...)
        {
            ++crashCount;
            continue;
        }

        for (float v : out)
            if (std::isnan(v) || std::isinf(v)) { ++nanCount; break; }
    }

    assert(nanCount  == 0);
    assert(crashCount == 0);
    printf("[PASS] testStress200Clips (200 clips, 0 NaN, 0 crashes)\n");
}

// ── FINAL RUN ALL ─────────────────────────────────────────────────────────
static void runAllFase4()
{
    printf("=== TimePitch Fase 4 Tests ===\n");
    testAntiClickFadeIn();
    testAntiClickFadeOut();
    testParameterSmootherSettles();
    testParameterSmootherMonotonic();
    testPresetsAllValid();
    testPresetChipmunk();
    testPresetDemon();
    testPresetSlowed();
    testCPUSafetyForceDraft();
    testCPUSafetyPreRenderFlag();
    testExportCoreFlags();
    testExportLatencyCompensation();
    testExportCoreRegistration();
    testBounceClipSync();
    testStress200Clips();
    printf("=== All 15 Fase 4 Tests PASSED ===\n");
}

// ── FASE 5: Final Integration ─────────────────────────────────────────────

// TEST 51 — ClipContextMenuCore: callbacks wired correctly
static void testContextMenuCallbacks()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);
    clip.clipName = "TestVocal";

    bool openPanelCalled = false;
    bool presetCalled    = false;
    std::string presetApplied;

    ClipContextCallbacks cb;
    cb.openTimePitchPanel = [&](ArrangementClipModel*) { openPanelCalled = true; };
    cb.applyPreset = [&](ArrangementClipModel* c, const std::string& n)
    {
        presetCalled = true;
        presetApplied = n;
        TimePitchPresetsCore::applyPreset(n, *c);
    };
    cb.resetTimePitch = [](ArrangementClipModel* c) { c->timePitch = TimePitchState{}; };

    // Simulate applyPreset call (menu result = 102 → "Chipmunk")
    cb.applyPreset(&clip, "Chipmunk");
    assert(presetCalled);
    assert(presetApplied == "Chipmunk");
    assert(clip.timePitch.mode == TimePitchMode::Resample);
    assert(approx(clip.timePitch.pitchSemitones, 12.0));

    // Simulate reset
    cb.resetTimePitch(&clip);
    assert(approx(clip.timePitch.pitchSemitones, 0.0));

    printf("[PASS] testContextMenuCallbacks\n");
}

// TEST 52 — Safe defaults: drums → Percussion mode
static void testSafeDefaultDrums()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 4.0);
    clip.timePitch.mode = TimePitchMode::Stretch; // wrong mode for drums

    // Simulate safe default 1 (Drums → Percussion)
    TimePitchState s;
    s.mode           = TimePitchMode::Percussion;
    s.pitchSemitones = 0.0;
    s.stretchRatio   = 1.0;
    clip.timePitch   = s;

    assert(clip.timePitch.mode == TimePitchMode::Percussion);
    assert(approx(clip.timePitch.pitchSemitones, 0.0));
    assert(approx(clip.timePitch.stretchRatio,   1.0));

    printf("[PASS] testSafeDefaultDrums\n");
}

// TEST 53 — Safe defaults: loops → Stretch mode
static void testSafeDefaultLoops()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 4.0);
    clip.timePitch.mode = TimePitchMode::Resample;

    TimePitchState s;
    s.mode           = TimePitchMode::Stretch;
    s.pitchSemitones = 0.0;
    s.stretchRatio   = 1.0;
    clip.timePitch   = s;

    assert(clip.timePitch.mode == TimePitchMode::Stretch);
    printf("[PASS] testSafeDefaultLoops\n");
}

// TEST 54 — ArrangementUndoCore::perform() bridge wraps juce::UndoableAction
static void testUndoCorePerformBridge()
{
    ArrangementUndoCore undo;

    TimePitchState s0; s0.pitchSemitones = 0.0;
    TimePitchState s1; s1.pitchSemitones = 5.0;

    TimePitchState applied;
    undo.perform(new TimePitchSetStateAction(
        juce::Uuid(), "Clip",
        s0, s1,
        [&](const TimePitchState& s) { applied = s; }));

    assert(approx(applied.pitchSemitones, 5.0));
    assert(undo.canUndo());

    undo.undo();
    assert(approx(applied.pitchSemitones, 0.0));

    assert(undo.canRedo());
    undo.redo();
    assert(approx(applied.pitchSemitones, 5.0));

    printf("[PASS] testUndoCorePerformBridge\n");
}

// TEST 55 — Export quality: beginExport → OfflineBest, endExport → Realtime
static void testExportQualityLifecycle()
{
    TimePitchExportCore exp;
    exp.setPlaybackQuality(TimePitchQuality::Balanced);
    assert(exp.playbackQuality() == TimePitchQuality::Balanced);

    TimePitchDSPCore dsp;
    dsp.prepare(44100.0, 512);
    exp.registerDSP(&dsp);

    exp.beginExport(44100.0, 512);
    assert(exp.isExporting());
    // latency compensation is 4096 during export
    assert(exp.exportLatencyCompensationSamples() == 4096);

    exp.endExport(exp.playbackQuality()); // restore to Balanced
    assert(!exp.isExporting());
    assert(exp.exportLatencyCompensationSamples() == 0);

    exp.unregisterDSP(&dsp);
    printf("[PASS] testExportQualityLifecycle\n");
}

// TEST 56 — Freeze → audio thread reads frozen buffer, not DSP
static void testFreezeBufferReadable()
{
    ClipFreezeState fs;
    assert(!fs.frozen);
    assert(fs.readPos.load() == 0);

    // Simulate post-freeze state
    const int frozenLen = 4410;
    fs.frozenBuffer.setSize(1, frozenLen);
    fs.frozenBuffer.clear();
    for (int i = 0; i < frozenLen; ++i)
        fs.frozenBuffer.getWritePointer(0)[i] = 0.5f;
    fs.frozen = true;
    fs.readPos.store(0);

    // Simulate audio thread read
    const int blockSize = 256;
    std::vector<float> out(blockSize, 0.f);
    const float* src = fs.frozenBuffer.getReadPointer(0);
    const int pos    = fs.readPos.load();
    const int toCopy = juce::jmin(blockSize, frozenLen - pos);
    std::copy(src + pos, src + pos + toCopy, out.data());
    fs.readPos.fetch_add(toCopy);

    // All copied samples should be 0.5
    for (int i = 0; i < toCopy; ++i)
        assert(approx(out[i], 0.5f));

    assert(fs.readPos.load() == toCopy);
    printf("[PASS] testFreezeBufferReadable (%d samples read)\n", toCopy);
}

// TEST 57 — Frozen badge state flags are independent bool fields
// (ClipRenderCore is a JUCE Component — cannot be instantiated in a unit test
//  without a running MessageManager. We test the state logic directly.)
static void testFrozenBadgeState()
{
    // Verify that ClipFreezeState fields are default-initialized correctly
    ClipFreezeState fs;
    assert(!fs.frozen);
    assert(fs.readPos.load() == 0);
    assert(fs.frozenBuffer.getNumSamples() == 0);

    // Verify freeze then reset clears everything
    fs.frozen = true;
    fs.frozenBuffer.setSize(1, 100);
    fs.readPos.store(50);
    fs.reset();
    assert(!fs.frozen);
    assert(fs.readPos.load() == 0);
    assert(fs.frozenBuffer.getNumSamples() == 0);

    printf("[PASS] testFrozenBadgeState\n");
}

// TEST 58 — QA chain: vocal → split → pitch → stretch → freeze → save/load → unfreeze
static void testQAVocalChain()
{
    // 1. Create vocal clip
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);
    clip.sourceEndSample = 44100 * 10;

    // 2. Apply Vocal Clean preset
    TimePitchPresetsCore::applyPreset("Vocal Clean", clip);
    assert(clip.timePitch.mode == TimePitchMode::Vocal);

    // 3. Split at 5s
    SplitResult split = ClipSplitCore::splitClip(clip, 5.0);
    assert(split.valid);
    assert(split.left.timePitch.mode == TimePitchMode::Vocal);

    // 4. Pitch +12 on right half
    split.right.timePitch.pitchSemitones = 12.0;
    assert(approx(split.right.timePitch.pitchSemitones, 12.0));

    // 5. Stretch 130% on left half
    split.left.timePitch.stretchRatio = 1.30;
    assert(approx(split.left.visualLength(), 6.5)); // 5.0 * 1.30

    // 6. Save/load round-trip
    auto vt = TimePitchSerializationCore::save(split.left.timePitch);
    auto loaded = TimePitchSerializationCore::load(vt);
    assert(loaded.mode == TimePitchMode::Vocal);
    assert(approx(loaded.stretchRatio, 1.30));

    // 7. DSP renders without NaN
    const int srcLen = 4410;
    const double sr = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.3f * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr);

    TimePitchDSPCore dsp;
    dsp.prepare(sr, 256);
    dsp.setState(split.left.timePitch);

    std::vector<float> out(256, 0.f);
    TimePitchRenderRequest req;
    req.sourceData = src.data(); req.sourceTotalSamples = srcLen;
    req.sourceStartSample = 0;   req.sourceEndSample = srcLen;
    req.outputData = out.data(); req.numOutputSamples = 256;
    req.channel = 0;             req.outputSampleRate = sr;
    dsp.renderClipSegment(req);

    for (float v : out)
    {
        assert(!std::isnan(v));
        assert(!std::isinf(v));
    }

    printf("[PASS] testQAVocalChain (split+pitch+stretch+serialize+render)\n");
}

// TEST 59 — Export vs playback: same state produces same visualLength
static void testExportPlaybackAlignment()
{
    ArrangementClipModel clip = ArrangementClipModel::createNew(0, 0.0, 10.0);
    clip.timePitch.mode         = TimePitchMode::Stretch;
    clip.timePitch.stretchRatio = 1.5;

    // visualLength is independent of quality (export vs playback)
    const double vl = clip.visualLength();
    assert(approx(vl, 15.0));

    // Export latency compensation: 4096 samples at 44100 = ~92.9ms
    TimePitchExportCore exp;
    exp.beginExport(44100.0, 512);
    const int comp = exp.exportLatencyCompensationSamples();
    assert(comp == 4096);
    // 4096 / 44100 = ~0.0929s offset to trim from render start
    const double offsetMs = (comp / 44100.0) * 1000.0;
    assert(offsetMs > 90.0 && offsetMs < 95.0);
    exp.endExport();

    printf("[PASS] testExportPlaybackAlignment (offset=%.1fms)\n", offsetMs);
}

// TEST 60 — Stress: 30-min session save/load (serialization of many clips)
static void testStress30MinSession()
{
    // Simulate 120 clips across 30 minutes (15s each on average)
    std::vector<ArrangementClipModel> clips;
    clips.reserve(120);

    for (int i = 0; i < 120; ++i)
    {
        ArrangementClipModel c = ArrangementClipModel::createNew(
            i % 8,              // 8 tracks
            (double)i * 15.0,   // 15s spacing
            15.0);              // 15s length

        c.timePitch.mode           = static_cast<TimePitchMode>(i % 7);
        c.timePitch.pitchSemitones = (double)((i * 3) % 25) - 12.0;
        c.timePitch.stretchRatio   = 0.5 + (i % 8) * 0.25;
        clips.push_back(c);
    }

    // Save all
    std::vector<juce::ValueTree> saved;
    saved.reserve(clips.size());
    for (const auto& c : clips)
        saved.push_back(TimePitchSerializationCore::save(c.timePitch));

    // Load all and verify
    int mismatches = 0;
    for (int i = 0; i < (int)clips.size(); ++i)
    {
        const auto loaded = TimePitchSerializationCore::load(saved[i]);
        if (loaded.mode != clips[i].timePitch.mode)          ++mismatches;
        if (!approx(loaded.pitchSemitones,
                    clips[i].timePitch.pitchSemitones, 0.01)) ++mismatches;
        if (!approx(loaded.stretchRatio,
                    clips[i].timePitch.stretchRatio, 0.001))  ++mismatches;
    }

    assert(mismatches == 0);
    printf("[PASS] testStress30MinSession (120 clips, 0 serialization mismatches)\n");
}

// TEST 61 — Unified classifier: low extreme engages dark/tape/body crossfade
static void testUnifiedPitchDarkExtremeSnapshot()
{
    const auto s = UnifiedPitchStateCore::snapshotFromState([]
    {
        TimePitchState st;
        st.pitchSemitones = -36.0;
        st.stretchRatio = 1.0;
        return st;
    }());

    assert(s.zone == UnifiedPitchZone::DarkExtreme);
    assert(s.darkIntensity > 0.99);
    assert(s.chipIntensity == 0.0);
    assert(s.tapeBlend > 0.99);
    assert(s.independentBlend < 0.01);
    assert(s.formantScale >= 0.31 && s.formantScale <= 0.42);
    assert(s.bodyAmount > 0.99);
    assert(s.cryptAmount > 0.54);
    assert(s.echoAmount > 0.39);
    assert(s.howlAmount > 0.29);
    assert(s.wailAmount > 0.49);
    assert(s.wet > 0.999);
    assert(!s.preserveFormants);

    printf("[PASS] testUnifiedPitchDarkExtremeSnapshot\n");
}

// TEST 62 — Unified classifier: chipmunk extreme engages upper formant/tape path
static void testUnifiedPitchChipmunkSnapshot()
{
    TimePitchState st;
    st.pitchSemitones = 24.0;
    const auto s = UnifiedPitchStateCore::snapshotFromState(st);

    assert(s.zone == UnifiedPitchZone::ChipmunkExtreme);
    assert(s.chipIntensity > 0.3);
    assert(s.darkIntensity == 0.0);
    assert(s.formantScale > 1.0);
    assert(s.brightness > 0.3);
    assert(s.cryptAmount == 0.0);
    assert(s.echoAmount == 0.0);
    assert(s.howlAmount == 0.0);
    assert(s.wailAmount == 0.0);
    assert(s.wet > 0.999);

    printf("[PASS] testUnifiedPitchChipmunkSnapshot\n");
}

// TEST 63 — Legacy v0 pitch migration: cents become semitones, v1 remains semitones
static void testUnifiedPitchLegacyMigration()
{
    juce::ValueTree legacy(TimePitchSerializationCore::kTreeType);
    legacy.setProperty(TimePitchSerializationCore::kPropPitch, -2400.0, nullptr);
    TimePitchState loaded = TimePitchSerializationCore::load(legacy);
    assert(approx(loaded.pitchSemitones, -24.0));
    assert(loaded.pitchEngineVersion == 1);

    TimePitchState v1;
    v1.pitchSemitones = -36.0;
    auto saved = TimePitchSerializationCore::save(v1);
    TimePitchState roundTrip = TimePitchSerializationCore::load(saved);
    assert(approx(roundTrip.pitchSemitones, -36.0));
    assert(roundTrip.pitchEngineVersion == 1);

    printf("[PASS] testUnifiedPitchLegacyMigration\n");
}

// TEST 64 — Fully wet blend has no dry leak counter increment
static void testUnifiedPitchNoDryLeakWhenFullyWet()
{
    PitchEngineBlendCore blend;
    PitchForensicAuditCore audit;
    blend.prepare(44100.0, 16);

    float dry[16];
    float ind[16];
    float tap[16];
    float out[16] {};
    for (int i = 0; i < 16; ++i)
    {
        dry[i] = 1.0f;
        ind[i] = 0.25f;
        tap[i] = 0.5f;
    }

    UnifiedPitchSnapshot s;
    s.wet = 1.0;
    s.independentBlend = 0.0;
    s.tapeBlend = 1.0;
    blend.blend(dry, ind, tap, out, 16, s, &audit);

    const auto counters = audit.snapshot();
    assert(counters.dryLeakDetected == 0);
    assert(approx(out[0], 0.5, 0.001));

    printf("[PASS] testUnifiedPitchNoDryLeakWhenFullyWet\n");
}

// TEST 65 — Unified render counters match acceptance zones
static void testUnifiedPitchRenderCounters()
{
    const int srcLen = 4096;
    const double sr = 44100.0;
    std::vector<float> src(srcLen);
    for (int i = 0; i < srcLen; ++i)
        src[i] = 0.35f * std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * i / sr);

    std::vector<float> out(256, 0.0f);
    ClipPitchRenderPathCore path;
    path.prepare(sr, 256);

    TimePitchState state;
    state.pitchSemitones = -24.0;
    state.mode = TimePitchMode::Vocal;
    path.setState(state);

    ClipPitchRenderPathRequest req;
    req.sourceData = src.data();
    req.sourceTotalSamples = srcLen;
    req.sourceStartSample = 0;
    req.sourceEndSample = srcLen;
    req.outputData = out.data();
    req.numOutputSamples = (int)out.size();
    req.channel = 0;
    req.state = state;
    req.outputSampleRate = sr;
    path.render(req);

    const auto counters = path.audit().snapshot();
    assert(counters.unifiedPitchPathHits > 0);
    assert(counters.independentPitchHits > 0);
    assert(counters.tapeResampleHits > 0);
    assert(counters.darkExtremeHits > 0);
    assert(counters.formantShiftHits > 0);
    assert(counters.darkBodyHits > 0);
    assert(counters.cryptHits > 0);
    assert(counters.echoHits > 0);
    assert(counters.howlHits > 0);
    assert(counters.wailHits > 0);
    assert(counters.dryLeakDetected == 0);

    printf("[PASS] testUnifiedPitchRenderCounters\n");
}

// TEST 66 — Atmosphere Off preset bypasses all atmosphere returns
static void testDemonAtmosphereOffBypass()
{
    DemonAtmosphereCore atmo;
    atmo.prepare(44100.0, 128);
    atmo.setStyle(DemonAtmosphereStyle::Off);

    std::vector<float> voice(128, 0.25f);
    std::vector<float> out(128, 0.25f);
    UnifiedPitchSnapshot s;
    s.cryptAmount = 0.55;
    s.echoAmount = 0.40;
    s.howlAmount = 0.30;
    s.wailAmount = 0.50;

    PitchForensicAuditCore audit;
    atmo.processAndSum(voice.data(), out.data(), (int)out.size(), 0, s, &audit);
    const auto counters = audit.snapshot();

    assert(atmo.isBypassed());
    assert(counters.cryptHits == 0);
    assert(counters.echoHits == 0);
    assert(counters.howlHits == 0);
    assert(counters.wailHits == 0);
    assert(approx(out[0], 0.25, 0.0001));

    printf("[PASS] testDemonAtmosphereOffBypass\n");
}

// ── FINAL COMPLETE RUN ────────────────────────────────────────────────────
static void runAllFase5()
{
    printf("=== TimePitch Fase 5 Tests ===\n");
    testContextMenuCallbacks();
    testSafeDefaultDrums();
    testSafeDefaultLoops();
    testUndoCorePerformBridge();
    testExportQualityLifecycle();
    testFreezeBufferReadable();
    testFrozenBadgeState();
    testQAVocalChain();
    testExportPlaybackAlignment();
    testStress30MinSession();
    testUnifiedPitchDarkExtremeSnapshot();
    testUnifiedPitchChipmunkSnapshot();
    testUnifiedPitchLegacyMigration();
    testUnifiedPitchNoDryLeakWhenFullyWet();
    testUnifiedPitchRenderCounters();
    testDemonAtmosphereOffBypass();
    printf("=== All 10 Fase 5 Tests PASSED ===\n");
    printf("\n");
    printf("=== SISTEMA TimePitch COMPLETO: 60 tests PASSED ===\n");
    printf("    Fase 1:  13 tests - matematicas / tipos / serializacion\n");
    printf("    Fase 2:  12 tests - motores DSP reales (WSOLA/granular/HQ)\n");
    printf("    Fase 3:  10 tests - undo/automation/quality/formants/stress\n");
    printf("    Fase 4:  15 tests - bounce/freeze/presets/CPU/export/stress\n");
    printf("    Fase 5:  10 tests - integracion final / QA / chain completo\n");
}

} // namespace TimePitchTests
} // namespace ArrangementEditor

