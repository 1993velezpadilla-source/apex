// ===========================================================================
// TimePitchDSPCore.h
// Dispatcher principal — única interfaz entre AudioEngine y motores DSP.
//
// FASE 2: usa renderClipSegment() en lugar de processBlock().
//
// AudioEngine llama:
//   timePitchDSP.renderClipSegment(req, dstBuffer, dstChannel)
//
// TimePitchDSPCore selecciona el motor correcto y devuelve audio procesado.
//
// MODOS:
//   0 Resample    → ResampleEngineCore (tape/DJ, pitch+time linked)
//   1 Stretch     → PhaseVocoderStretchCore/WSOLA (duration only)
//   2 PitchOnly   → PhaseVocoderStretchCore/WSOLA (pitch only)
//   3 Vocal       → VocalPitchCore (WSOLA + formant placeholder)
//   4 Percussion  → PercussionStretchCore (WSOLA + transient detection)
//   5 Texture     → GranularStretchCore (granular OLA)
//   6 OfflineHQ   → OfflineHQTimePitchCore cache, fallback Resample
//
// AUDIO THREAD SAFETY:
//   renderClipSegment() — no allocations, no locks, no file IO.
//   Todos los motores pre-alocados en prepare().
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "TimePitchQualityCore.h"
#include "PitchValueCore.h"
#include "StretchValueCore.h"
#include "PitchScaleMathCore.h"
#include "ResampleEngineCore.h"
#include "PhaseVocoderStretchCore.h"
#include "VocalPitchCore.h"
#include "VoiceTransformMapperCore.h"
#include "PercussionStretchCore.h"
#include "GranularStretchCore.h"
#include "OfflineHQTimePitchCore.h"
#include "ClipPitchRenderPathCore.h"
#include "PitchTimelineCompensationCore.h"
#include <JuceHeader.h>
#include <atomic>
#include <memory>

namespace ArrangementEditor
{

// ---------------------------------------------------------------------------
// TimePitchRenderRequest — todo lo que necesita renderClipSegment()
// ---------------------------------------------------------------------------
struct TimePitchRenderRequest
{
    const float*    sourceData         = nullptr; // puntero al canal fuente
    int             sourceTotalSamples = 0;       // total de samples del source file
    int64_t         sourceStartSample  = 0;       // primer sample a usar
    int64_t         sourceEndSample    = 0;       // último sample (exclusive)
    float*          outputData         = nullptr; // buffer de salida
    int             numOutputSamples   = 0;       // cuántos samples generar
    int             channel            = 0;       // canal (para motores multi-ch)
    TimePitchState  state;                        // snapshot del clip
    double          outputSampleRate   = 44100.0;
    const char*     callerTag          = "UNSPECIFIED";
};

// ---------------------------------------------------------------------------
// TimePitchDSPCore
// ---------------------------------------------------------------------------
class TimePitchDSPCore
{
public:
    TimePitchDSPCore()
    {
        resampleEngine_  = std::make_unique<ResampleEngineCore>();
        vocoderEngine_   = std::make_unique<PhaseVocoderStretchCore>();
        vocalEngine_     = std::make_unique<VocalPitchCore>();
        percEngine_      = std::make_unique<PercussionStretchCore>();
        granularEngine_  = std::make_unique<GranularStretchCore>();
        offlineCache_    = std::make_unique<OfflineHQTimePitchCore>();
        unifiedPitchPath_ = std::make_unique<ClipPitchRenderPathCore>();
    }

    // -----------------------------------------------------------------------
    // prepare — llamar desde AudioEngine::prepare()
    // -----------------------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_   = sampleRate;
        maxBlockSize_ = maxBlockSize;

        resampleEngine_->prepare(sampleRate, maxBlockSize);
        vocoderEngine_->prepare(sampleRate, maxBlockSize, TimePitchQuality::Balanced);
        vocalEngine_->prepare(sampleRate, maxBlockSize);
        percEngine_->prepare(sampleRate, maxBlockSize);
        granularEngine_->prepare(sampleRate, maxBlockSize);
        unifiedPitchPath_->prepare(sampleRate, maxBlockSize);
    }

    void reset()
    {
        resampleEngine_->reset();
        vocoderEngine_->reset();
        vocalEngine_->reset();
        percEngine_->reset();
        granularEngine_->reset();
        unifiedPitchPath_->reset();
    }

    // -----------------------------------------------------------------------
    // setQuality — message thread. Re-prepares stretch engines.
    // Safe to call between transport stops; does not re-alloc on audio thread.
    // -----------------------------------------------------------------------
    void setQuality(TimePitchQuality quality)
    {
        if (quality == currentQuality_) return;
        currentQuality_ = quality;
        vocoderEngine_->prepare(sampleRate_, maxBlockSize_, quality);
        percEngine_->prepare(sampleRate_, maxBlockSize_);
        // granular uses grainSizeMs, not window quality — no action needed
    }

