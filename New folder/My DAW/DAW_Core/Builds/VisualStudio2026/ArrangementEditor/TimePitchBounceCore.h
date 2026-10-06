// ===========================================================================
// TimePitchBounceCore.h
// Bounce / Freeze / Unfreeze para clips con pitch/time processing.
//
// CONCEPTOS:
//   Bounce:
//     Renderiza el clip procesado (pitch+stretch+formant) a un nuevo
//     AudioBuffer en memoria. El clip resultante tiene timePitch=identity
//     pero su audio ya está procesado. Sin más carga de CPU en playback.
//
//   Freeze:
//     Como Bounce pero temporal. El clip original se preserva.
//     Durante freeze: audio thread lee el buffer congelado.
//     Unfreeze: descarta el buffer, vuelve al procesamiento original.
//     Útil para liberar CPU durante mezcla sin perder los parámetros.
//
//   Unfreeze:
//     Restaura el estado original. El buffer congelado se libera.
//     CPU vuelve a pagar el costo del stretch en tiempo real.
//
// FLUJO:
//   1. User click "Bounce" en clip.
//   2. UI llama TimePitchBounceCore::bounceClip(clip, srcBuffer, sr).
//   3. Corre en background thread (no bloquea UI/audio).
//   4. onBounceComplete(bouncedBuffer, bouncedModel) callback.
//   5. AudioEngine reemplaza lectura con bouncedBuffer.
//
// THREAD SAFETY:
//   bounceClip() inicia job en ThreadPool.
//   Audio thread NO espera. Sigue con fallback hasta que bounce completa.
//   onBounceComplete se llama en message thread (via AsyncUpdater).
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "TimePitchDSPCore.h"
#include "ArrangementClipModel.h"
#include <JuceHeader.h>
#include <functional>
#include <atomic>

namespace ArrangementEditor
{

// ---------------------------------------------------------------------------
// FreezeState — attached to a clip model to track freeze status
// ---------------------------------------------------------------------------
struct ClipFreezeState
{
    bool frozen = false;

    // Pre-rendered audio — audio thread reads this when frozen
    juce::AudioBuffer<float> frozenBuffer;

    // Original timePitch — restored on unfreeze
    TimePitchState originalTimePitch;

    // Position in frozen buffer for audio thread sequential read
    std::atomic<int> readPos { 0 };

    void reset()
    {
        frozen = false;
        frozenBuffer.setSize(0, 0);
        readPos.store(0);
    }
};

// ---------------------------------------------------------------------------
// TimePitchBounceCore
// ---------------------------------------------------------------------------
class TimePitchBounceCore : private juce::AsyncUpdater
{
public:
    TimePitchBounceCore()
        : pool_(1)
    {}

    ~TimePitchBounceCore() override
    {
        pool_.removeAllJobs(true, 2000);
        cancelPendingUpdate();
    }

    // -----------------------------------------------------------------------
    // bounceClip — async bounce to a new buffer.
    //
    // Renders sourceBuffer through the full TimePitch DSP chain at
    // OfflineBest quality and calls onBounceComplete when done.
    //
    // The resulting buffer has the same number of channels as source.
    // Duration = sourceLen * stretchRatio (adjusted for pitch/mode).
    // -----------------------------------------------------------------------
    void bounceClip(const ArrangementClipModel& model,
                    const juce::AudioBuffer<float>& sourceBuffer,
                    double sampleRate)
    {

        struct BounceJob : public juce::ThreadPoolJob
        {
            BounceJob(TimePitchBounceCore* owner,
                      ArrangementClipModel m,
                      juce::AudioBuffer<float> s,
                      double sr)
                : juce::ThreadPoolJob("TimePitchBounce")
                , owner_(owner), model_(std::move(m)), src_(std::move(s)), sr_(sr) {}

            JobStatus runJob() override
            {
                auto result = owner_->renderBounce(model_, src_, sr_);
                {
                    juce::ScopedLock sl(owner_->resultLock_);
                    owner_->pendingResult_ = std::move(result);
                    owner_->pendingModel_  = model_;
                    owner_->pendingModel_.timePitch = TimePitchState{};
                    owner_->hasPendingResult_ = true;
                }
                owner_->triggerAsyncUpdate();
                return jobHasFinished;
            }

            TimePitchBounceCore*     owner_;
            ArrangementClipModel     model_;
            juce::AudioBuffer<float> src_;
            double sr_;
        };

        pool_.addJob(new BounceJob(this, model, sourceBuffer, sampleRate), true);
    }

