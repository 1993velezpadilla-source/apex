// ===========================================================================
// OfflineHQTimePitchCore.h
// Render offline de alta calidad + cache thread-safe.
//
// POR QUÉ EXISTE:
//   Mode 6 (OfflineHQ) renderiza el clip completo en background.
//   El audio thread solo lee el buffer cacheado — nunca procesa stretch.
//
//   Flujo:
//     1. UI cambia parámetros → cache invalidada → background job encolado.
//     2. Background thread renderiza con OfflineBest quality.
//     3. Audio thread: si cache lista → lee cache.
//                     si no lista → fallback a Resample (Mode 0).
//
// THREAD SAFETY:
//   store(): solo background thread escribe.
//   getReadyBuffer(): audio thread llama tryGetBuffer() — sin bloqueo.
//   invalidate(): mensaje thread, atomic flag.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "TimePitchQualityCore.h"
#include "WSOLAStretchCore.h"
#include "PitchValueCore.h"
#include <JuceHeader.h>
#include <mutex>
#include <atomic>
#include <functional>

namespace ArrangementEditor
{

// ---------------------------------------------------------------------------
// OfflineHQTimePitchCore — cache store + offline render
// ---------------------------------------------------------------------------
class OfflineHQTimePitchCore
{
public:
    OfflineHQTimePitchCore() = default;

    // -----------------------------------------------------------------------
    // Audio thread: check and read cache (non-blocking, realtime-safe)
    // Returns nullptr if cache not ready or key mismatch.
    // Never acquires a mutex — uses atomic ready_ flag + snapshot key.
    // -----------------------------------------------------------------------
    const juce::AudioBuffer<float>* tryGetBuffer(const TimePitchCacheKey& key) const
    {
        // Fast path: atomic flag check before touching anything else.
        if (!ready_.load(std::memory_order_acquire))
            return nullptr;

        // Compare against the lock-free key snapshot published by store().
        // snapshotKey_ is written only under mutex on the worker thread, then
        // the ready_ flag is raised with release semantics — so any thread that
        // sees ready_==true also sees the fully written snapshotKey_.
        if (!(snapshotKey_ == key))
            return nullptr;

        // The buffer pointer itself is stable once ready_ is true (store() only
        // replaces it while ready_ is false and mutex is held). Safe to return.
        return &cachedBuffer_;
    }

    bool isReady(const TimePitchCacheKey& key) const
    {
        if (!ready_.load(std::memory_order_acquire)) return false;
        return snapshotKey_ == key;
    }

    // -----------------------------------------------------------------------
    // Invalidate — message thread, atomic
    // -----------------------------------------------------------------------
    void invalidate()
    {
        ready_.store(false);
    }

    // -----------------------------------------------------------------------
    // store — background thread writes completed render
    // -----------------------------------------------------------------------
    void store(const TimePitchCacheKey& key, juce::AudioBuffer<float>&& buf)
    {
        // Lower the ready flag first so the audio thread never reads a
        // partially replaced buffer. The release/acquire fence on ready_
        // guarantees that all writes to cachedBuffer_ and snapshotKey_ are
        // visible before the audio thread observes ready_==true.
        ready_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            currentKey_   = key;
            cachedBuffer_ = std::move(buf);
        }
        // Publish the key snapshot lock-free before raising ready_.
        snapshotKey_ = key;
        ready_.store(true, std::memory_order_release);
        if (onCacheReady) onCacheReady();
    }

    // -----------------------------------------------------------------------
    // renderOffline — called by TimePitchBackgroundRenderCore on worker thread.
    // Renders the full source segment using OfflineBest WSOLA quality.
    // Stores result in cache.
    //
    // Parameters:
    //   src         — full source audio buffer (all channels)
    //   key         — cache key (includes bounds, pitch, stretch, etc.)
    //   outputSampleRate — target output sample rate
    // -----------------------------------------------------------------------
    void renderOffline(const juce::AudioBuffer<float>& src,
                       const TimePitchCacheKey& key,
                       double outputSampleRate)
    {
        const int srcChannels = src.getNumChannels();
        const int srcStart    = (int)juce::jlimit((int64_t)0,
                                                   (int64_t)(src.getNumSamples() - 1),
                                                   key.sourceStartSample);
        const int srcEnd      = (int)juce::jlimit((int64_t)(srcStart + 1),
                                                   (int64_t)src.getNumSamples(),
                                                   key.sourceEndSample);
        const int srcLen      = srcEnd - srcStart;
        if (srcLen <= 0) return;

        // Calculate output length from stretch ratio only.
        // Bug 45 fix: the previous formula divided by pitchRatio:
        //   outLen = srcLen * stretchRatio / pitchRatio
        // This was wrong because the WSOLA engine here is set to stretchRatio
        // only — there is no separate resample stage in this offline path.
        // Dividing by pitchRatio produced a truncated buffer for pitch != 0,
        // causing the cache read to overrun the stored buffer end (UB/noise)
        // and cutting off the tail of pitched OfflineHQ renders.
        // The correct output length for a pure WSOLA stretch is:
        //   outLen = srcLen * stretchRatio
        const double stretchRatio = juce::jlimit(0.1, 16.0, key.stretchRatio);
        const int    outLen       = (int)std::round(srcLen * stretchRatio);
        if (outLen <= 0) return;

        juce::AudioBuffer<float> result(srcChannels, outLen);

        for (int ch = 0; ch < srcChannels; ++ch)
        {
            // Create per-channel WSOLA engine at OfflineBest quality
            WSOLAStretchCore wsola;
            wsola.prepare(outputSampleRate, outLen, TimePitchQuality::OfflineBest);
            wsola.setStretchRatio(stretchRatio);
            wsola.renderSegment(src.getReadPointer(ch), src.getNumSamples(),
                                srcStart, srcEnd,
                                result.getWritePointer(ch), outLen, ch);
        }

        store(key, std::move(result));
    }

    // Callback fired when cache is stored — connect to UI to show ready indicator
    std::function<void()> onCacheReady;

private:
    mutable std::mutex        mutex_;
    juce::AudioBuffer<float>  cachedBuffer_;
    TimePitchCacheKey         currentKey_;
    // Lock-free key snapshot for audio-thread reads — written before ready_
    // is raised, read without a mutex by tryGetBuffer().
    TimePitchCacheKey         snapshotKey_;
    std::atomic<bool>         ready_ { false };
};

} // namespace ArrangementEditor