    // -----------------------------------------------------------------------
    // setState — message thread publishes new state.
    // Engines are updated; cache invalidated if anything changed.
    // -----------------------------------------------------------------------
    void setState(const TimePitchState& newState)
    {
        const TimePitchState old = currentState_;
        currentState_ = newState;

        if (!statesEqual(old, newState))
        {
            offlineCache_->invalidate();

            // Do NOT call reset() here — resetting overlap/OLA buffers on every
            // parameter change (knob drag) causes severe artifacts because the
            // WSOLA/phase-vocoder state is destroyed every audio block while the
            // user moves a knob.  Each engine's setState() updates parameters
            // smoothly in-place.  A full reset() only happens at transport jump
            // (AudioEngine calls reset() on transport stop/seek).

            resampleEngine_->setState(newState);
            vocoderEngine_->setState(newState);
            vocalEngine_->setState(newState);
            percEngine_->setStretchRatio(newState.stretchRatio);
            granularEngine_->setStretchRatio(newState.stretchRatio);
            unifiedPitchPath_->setState(newState);

            activeMode_.store(static_cast<int>(newState.mode));
        }
    }

    const TimePitchState& getState() const { return currentState_; }

    // -----------------------------------------------------------------------
    // latencySamples — AudioEngine reads this to compensate playback alignment.
    // Mode 0 Resample = 0 (no lookahead).
    // Mode 1-5 = WSOLA window (one frame).
    // Mode 6 OfflineHQ = 0 (cache is pre-rendered, no latency).
    // -----------------------------------------------------------------------
    int latencySamples() const noexcept
    {
        if (currentState_.isPitchActive()
            || (unifiedPitchPath_ != nullptr && unifiedPitchPath_->isPitchTransitionActive()))
            return unifiedPitchPath_ ? unifiedPitchPath_->getReportedLatencySamples() : 0;

        const TimePitchMode mode = static_cast<TimePitchMode>(activeMode_.load());
        switch (mode)
        {
            case TimePitchMode::Resample:
            case TimePitchMode::OfflineHQ:
                return 0;
            case TimePitchMode::Texture:
                return granularEngine_ ? granularEngine_->latencySamples() : 0;
            default: // Stretch, PitchOnly, Vocal, Percussion
                return vocoderEngine_ ? (int)(vocoderEngine_->getStretchRatio() * 256.0) : 0;
        }
    }
    //
    // Reads from req.sourceData[sourceStartSample..sourceEndSample],
    // processes according to req.state.mode,
    // writes req.numOutputSamples into req.outputData.
    //
    // If state is identity (no pitch/stretch/formant): memcpy path.
    // -----------------------------------------------------------------------
    void renderClipSegment(const TimePitchRenderRequest& req)
    {
        if (req.outputData == nullptr || req.numOutputSamples <= 0) return;

        if (req.state.isPitchActive()
            || (unifiedPitchPath_ != nullptr && unifiedPitchPath_->isPitchTransitionActive()))
        {
            ClipPitchRenderPathRequest unifiedReq;
            unifiedReq.sourceData = req.sourceData;
            unifiedReq.sourceTotalSamples = req.sourceTotalSamples;
            unifiedReq.sourceStartSample = req.sourceStartSample;
            unifiedReq.sourceEndSample = req.sourceEndSample;
            unifiedReq.outputData = req.outputData;
            unifiedReq.numOutputSamples = req.numOutputSamples;
            unifiedReq.channel = req.channel;
            unifiedReq.state = req.state;
            unifiedReq.outputSampleRate = req.outputSampleRate;
            unifiedReq.offline = req.state.highQuality || req.state.mode == TimePitchMode::OfflineHQ;
            unifiedPitchPath_->render(unifiedReq);
            return;
        }

        const TimePitchMode mode = static_cast<TimePitchMode>(activeMode_.load());

        // Identity fast path: no processing needed
        if (currentState_.isIdentity())
        {
            copyDirect(req);
            return;
        }

        if (std::abs(req.state.stretchRatio - 1.0) > 0.001)
        {
            DBG("[STRETCH CORE CALL] inSamples=" << (int)(req.sourceEndSample - req.sourceStartSample)
                << " outSamples=" << req.numOutputSamples
                << " ratio=" << req.state.stretchRatio
                << " caller=" << req.callerTag
                << " mode=" << static_cast<int>(req.state.mode));
        }

        switch (mode)
        {
            case TimePitchMode::Resample:
                renderResample(req);
                break;

            case TimePitchMode::Stretch:
            case TimePitchMode::PitchOnly:
            case TimePitchMode::Vocal:
                renderVocoder(req, mode);
                break;

            case TimePitchMode::Percussion:
                renderPercussion(req);
                break;

            case TimePitchMode::Texture:
                renderGranular(req);
                break;

            case TimePitchMode::OfflineHQ:
                renderOfflineHQ(req);
                break;
        }
    }