    // -----------------------------------------------------------------------
    // freezeClip — synchronous if sourceBuffer is small, async if large.
    // For most clips (< 30s), runs sync in calling thread.
    // -----------------------------------------------------------------------
    void freezeClip(ArrangementClipModel& model,
                    ClipFreezeState& freezeState,
                    const juce::AudioBuffer<float>& sourceBuffer,
                    double sampleRate)
    {
        if (freezeState.frozen) return;

        freezeState.originalTimePitch = model.timePitch;
        freezeState.frozenBuffer      = renderBounce(model, sourceBuffer, sampleRate);
        freezeState.readPos.store(0);
        freezeState.frozen            = true;
    }

    void unfreezeClip(ArrangementClipModel& model,
                      ClipFreezeState& freezeState)
    {
        if (!freezeState.frozen) return;
        model.timePitch = freezeState.originalTimePitch;
        freezeState.reset();
    }

    // Callback — called on message thread when async bounce completes
    std::function<void(const ArrangementClipModel&,
                       juce::AudioBuffer<float>&&)> onBounceComplete;

private:
    // -----------------------------------------------------------------------
    // renderBounce — the actual DSP render (heavy, call off audio thread)
    // -----------------------------------------------------------------------
    juce::AudioBuffer<float> renderBounce(const ArrangementClipModel& model,
                                           const juce::AudioBuffer<float>& src,
                                           double sampleRate)
    {
        const int srcLen   = src.getNumSamples();
        const int channels = src.getNumChannels();
        if (srcLen == 0 || channels == 0)
            return juce::AudioBuffer<float>(channels, 0);

        // Calculate output length from visualLength()
        const int outLen = juce::jmax(1,
            (int)std::round(model.visualLength() / model.length * srcLen));

        juce::AudioBuffer<float> result(channels, outLen);

        for (int ch = 0; ch < channels; ++ch)
        {
            // Use OfflineBest WSOLA per channel
            WSOLAStretchCore wsola;
            wsola.prepare(sampleRate, outLen, TimePitchQuality::OfflineBest);

            const double totalSt    = model.timePitch.pitchSemitones
                                    + model.timePitch.fineTuneCents / 100.0;
            const double pitchRatio = std::pow(2.0, totalSt / 12.0);
            const double sr         = model.timePitch.stretchRatio > 0.001
                                    ? model.timePitch.stretchRatio : 1.0;

            const int   mode = static_cast<int>(model.timePitch.mode);
            double wsolaStretch = (mode == 0) ? 1.0 : sr / pitchRatio;
            wsolaStretch = juce::jlimit(0.1, 16.0, wsolaStretch);
            wsola.setStretchRatio(wsolaStretch);

            wsola.renderSegment(src.getReadPointer(ch), srcLen,
                                (int64_t)model.sourceStartSample,
                                model.sourceEndSample > 0
                                    ? model.sourceEndSample : (int64_t)srcLen,
                                result.getWritePointer(ch), outLen, ch);
        }

        return result;
    }

    void handleAsyncUpdate() override
    {
        juce::ScopedLock sl(resultLock_);
        if (hasPendingResult_ && onBounceComplete)
        {
            onBounceComplete(pendingModel_, std::move(pendingResult_));
            hasPendingResult_ = false;
        }
    }

    juce::ThreadPool   pool_;
    juce::CriticalSection resultLock_;
    juce::AudioBuffer<float> pendingResult_;
    ArrangementClipModel     pendingModel_;
    bool                     hasPendingResult_ = false;
};

} // namespace ArrangementEditor
