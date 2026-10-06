// ===========================================================================
// TimePitchBackgroundRenderCore.h
// Background render queue for OfflineHQ cache generation.
//
// POR QUÉ EXISTE:
//   El render offline es demasiado costoso para el audio thread.
//   Este núcleo encola jobs, los ejecuta en un thread separado,
//   y nunca bloquea el audio thread.
//
// THREAD MODEL:
//   - Message thread: enqueue() / cancelAll()
//   - Worker thread: procesa jobs en orden LIFO (último cambio = más urgente)
//   - Audio thread: nunca toca esta clase
//
// SAFETY:
//   - Si llega un job con la misma clipId, reemplaza el anterior.
//   - Si el parámetro cambia antes de que el job empiece, se descarta el viejo.
//   - juce::ThreadPool maneja el lifecycle del thread.
// ===========================================================================
#pragma once
#include "OfflineHQTimePitchCore.h"
#include "TimePitchTypesCore.h"
#include <JuceHeader.h>
#include <map>
#include <mutex>
#include <functional>

namespace ArrangementEditor
{

struct TimePitchRenderJob
{
    juce::String            clipId;
    TimePitchCacheKey       key;
    juce::AudioBuffer<float> sourceBuffer; // copy of source (owned by job)
    double                  outputSampleRate = 44100.0;
    OfflineHQTimePitchCore* cache = nullptr;
};

// ---------------------------------------------------------------------------
// TimePitchBackgroundRenderCore
// ---------------------------------------------------------------------------
class TimePitchBackgroundRenderCore
{
public:
    TimePitchBackgroundRenderCore()
        : pool_(1) // single background thread (serial, low-priority)
    {}

    ~TimePitchBackgroundRenderCore()
    {
        pool_.removeAllJobs(true, 2000);
    }

    // -----------------------------------------------------------------------
    // enqueue — message thread.
    // If a job for the same clipId already exists in the queue,
    // it is replaced (only latest params matter).
    // -----------------------------------------------------------------------
    void enqueue(TimePitchRenderJob job)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            pendingJobs_[job.clipId] = std::move(job);
        }
        // Kick the worker if idle
        if (pool_.getNumJobs() == 0)
            pool_.addJob([this] { processNext(); }, false);
    }

    // -----------------------------------------------------------------------
    // cancelAll — message thread.
    // Removes all pending jobs. In-flight job finishes naturally.
    // -----------------------------------------------------------------------
    void cancelAll()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pendingJobs_.clear();
    }

    void cancelClip(const juce::String& clipId)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pendingJobs_.erase(clipId);
    }

private:
    void processNext()
    {
        // Drain the pending map one job at a time
        while (true)
        {
            TimePitchRenderJob job;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (pendingJobs_.empty()) break;
                auto it = pendingJobs_.begin();
                job = std::move(it->second);
                pendingJobs_.erase(it);
            }

            if (job.cache && job.sourceBuffer.getNumSamples() > 0)
            {
                job.cache->renderOffline(job.sourceBuffer, job.key,
                                         job.outputSampleRate);
                if (job.cache->onCacheReady)
                    job.cache->onCacheReady();
            }
        }
    }

    juce::ThreadPool pool_;
    std::mutex mutex_;
    std::map<juce::String, TimePitchRenderJob> pendingJobs_;
};

} // namespace ArrangementEditor
