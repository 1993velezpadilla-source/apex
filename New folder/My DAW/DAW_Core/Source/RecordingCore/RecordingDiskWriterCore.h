#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * RecordingDiskWriterCore — streaming, crash-safe per-track WAV writer.
 *
 * One instance per active recording (per armed track). Wraps JUCE's
 * AudioFormatWriter::ThreadedWriter so audio-thread pushSamples() is
 * lock-free and a background thread handles all disk I/O.
 */
class RecordingDiskWriterCore
{
public:
    struct DiagnosticsSnapshot
    {
        int64_t pushAttempts = 0;
        int64_t pushAccepted = 0;
        int64_t pushRejected = 0;
        int64_t pushSamplesAttempted = 0;
        int64_t pushSamplesAccepted = 0;
        int64_t pushSamplesRejected = 0;
    };

    RecordingDiskWriterCore() = default;

    ~RecordingDiskWriterCore()
    {
        stop();
    }

    bool start(const juce::File& outFile,
               double sampleRate,
               int numChannels,
               int bitDepth,
               juce::TimeSliceThread& diskThread)
    {
        stop();

        outputFile_ = outFile;
        outputFile_.deleteFile();

        auto* outStream = outputFile_.createOutputStream().release();
        if (outStream == nullptr) return false;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(outStream, sampleRate,
                                (unsigned int) numChannels, bitDepth, {}, 0));
        if (writer == nullptr)
        {
            delete outStream;
            return false;
        }

        threadedWriter_.reset(
            new juce::AudioFormatWriter::ThreadedWriter(
                writer.release(), diskThread, 32768));

        const auto flushSamples = (int) juce::jlimit(1.0, 2147483647.0, sampleRate);
        threadedWriter_->setFlushInterval(flushSamples);

        samplesWritten_.store(0, std::memory_order_relaxed);
        overrun_.store(false, std::memory_order_relaxed);
        pushAttempts_.store(0, std::memory_order_relaxed);
        pushAccepted_.store(0, std::memory_order_relaxed);
        pushRejected_.store(0, std::memory_order_relaxed);
        pushSamplesAttempted_.store(0, std::memory_order_relaxed);
        pushSamplesAccepted_.store(0, std::memory_order_relaxed);
        pushSamplesRejected_.store(0, std::memory_order_relaxed);
        active_.store(true, std::memory_order_release);
        return true;
    }

    void stop()
    {
        const bool wasActive = active_.exchange(false, std::memory_order_acq_rel);
        if (threadedWriter_ != nullptr)
        {
            threadedWriter_.reset();
            if (wasActive)
                juce::Thread::sleep(20);
        }
    }

    bool pushSamples(const float* const* channelData, int numSamples) noexcept
    {
        if (!active_.load(std::memory_order_acquire)) return false;
        if (threadedWriter_ == nullptr) return false;

        pushAttempts_.fetch_add(1, std::memory_order_relaxed);
        pushSamplesAttempted_.fetch_add(numSamples, std::memory_order_relaxed);
        const bool ok = threadedWriter_->write(channelData, numSamples);
        if (ok)
        {
            pushAccepted_.fetch_add(1, std::memory_order_relaxed);
            pushSamplesAccepted_.fetch_add(numSamples, std::memory_order_relaxed);
            samplesWritten_.fetch_add(numSamples, std::memory_order_relaxed);
        }
        else
        {
            pushRejected_.fetch_add(1, std::memory_order_relaxed);
            pushSamplesRejected_.fetch_add(numSamples, std::memory_order_relaxed);
            overrun_.store(true, std::memory_order_relaxed);
        }

        return ok;
    }

    void flushToDisk() noexcept
    {
    }

    bool isActive()     const noexcept { return active_.load(std::memory_order_acquire); }
    bool hasOverrun()   const noexcept { return overrun_.load(std::memory_order_relaxed); }
    void clearOverrun() noexcept       { overrun_.store(false, std::memory_order_relaxed); }
    int  getSamplesWritten() const noexcept { return samplesWritten_.load(std::memory_order_relaxed); }
    const juce::File& getOutputFile() const noexcept { return outputFile_; }
    DiagnosticsSnapshot getDiagnosticsSnapshot() const noexcept
    {
        DiagnosticsSnapshot snapshot;
        snapshot.pushAttempts = pushAttempts_.load(std::memory_order_relaxed);
        snapshot.pushAccepted = pushAccepted_.load(std::memory_order_relaxed);
        snapshot.pushRejected = pushRejected_.load(std::memory_order_relaxed);
        snapshot.pushSamplesAttempted = pushSamplesAttempted_.load(std::memory_order_relaxed);
        snapshot.pushSamplesAccepted = pushSamplesAccepted_.load(std::memory_order_relaxed);
        snapshot.pushSamplesRejected = pushSamplesRejected_.load(std::memory_order_relaxed);
        return snapshot;
    }

private:
    juce::File                                               outputFile_;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
    std::atomic<bool>                                        active_         { false };
    std::atomic<bool>                                        overrun_        { false };
    std::atomic<int>                                         samplesWritten_ { 0 };
    std::atomic<int64_t>                                     pushAttempts_ { 0 };
    std::atomic<int64_t>                                     pushAccepted_ { 0 };
    std::atomic<int64_t>                                     pushRejected_ { 0 };
    std::atomic<int64_t>                                     pushSamplesAttempted_ { 0 };
    std::atomic<int64_t>                                     pushSamplesAccepted_ { 0 };
    std::atomic<int64_t>                                     pushSamplesRejected_ { 0 };

    JUCE_DECLARE_NON_COPYABLE(RecordingDiskWriterCore)
};

} // namespace DAW
