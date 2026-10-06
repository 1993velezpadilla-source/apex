// ===========================================================================
// TimePitchCPUSafetyCore.h
// CPU safety: max realtime stretch clips, auto-fallback to Draft, pre-render.
//
// POR QUÉ EXISTE:
//   WSOLA y granular tienen costo O(windowSize * hopSyn * clips).
//   Con 50 clips en stretch en una sesión grande, el audio thread puede
//   sobrecargarse. Este núcleo:
//
//   1. Cuenta clips con procesamiento activo (no Resample, no identity).
//   2. Si superan el límite, fuerza Draft quality en todos.
//   3. Si hay CPU spike (callback time > threshold), reduce calidad.
//   4. Cuando playback está detenido, encola pre-renders en background.
//
// INTEGRATION:
//   AudioEngine llama TimePitchCPUSafetyCore::notifyBlockStart(time) al inicio
//   de cada audio callback, y notifyBlockEnd(time) al final.
//   El coste medido se usa para ajustar calidad del siguiente bloque.
//
// AUDIO THREAD SAFETY:
//   notifyBlockStart/End son audio-thread safe (solo atomics).
//   Las decisiones de calidad se leen por atomic en audio thread.
//   El cambio de calidad se ejecuta en message thread (no en audio thread).
// ===========================================================================
#pragma once
#include "TimePitchQualityCore.h"
#include <JuceHeader.h>
#include <atomic>
#include <functional>

namespace ArrangementEditor
{

class TimePitchCPUSafetyCore
{
public:
    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------
    struct Config
    {
        int    maxRealtimeStretchClips = 24;   // above this: force Draft
        double cpuSpikeThresholdMs    = 8.0;   // > 8ms callback → reduce quality
        double cpuRecoverThresholdMs  = 3.0;   // < 3ms for 2s → restore quality
        double sampleRate             = 44100.0;
        int    blockSize              = 512;
    };

    TimePitchCPUSafetyCore() = default;

    void setConfig(const Config& cfg) { config_ = cfg; }

    // -----------------------------------------------------------------------
    // notifyActiveStretchClips — message thread, before each block cycle.
    // AudioEngine counts how many clips are using non-Resample modes.
    // -----------------------------------------------------------------------
    void notifyActiveStretchClips(int count)
    {
        activeStretchClips_.store(count);

        // Immediate decision: too many → force Draft
        if (count > config_.maxRealtimeStretchClips)
        {
            if (currentQuality_.load() != static_cast<int>(TimePitchQuality::Draft))
            {
                currentQuality_.store(static_cast<int>(TimePitchQuality::Draft));
                if (onQualityForced) onQualityForced(TimePitchQuality::Draft,
                    "Too many realtime stretch clips. Forcing Draft quality.");
            }
        }
    }

    // -----------------------------------------------------------------------
    // notifyBlockStart / notifyBlockEnd — audio thread timing.
    // Measures callback duration to detect CPU spikes.
    // -----------------------------------------------------------------------
    void notifyBlockStart()
    {
        blockStartTick_.store(juce::Time::getHighResolutionTicks());
    }

    void notifyBlockEnd()
    {
        const juce::int64 now   = juce::Time::getHighResolutionTicks();
        const juce::int64 start = blockStartTick_.load();
        const double ms = juce::Time::highResolutionTicksToSeconds(now - start) * 1000.0;
        lastBlockMs_.store(ms);

        // Spike detection: reduce quality if callback too slow
        if (ms > config_.cpuSpikeThresholdMs)
        {
            ++spikeCount_;
            if (spikeCount_ >= 3) // 3 consecutive spikes
            {
                spikeCount_ = 0;
                forceDowngrade();
            }
        }
        else
        {
            spikeCount_ = 0;
            // Accumulate recovery time
            recoverAccumMs_ += ms;
            // After 2 seconds of clean callbacks, allow upgrade
            const double blockDurationMs = (config_.blockSize / config_.sampleRate) * 1000.0;
            const double recoverThresholdMs = 2000.0;
            if (recoverAccumMs_ > recoverThresholdMs)
            {
                recoverAccumMs_ = 0.0;
                tryUpgrade();
            }
        }
    }

    // -----------------------------------------------------------------------
    // shouldPreRenderInBackground — call when transport is stopped.
    // Returns true if there are clips that would benefit from background render.
    // -----------------------------------------------------------------------
    bool shouldPreRenderInBackground() const
    {
        return activeStretchClips_.load() > 0;
    }

    // -----------------------------------------------------------------------
    // Getters for UI display
    // -----------------------------------------------------------------------
    TimePitchQuality currentForcedQuality() const
    {
        return static_cast<TimePitchQuality>(currentQuality_.load());
    }

    double lastBlockMs()         const { return lastBlockMs_.load(); }
    int    activeStretchClips()  const { return activeStretchClips_.load(); }
    bool   isCPULimited()        const { return cpuLimited_.load(); }

    // Callbacks — connect to DSP system on message thread
    std::function<void(TimePitchQuality, const juce::String&)> onQualityForced;
    std::function<void(TimePitchQuality)>                      onQualityRestored;

private:
    void forceDowngrade()
    {
        const int cur = currentQuality_.load();
        if (cur > static_cast<int>(TimePitchQuality::Draft))
        {
            const int next = cur - 1;
            currentQuality_.store(next);
            cpuLimited_.store(true);
            if (onQualityForced)
                onQualityForced(static_cast<TimePitchQuality>(next),
                                "CPU spike detected. Reducing stretch quality.");
        }
    }

    void tryUpgrade()
    {
        const int cur = currentQuality_.load();
        // Only upgrade up to Realtime (not High/OfflineBest for realtime)
        if (cpuLimited_.load() &&
            cur < static_cast<int>(TimePitchQuality::Realtime))
        {
            const int next = cur + 1;
            currentQuality_.store(next);
            if (next >= static_cast<int>(TimePitchQuality::Realtime))
                cpuLimited_.store(false);
            if (onQualityRestored)
                onQualityRestored(static_cast<TimePitchQuality>(next));
        }
    }

    Config config_;

    std::atomic<int>    activeStretchClips_ { 0 };
    std::atomic<int>    currentQuality_ { static_cast<int>(TimePitchQuality::Realtime) };
    std::atomic<juce::int64> blockStartTick_ { 0 };
    std::atomic<double> lastBlockMs_    { 0.0 };
    std::atomic<bool>   cpuLimited_     { false };

    int    spikeCount_      = 0;
    double recoverAccumMs_  = 0.0;
};

} // namespace ArrangementEditor
