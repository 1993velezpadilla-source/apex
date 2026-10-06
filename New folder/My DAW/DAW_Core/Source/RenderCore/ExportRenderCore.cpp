#include "ExportRenderCore.h"
#include "../AppCore/ApplicationCore.h"
#include "../ClipCore/Clip.h"

namespace DAW {

ExportRenderCore::ExportRenderCore(ApplicationCore& appCore, ExportSettings settings)
    : juce::Thread("APEX WAV Export"),
      appCore_(appCore),
      settings_(std::move(settings))
{
}

ExportRenderCore::~ExportRenderCore()
{
    requestCancel();
    stopThread(5000);
}

bool ExportRenderCore::startExport(ProgressCallback progressCallback, CompletionCallback completionCallback)
{
    if (running_.exchange(true, std::memory_order_acq_rel))
        return false;

    progressCallback_ = std::move(progressCallback);
    completionCallback_ = std::move(completionCallback);
    cancelRequested.store(false, std::memory_order_release);
    startThread();
    return true;
}

void ExportRenderCore::requestCancel() noexcept
{
    cancelRequested.store(true, std::memory_order_release);
}

bool ExportRenderCore::isExportRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

void ExportRenderCore::run()
{
    auto result = render();
    running_.store(false, std::memory_order_release);

    if (completionCallback_)
        juce::MessageManager::callAsync([callback = completionCallback_, result]() mutable
        {
            callback(result);
        });
}

ExportRenderCore::Result ExportRenderCore::render()
{
    Result result;

    const double engineSampleRate = appCore_.getCurrentSampleRate();
    if (engineSampleRate <= 0.0)
    {
        result.message = "Audio engine is not prepared";
        return result;
    }

    const double requestedSampleRate = settings_.sampleRate > 0.0 ? settings_.sampleRate : engineSampleRate;
    if (std::abs(requestedSampleRate - engineSampleRate) > 0.001)
    {
        result.message = "Export sample rate must match engine sample rate in V1";
        return result;
    }

    if (!isSupportedBitDepth(settings_.bitDepth))
    {
        result.message = "Unsupported export bit depth";
        return result;
    }

    if (settings_.outputFile == juce::File{})
    {
        result.message = "No output file selected";
        return result;
    }

    const int blockSize = juce::jmax(16, settings_.blockSize);
    const int64_t detectedEnd = detectProjectEndSample(engineSampleRate);
    if (detectedEnd <= 0)
    {
        result.message = "Nothing to export";
        return result;
    }

    const int64_t startSample = juce::jmax<int64_t>(0, settings_.startSample);
    const int64_t endSample = settings_.endSample > startSample
        ? settings_.endSample
        : detectedEnd + (int64_t)std::llround(settings_.tailSeconds * engineSampleRate);

    if (endSample <= startSample)
    {
        result.message = "Nothing to export";
        return result;
    }

    struct TempFileGuard
    {
        juce::File f;
        bool committed = false;
        ~TempFileGuard() { if (!committed) f.deleteFile(); }
    } tempGuard { juce::File(makeTempPath(settings_.outputFile)) };

    auto& tempFile = tempGuard.f;
    tempFile.deleteFile();
    tempFile.getParentDirectory().createDirectory();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(tempFile.createOutputStream());
    if (!stream)
    {
        result.message = "Could not create export file";
        return result;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), engineSampleRate, 2, settings_.bitDepth, {}, 0));

    if (!writer)
    {
        result.message = "Could not create WAV writer";
        return result;
    }

    stream.release();

    struct OfflineRenderGuard
    {
        ApplicationCore& app;
        bool active = true;
        ~OfflineRenderGuard() { if (active) app.endOfflineRender(); }
        void endNow()
        {
            if (!active) return;
            active = false;
            app.endOfflineRender();
        }
    } offlineGuard { appCore_ };

    appCore_.beginOfflineRender(blockSize);

    juce::AudioBuffer<float> renderBuffer(2, blockSize);
    int64_t timelineSample = startSample;
    const int64_t totalSamples = endSample - startSample;

    while (timelineSample < endSample)
    {
        if (threadShouldExit() || cancelRequested.load(std::memory_order_acquire))
        {
            writer.reset();
            result.cancelled = true;
            result.message = "Export cancelled";
            return result;
        }

        const int numThisBlock = (int)juce::jmin<int64_t>(blockSize, endSample - timelineSample);
        renderBuffer.clear();

        if (!appCore_.renderOfflineBlock(renderBuffer, numThisBlock, timelineSample))
        {
            writer.reset();
            result.message = "Offline render failed";
            return result;
        }

        if (!writer->writeFromAudioSampleBuffer(renderBuffer, 0, numThisBlock))
        {
            writer.reset();
            result.message = "Could not write WAV data";
            return result;
        }

        timelineSample += numThisBlock;

        if (progressCallback_)
        {
            const float progress = juce::jlimit(0.0f, 1.0f, (float)((double)(timelineSample - startSample) / (double)totalSamples));
            juce::MessageManager::callAsync([callback = progressCallback_, progress]() mutable
            {
                callback(progress);
            });
        }
    }

    writer.reset();
    offlineGuard.endNow();

    if (settings_.outputFile.existsAsFile())
        settings_.outputFile.deleteFile();

    if (!tempFile.moveFileTo(settings_.outputFile))
    {
        result.message = "Could not replace final export file";
        return result;
    }

    tempGuard.committed = true;
    result.success = true;
    result.message = "Export complete";
    return result;
}

int64_t ExportRenderCore::detectProjectEndSample(double sampleRate) const
{
    juce::ignoreUnused(sampleRate);

    int64_t latestClipEnd = 0;
    auto& clipManager = appCore_.getClipManager();
    juce::ScopedLock lock(clipManager.getLock());

    for (auto* clip : clipManager.getAllClips())
    {
        if (clip == nullptr || clip->isMuted())
            continue;

        int64_t clipLength = (int64_t)clip->getLength();
        if (auto* audioClip = dynamic_cast<AudioClip*>(clip))
            clipLength = (int64_t)audioClip->getProcessedTimelineLength();

        latestClipEnd = juce::jmax(latestClipEnd, (int64_t)clip->getStartPosition() + clipLength);
    }

    return latestClipEnd;
}

bool ExportRenderCore::isSupportedBitDepth(int bitDepth) noexcept
{
    return bitDepth == 16 || bitDepth == 24 || bitDepth == 32;
}

juce::String ExportRenderCore::makeTempPath(const juce::File& outputFile)
{
    return outputFile.getFullPathName() + ".tmp";
}

} // namespace DAW