    // -----------------------------------------------------------------------
    // Legacy processBlock shim — for any code still using old API.
    // AudioEngine should migrate to renderClipSegment().
    // -----------------------------------------------------------------------
    void processBlock(const juce::AudioBuffer<float>& src,
                      juce::AudioBuffer<float>& dst, int numSamples)
    {
        const int channels = std::min(src.getNumChannels(), dst.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
        {
            TimePitchRenderRequest req;
            req.sourceData         = src.getReadPointer(ch);
            req.sourceTotalSamples = src.getNumSamples();
            req.sourceStartSample  = 0;
            req.sourceEndSample    = src.getNumSamples();
            req.outputData         = dst.getWritePointer(ch);
            req.numOutputSamples   = numSamples;
            req.channel            = ch;
            req.state              = currentState_;
            req.outputSampleRate   = sampleRate_;
            renderClipSegment(req);
        }
    }

    // -----------------------------------------------------------------------
    // processedDuration — for arrangement view clip sizing.
    // -----------------------------------------------------------------------
    double processedDuration(double sourceLength) const
    {
        juce::ignoreUnused(activeMode_);
        return PitchTimelineCompensationCore::getProcessedTimelineLengthForArrangement(sourceLength, currentState_.stretchRatio);
    }

    OfflineHQTimePitchCore* getOfflineCache() { return offlineCache_.get(); }
    int getReportedLatencySamples() const noexcept { return unifiedPitchPath_ ? unifiedPitchPath_->getReportedLatencySamples() : latencySamples(); }

private:
    // ── Render paths ─────────────────────────────────────────────────────

    void copyDirect(const TimePitchRenderRequest& req)
    {
        const int srcAvail = (int)(req.sourceEndSample - req.sourceStartSample);
        for (int i = 0; i < req.numOutputSamples; ++i)
        {
            const int64_t si = req.sourceStartSample + i;
            req.outputData[i] = (si >= 0 && si < req.sourceTotalSamples)
                ? req.sourceData[(int)si] : 0.f;
        }
    }

    void renderResample(const TimePitchRenderRequest& req)
    {
        // Resample: pitch and duration coupled via pitchRatio read rate.
        const double pitchRatio = PitchValueCore::pitchToAudibleRatio(req.state.pitchSemitones,
                                                                      req.state.fineTuneCents);
        const double readRate  = pitchRatio / req.state.stretchRatio;
        const int    srcTotal  = req.sourceTotalSamples;

        for (int i = 0; i < req.numOutputSamples; ++i)
        {
            const double srcPos = (double)req.sourceStartSample + (double)i * readRate;
            const int    i1     = (int)std::floor(srcPos);
            const float  frac   = (float)(srcPos - i1);
            // Bug 23 fix: cubic Hermite interpolation — matches ResampleEngineCore
            // and TapeResamplePitchEngineCore. Linear interpolation aliases at
            // pitch ratios > ~1.5x, producing audible imaging hiss.
            const auto safe = [&](int idx) noexcept -> float
            {
                return (idx >= 0 && idx < srcTotal) ? req.sourceData[idx] : 0.f;
            };
            const float y0 = safe(i1 - 1);
            const float y1 = safe(i1);
            const float y2 = safe(i1 + 1);
            const float y3 = safe(i1 + 2);
            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            req.outputData[i] = PitchScaleMathCore::flushDenormal(((c3 * frac + c2) * frac + c1) * frac + c0);
        }
    }

    void renderVocoder(const TimePitchRenderRequest& req, TimePitchMode mode)
    {
        if (mode == TimePitchMode::Vocal)
        {
            // Bug 24/25 fix: do NOT call setState twice per block.
            // The previous pattern set a modified state for the render, then
            // restored req.state immediately after — which triggered two IIR
            // flushes (from VocalPitchCore::setState's alpha-change detection)
            // on every block with an active voice transform, zeroing the
            // formant filter state every frame and eliminating the formant effect.
            //
            // Correct approach: build the effective state once, set it once,
            // and render. The next block's setState call (from TimePitchDSPCore
            // ::setState) will update parameters as needed without a double flush.
            if (req.state.voiceTransform.isActive())
            {
                TimePitchState renderState = req.state;
                const auto targets = VoiceTransformMapperCore::map(renderState.voiceTransform, 0.f);
                renderState.pitchSemitones = PitchScaleMathCore::clampPitch(
                    renderState.pitchSemitones + targets.pitchSemitones, true);
                renderState.formantSemitones = juce::jlimit(-12.0,  12.0,
                    renderState.formantSemitones + (double)targets.formantShift);
                vocalEngine_->setState(renderState);
            }
            else
            {
                vocalEngine_->setState(req.state);
            }

            vocalEngine_->renderSegment(req.sourceData, req.sourceTotalSamples,
                                        req.sourceStartSample, req.sourceEndSample,
                                        req.outputData, req.numOutputSamples,
                                        req.channel);
        }
        else
        {
            vocoderEngine_->renderSegment(req.sourceData, req.sourceTotalSamples,
                                           req.sourceStartSample, req.sourceEndSample,
                                           req.outputData, req.numOutputSamples,
                                           req.channel);
        }
    }

    void renderPercussion(const TimePitchRenderRequest& req)
    {
        percEngine_->renderSegment(req.sourceData, req.sourceTotalSamples,
                                    req.sourceStartSample, req.sourceEndSample,
                                    req.outputData, req.numOutputSamples,
                                    req.channel);
    }

    void renderGranular(const TimePitchRenderRequest& req)
    {
        granularEngine_->renderSegment(req.sourceData, req.sourceTotalSamples,
                                        req.sourceStartSample, req.sourceEndSample,
                                        req.outputData, req.numOutputSamples,
                                        req.channel);
    }

    void renderOfflineHQ(const TimePitchRenderRequest& req)
    {
        // Build cache key for this request
        TimePitchCacheKey key;
        key.sourceStartSample  = req.sourceStartSample;
        key.sourceEndSample    = req.sourceEndSample;
        key.pitchSemitones     = req.state.pitchSemitones;
        key.fineTuneCents      = req.state.fineTuneCents;
        key.stretchRatio       = req.state.stretchRatio;
        key.formantSemitones   = req.state.formantSemitones;
        key.preserveFormants   = req.state.preserveFormants;
        key.mode               = TimePitchMode::OfflineHQ;
        key.sampleRate         = (int)req.outputSampleRate;

        const auto* cached = offlineCache_->tryGetBuffer(key);
        if (cached && cached->getNumSamples() >= req.numOutputSamples
            && req.channel < cached->getNumChannels())
        {
            // Read directly from pre-rendered cache
            juce::FloatVectorOperations::copy(
                req.outputData,
                cached->getReadPointer(req.channel),
                req.numOutputSamples);
        }
        else
        {
            DBG("[TimePitch] Offline HQ cache miss; using realtime independent fallback. mode="
                << static_cast<int>(req.state.mode)
                << " pitch=" << req.state.pitchSemitones
                << " fine=" << req.state.fineTuneCents
                << " stretch=" << req.state.stretchRatio);

            if (req.state.isPitchActive())
                renderVocoder(req, TimePitchMode::Vocal);
            else if (req.state.isStretchActive())
                renderVocoder(req, TimePitchMode::Stretch);
            else
                copyDirect(req);

            // Background render will be triggered by AudioEngine or UI layer
            // via TimePitchBackgroundRenderCore — not from audio thread.
        }
    }

    // ── State equality ───────────────────────────────────────────────────
    static bool statesEqual(const TimePitchState& a, const TimePitchState& b)
    {
        return a.mode == b.mode
            && std::abs(a.pitchSemitones   - b.pitchSemitones)   < 0.0001
            && a.pitchEngineVersion == b.pitchEngineVersion
            && std::abs(a.fineTuneCents    - b.fineTuneCents)    < 0.01
            && std::abs(a.stretchRatio     - b.stretchRatio)     < 0.0001
            && std::abs(a.formantSemitones - b.formantSemitones) < 0.0001
            && a.preserveFormants == b.preserveFormants
            && a.voiceTransform.preset == b.voiceTransform.preset
            && std::abs(a.voiceTransform.demonAmount - b.voiceTransform.demonAmount) < 0.0001f
            && a.voiceTransform.reactiveMode == b.voiceTransform.reactiveMode;
    }

    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 512;

    TimePitchState   currentState_;
    TimePitchQuality currentQuality_ = TimePitchQuality::Realtime;
    std::atomic<int> activeMode_ { static_cast<int>(TimePitchMode::Resample) };

    std::unique_ptr<ResampleEngineCore>      resampleEngine_;
    std::unique_ptr<PhaseVocoderStretchCore> vocoderEngine_;
    std::unique_ptr<VocalPitchCore>          vocalEngine_;
    std::unique_ptr<PercussionStretchCore>   percEngine_;
    std::unique_ptr<GranularStretchCore>     granularEngine_;
    std::unique_ptr<OfflineHQTimePitchCore>  offlineCache_;
    std::unique_ptr<ClipPitchRenderPathCore> unifiedPitchPath_;
};

} // namespace ArrangementEditor
