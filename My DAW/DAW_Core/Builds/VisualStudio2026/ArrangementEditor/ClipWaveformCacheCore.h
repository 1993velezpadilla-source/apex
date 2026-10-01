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
                m_targetWidth = targetWidth;
            }
            if (startWorker)
                startThread(juce::Thread::Priority::background);
        }

        // Trigger async peak update for current source/buffer if not already running.
        // Safe to call from UI thread; defers if thread already running.
        void requestPeaksUpdate()
        {
            if (isThreadRunning())
                return;
            if (!m_requestPending && m_sourcePath.empty() && !hasPendingBuffer_)
                return;
            m_requestPending = true;
            startThread(juce::Thread::Priority::background);
        }

        bool hasPendingBuffer()
        {
            juce::ScopedLock sl(bufferLock_);
            return hasPendingBuffer_;
        }

        // Get cached peaks (call from UI thread)
        // THREAD-SAFE: Must be called with peaksLock_ held
        const WaveformPeakData& getPeaks() const { return m_peaks; }
        bool isReady() const
        {
            juce::CriticalSection::ScopedLockType lock(peaksLock_);
            return m_peaks.valid;
        }

        // Lock for thread-safe access to m_peaks during UI paint and background thread completion
        juce::CriticalSection& getPeaksLock() { return peaksLock_; }

        // Callbacks
        std::function<void()> onPeaksReady;

    private:
        void run() override;

        std::string      m_sourcePath;
        int              m_targetWidth = 0;
        int64_t          m_visibleSamplesHint = 0;
        WaveformPeakData m_peaks;
        DAW::AudioFileManager::CachedAudioHandle cachedOverviewSource_;
        bool             m_requestPending = false;

        // For buffer-based peak generation
        juce::CriticalSection    bufferLock_;
        juce::AudioBuffer<float> pendingBuffer_;
        bool                     hasPendingBuffer_ = false;
        std::shared_ptr<void>    lifetimeToken_;

        // Thread safety: protects m_peaks from simultaneous access by UI thread (paint)
        // and background thread (run/generatePeaks)
        mutable juce::CriticalSection peaksLock_;

        void generatePeaks();
        void generatePeaksFromBuffer();
        void readAudioFile(const juce::File& file);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipWaveformCacheCore)
    };

} // namespace ArrangementEditor
