// ===========================================================================
// ClipWaveformCacheCore.cpp
// ===========================================================================
#include "ClipWaveformCacheCore.h"

namespace ArrangementEditor
{
    ClipWaveformCacheCore::ClipWaveformCacheCore()
        : juce::Thread("WaveformCache")
        , lifetimeToken_(std::make_shared<int>(0))
    {
        startThread(juce::Thread::Priority::low);
    }

    ClipWaveformCacheCore::~ClipWaveformCacheCore()
    {
        lifetimeToken_.reset();
        signalThreadShouldExit();
        notify(); // wake the wait(500) so thread exits promptly
        stopThread(500);
    }

    // -----------------------------------------------------------------------
    // Zoom-adaptive peak resolution.
    // Picks a samplesPerPeak so the *visible* portion of the source always
    // has >= 2 peak columns per displayed pixel (pro-DAW min/max accuracy),
    // bounded by a fine-zoom floor and a global memory cap per clip.
    // -----------------------------------------------------------------------
    static int computeSamplesPerPeak(juce::int64 totalSamples, int targetWidth,
                                     int64_t visibleSamplesHint)
    {
        const auto visible = visibleSamplesHint > 0
            ? juce::jlimit((juce::int64)1, totalSamples, (juce::int64)visibleSamplesHint)
            : totalSamples;

        const auto wantedColumns = (juce::int64)juce::jmax(1, targetWidth) * 2;
        juce::int64 spp = visible / juce::jmax((juce::int64)1, wantedColumns);

        // Never coarser than the legacy whole-file baseline (8192 columns)
        spp = juce::jmin(spp, juce::jmax((juce::int64)1, totalSamples / 8192));

        // Fine-zoom floor + memory cap (~2M columns per clip)
        constexpr juce::int64 kMinSamplesPerPeak = 16;
        constexpr juce::int64 kMaxPeakColumns    = 2000000;
        spp = juce::jmax(spp, kMinSamplesPerPeak);
        spp = juce::jmax(spp, totalSamples / kMaxPeakColumns);

        return (int)juce::jmax((juce::int64)1, spp);
    }

    void ClipWaveformCacheCore::requestPeaks(const std::string& sourcePath, int targetWidth,
                                             int64_t visibleSamplesHint)
    {
        if (m_sourcePath == sourcePath && m_peaks.valid)
        {
            m_targetWidth = targetWidth;
            m_visibleSamplesHint = visibleSamplesHint;

            // Re-generate only when the cache is meaningfully coarser than
            // this zoom level needs (1.5x hysteresis prevents regen thrash).
            const int required = computeSamplesPerPeak(m_peaks.totalSamples,
                                                       targetWidth, visibleSamplesHint);
            if ((juce::int64)m_peaks.samplesPerPeak <= (juce::int64)required * 3 / 2)
                return; // cached resolution is fine for this zoom

            m_requestPending = true;
            notify();
            return;
        }

        // New source: invalidate immediately so stale audio never paints.
        if (m_sourcePath != sourcePath)
            m_peaks.valid = false;

        m_sourcePath = sourcePath;
        m_targetWidth = targetWidth;
        m_visibleSamplesHint = visibleSamplesHint;
        m_requestPending = true;
        notify();
    }

    void ClipWaveformCacheCore::run()
    {
        while (!threadShouldExit())
        {
            wait(500);

            // Buffer-based request (from OfflineHQ cache / bounce) takes priority
            {
                juce::ScopedLock sl(bufferLock_);
                if (hasPendingBuffer_ && !threadShouldExit())
                {
                    hasPendingBuffer_ = false;
                    generatePeaksFromBuffer();

                    if (onPeaksReady)
                    {
                        std::weak_ptr<void> weak = lifetimeToken_;
                        const juce::String filePath("buffer");
                        juce::MessageManager::callAsync([weak, this, filePath]() {
                            if (weak.lock())
                            {
                                DBG("[CRASH TRACE] action=waveform_async_complete file=" << filePath);
                                if (onPeaksReady) onPeaksReady();
                            }
                        });
                    }
                    continue;
                }
            }

            if (m_requestPending && !threadShouldExit())
            {
                m_requestPending = false;
                generatePeaks();

                if (onPeaksReady)
                {
                    std::weak_ptr<void> weak = lifetimeToken_;
                    const juce::String filePath(m_sourcePath);
                    juce::MessageManager::callAsync([weak, this, filePath]() {
                        if (weak.lock())
                        {
                            DBG("[CRASH TRACE] action=waveform_async_complete file=" << filePath);
                            if (onPeaksReady) onPeaksReady();
                        }
                    });
                }
            }
        }
    }

