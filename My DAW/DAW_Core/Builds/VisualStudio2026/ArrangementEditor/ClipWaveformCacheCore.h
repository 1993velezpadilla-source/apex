// ===========================================================================
// ClipWaveformCacheCore.h
// Async waveform peak data generator.
// Reads source file, generates peak data at multiple zoom levels.
// Never blocks the UI thread.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include "../../../Source/AudioEngineCore/AudioFileManager.h"

namespace ArrangementEditor
{
    struct WaveformPeakData
    {
        std::vector<float> minPeaks;
        std::vector<float> maxPeaks;
        DAW::AudioFileManager::CachedAudioHandle overview;

        const std::vector<float>& minimums() const noexcept
        {
            return overview ? overview->getRawMinRef() : minPeaks;
        }

        const std::vector<float>& maximums() const noexcept
        {
            return overview ? overview->getRawMaxRef() : maxPeaks;
        }

        /** Peak-bucket indices whose source samples reached 0 dBFS
            (|sample| >= 1). Sorted ascending; used to draw the clipped-
            position markers on the clip. */
        std::vector<int> clippedPeaks;
        int samplesPerPeak = 256;
        int64_t totalSamples = 0;
        double sourceSampleRate = 44100.0;
        bool valid = false;
    };

    class ClipWaveformCacheCore : public juce::Thread
    {
    public:
        ClipWaveformCacheCore();
        ~ClipWaveformCacheCore() override;

        // Request peak data generation for a source file.
        // visibleSamplesHint = number of source samples the clip currently
        // displays (trimmed segment). Used to pick a zoom-appropriate peak
        // resolution; pass 0 when unknown (falls back to whole file).
        void requestPeaks(const std::string& sourcePath, int targetWidth,
                          int64_t visibleSamplesHint = 0);

        /** Adopt AudioFileManager's already-built bounded overview. This avoids
            a second source decode and a second full-PCM scan per visual clip. */
        bool setPeaksFromCachedOverview(const DAW::AudioFileManager::CachedAudioHandle& cachedAudio);

        // Request peak generation from a pre-rendered (bounced/frozen) buffer.
        // Used when OfflineHQ cache completes or clip is bounced.
        // Runs peak extraction on a background thread, then fires onPeaksReady.
        void requestPeaksFromBuffer(const juce::AudioBuffer<float>& processedBuffer,
                                     int targetWidth,
                                     bool startWorker = true)
        {
            // Capture buffer on message thread (copy)
            {
                juce::ScopedLock sl(bufferLock_);
                pendingBuffer_.makeCopyOf(processedBuffer);
                hasPendingBuffer_ = true;
                pendingBufferWidth_ = targetWidth;
            }
            if (startWorker)
            {
                startThread(juce::Thread::Priority::background);
                notify();
            }
        }

        // Wake the worker when an offscreen buffer becomes drawable.
        void requestPeaksUpdate()
        {
            if (isThreadRunning())
            {
                notify();
                return;
            }
            if (!m_requestPending && m_sourcePath.empty() && !hasPendingBuffer())
                return;
            m_requestPending = true;
            startThread(juce::Thread::Priority::background);
            notify();
        }

        bool hasPendingBuffer()
        {
            juce::ScopedLock sl(bufferLock_);
            return hasPendingBuffer_;
        }

        // Get cached peaks (call from UI thread)
        // THREAD-SAFE: Must be called with peaksLock_ held
        const WaveformPeakData& getPeaks() const
        {
            // Legacy callers must hold peaksLock_ for this reference.
            return m_peaks ? *m_peaks : emptyPeaks();
        }

        // Copy shared ownership under a tiny lock, then render with no peaks
        // mutex held. Background peak updates atomically replace this pointer,
        // never mutate peak arrays while the UI is reading them.
        std::shared_ptr<const WaveformPeakData> getPeaksSnapshot() const
        {
            juce::CriticalSection::ScopedLockType lock(peaksLock_);
            return m_peaks;
        }

        bool isReady() const
        {
            juce::CriticalSection::ScopedLockType lock(peaksLock_);
            return m_peaks && m_peaks->valid;
        }

        // Lock for thread-safe access to m_peaks during UI paint and background thread completion
        juce::CriticalSection& getPeaksLock() { return peaksLock_; }

        // Callbacks
        std::function<void()> onPeaksReady;

    private:
        static const WaveformPeakData& emptyPeaks() noexcept
        {
            static const WaveformPeakData empty;
            return empty;
        }

        void run() override;

        std::string      m_sourcePath;
        int              m_targetWidth = 0;
        int64_t          m_visibleSamplesHint = 0;
        std::shared_ptr<const WaveformPeakData> m_peaks;
        DAW::AudioFileManager::CachedAudioHandle cachedOverviewSource_;
        bool             m_requestPending = false;

        // For buffer-based peak generation
        juce::CriticalSection    bufferLock_;
        juce::AudioBuffer<float> pendingBuffer_;
        int                      pendingBufferWidth_ = 0;
        bool                     hasPendingBuffer_ = false;
        std::shared_ptr<void>    lifetimeToken_;

        // Thread safety: protects m_peaks from simultaneous access by UI thread (paint)
        // and background thread (run/generatePeaks)
        mutable juce::CriticalSection peaksLock_;

        void generatePeaks();
        bool generatePendingBufferPeaks();
        void readAudioFile(const juce::File& file);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipWaveformCacheCore)
    };

} // namespace ArrangementEditor