    void ClipWaveformCacheCore::generatePeaks()
    {
        juce::File file(m_sourcePath);
        if (!file.existsAsFile())
        {
            juce::CriticalSection::ScopedLockType lock(peaksLock_);
            m_peaks.valid = false;
            return;
        }

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (!reader)
        {
            juce::CriticalSection::ScopedLockType lock(peaksLock_);
            m_peaks.valid = false;
            return;
        }

        juce::int64 totalSamples = reader->lengthInSamples;
        if (totalSamples <= 0 || m_targetWidth <= 0)
        {
            juce::CriticalSection::ScopedLockType lock(peaksLock_);
            m_peaks.valid = false;
            return;
        }

        // Build into a local struct so the previous cache stays displayable
        // during regeneration (no waveform flicker while zooming).
        WaveformPeakData fresh;
        fresh.totalSamples = (int64_t)totalSamples;
        fresh.sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;

        const int samplesPerPeak = computeSamplesPerPeak(totalSamples, m_targetWidth,
                                                         m_visibleSamplesHint);
        fresh.samplesPerPeak = samplesPerPeak;

        int numPeaks = (int)std::ceil((double)totalSamples / (double)samplesPerPeak);
        fresh.minPeaks.resize(numPeaks, 0.f);
        fresh.maxPeaks.resize(numPeaks, 0.f);

        // Read in large sequential blocks (fast) rather than per-peak reads.
        const int peaksPerBlock = juce::jmax(1, 65536 / samplesPerPeak);
        const int blockSamples  = peaksPerBlock * samplesPerPeak;
        juce::AudioBuffer<float> buffer((int)reader->numChannels, blockSamples);

        for (int blockStart = 0; blockStart < numPeaks && !threadShouldExit(); blockStart += peaksPerBlock)
        {
            const juce::int64 startSample = (juce::int64)blockStart * samplesPerPeak;
            const int samplesRead = (int)juce::jmin((juce::int64)blockSamples, totalSamples - startSample);
            if (samplesRead <= 0)
                break;

            reader->read(&buffer, 0, samplesRead, startSample, true, true);

            const int blockPeaks = juce::jmin(peaksPerBlock, numPeaks - blockStart);
            for (int p = 0; p < blockPeaks; ++p)
            {
                const int offset = p * samplesPerPeak;
                const int len    = juce::jmin(samplesPerPeak, samplesRead - offset);
                if (len <= 0)
                    break;

                float minVal = 0.f;
                float maxVal = 0.f;
                for (int ch = 0; ch < (int)reader->numChannels; ++ch)
                {
                    auto range = buffer.findMinMax(ch, offset, len);
                    minVal = juce::jmin(minVal, range.getStart());
                    maxVal = juce::jmax(maxVal, range.getEnd());
                }

                fresh.minPeaks[blockStart + p] = minVal;
                fresh.maxPeaks[blockStart + p] = maxVal;
            }
        }

        if (threadShouldExit())
            return;

        // Thread-safe peak update: lock while swapping in the new data
        // This ensures paint() never accesses partially-written peak data
        {
            juce::CriticalSection::ScopedLockType lock(peaksLock_);
            m_peaks.valid = false;
            fresh.valid = true;
            m_peaks = std::move(fresh);
        }
    }

    // -----------------------------------------------------------------------
    // generatePeaksFromBuffer — extract peaks from a pre-rendered AudioBuffer.
    // Called after OfflineHQ render completes or clip is bounced.
    // Shows the PROCESSED waveform (with stretch applied visually).
    // -----------------------------------------------------------------------
    void ClipWaveformCacheCore::generatePeaksFromBuffer()
    {
        juce::ScopedLock sl(bufferLock_);
        const int totalSamples = pendingBuffer_.getNumSamples();
        const int channels     = pendingBuffer_.getNumChannels();
        if (totalSamples <= 0 || channels <= 0 || m_targetWidth <= 0)
            return;

        // Build peaks in local struct first, then lock when updating m_peaks
        WaveformPeakData fresh;
        fresh.totalSamples = totalSamples;

        const int samplesPerPeak = juce::jmax(1, totalSamples / m_targetWidth);
        fresh.samplesPerPeak   = samplesPerPeak;
        const int numPeaks       = totalSamples / samplesPerPeak;

        fresh.minPeaks.resize(numPeaks, 0.f);
        fresh.maxPeaks.resize(numPeaks, 0.f);

        for (int i = 0; i < numPeaks && !threadShouldExit(); ++i)
        {
            const int start = i * samplesPerPeak;
            const int len   = juce::jmin(samplesPerPeak, totalSamples - start);

            float minVal = 0.f, maxVal = 0.f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto range = pendingBuffer_.findMinMax(ch, start, len);
                minVal = juce::jmin(minVal, range.getStart());
                maxVal = juce::jmax(maxVal, range.getEnd());
            }
            fresh.minPeaks[i] = minVal;
            fresh.maxPeaks[i] = maxVal;
        }

        // Thread-safe update: lock while swapping in the new data
        {
            juce::CriticalSection::ScopedLockType lock(peaksLock_);
            fresh.valid = true;
            m_peaks = std::move(fresh);
        }
    }

} // namespace ArrangementEditor
