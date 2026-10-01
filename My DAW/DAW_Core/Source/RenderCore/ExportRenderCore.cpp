#include "ExportRenderCore.h"
#include "ExportPublicationCore.h"
#include "ExportThreadStartStateCore.h"
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
    prepareForShutdown();

    constexpr int diagnosticIntervalMs = 5000;
    while (!waitForThreadToExit(diagnosticIntervalMs))
    {
        juce::Logger::writeToLog(
            "[EXPORT] Cooperative shutdown is still waiting for the render worker; "
            "an in-process plugin may be blocked. Render dependencies remain alive and close will keep waiting.");
    }
}

bool ExportRenderCore::startExport(CompletionCallback completionCallback)
{
    if (running_.exchange(true, std::memory_order_acq_rel))
        return false;

    completionCallback_ = std::move(completionCallback);
    progress_.store(0.0f, std::memory_order_release);
    shutdownState_.beginExport();
    const auto startOutcome = ExportThreadStartStateCore::resolve(startThread());
    if (startOutcome.releaseOwnership)
    {
        running_.store(false, std::memory_order_release);
        shutdownState_.prepareForShutdown();
        completionState_->suppress();
        completionCallback_ = {};
    }
    return startOutcome.running;
}

void ExportRenderCore::requestCancel() noexcept
{
    shutdownState_.requestCancellation();
    signalThreadShouldExit();
    notify();
}

void ExportRenderCore::prepareForShutdown() noexcept
{
    shutdownState_.prepareForShutdown();
    completionState_->suppress();
    signalThreadShouldExit();
    notify();
}

bool ExportRenderCore::isExportRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

float ExportRenderCore::getProgress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

juce::String ExportRenderCore::getProgressDetail() const
{
    const juce::ScopedLock lock(progressDetailLock_);
    return progressDetail_;
}

void ExportRenderCore::setProgressDetail(const juce::String& detail)
{
    const juce::ScopedLock lock(progressDetailLock_);
    progressDetail_ = detail;
}

bool ExportRenderCore::dispatchPendingCompletion()
{
    Result result;
    if (!completionState_->consume(result))
        return false;

    if (completionCallback_)
        completionCallback_(std::move(result));
    return true;
}

void ExportRenderCore::run()
{
    auto result = render();
    const bool completionPublished = completionState_->publish(result);
    running_.store(false, std::memory_order_release);

    if (completionPublished && shutdownState_.shouldPostCallbacks() && completionCallback_)
    {
        auto completionState = completionState_;
        auto callback = completionCallback_;
        const bool posted = juce::MessageManager::callAsync(
            [completionState = std::move(completionState), callback = std::move(callback)]() mutable
        {
            Result postedResult;
            if (completionState->consume(postedResult))
                callback(std::move(postedResult));
        });
        if (!posted)
            juce::Logger::writeToLog(
                "[EXPORT] completion post rejected; message-thread polling will deliver the pending result");
    }
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

    if (!isSupportedBitDepth(settings_.format, settings_.bitDepth))
    {
        result.message = "Unsupported export bit depth";
        return result;
    }

    if (settings_.content == ExportContent::FullMix && settings_.outputFile == juce::File{})
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

    return settings_.content == ExportContent::Stems
        ? renderStems(engineSampleRate, blockSize, startSample, endSample)
        : renderFullMix(engineSampleRate, blockSize, startSample, endSample);
}

namespace
{
    struct OfflineRenderGuard
    {
        ApplicationCore& app;
        bool active = false;
        ~OfflineRenderGuard() { if (active) app.endOfflineRender(); }
        void endNow() { if (active) { active = false; app.endOfflineRender(); } }
    };

    struct TempFileGuard
    {
        juce::File file;
        bool committed = false;
        ~TempFileGuard() { if (!committed) file.deleteFile(); }
    };

    struct TempDirectoryGuard
    {
        juce::File directory;
        bool committed = false;
        ~TempDirectoryGuard() { if (!committed) directory.deleteRecursively(); }
    };
}

ExportRenderCore::Result ExportRenderCore::renderFullMix(double sampleRate, int blockSize,
                                                         int64_t startSample, int64_t endSample)
{
    Result result;
    auto format = createAudioFormat(settings_.format);
    if (!format)
    {
        result.message = "Selected export format is unavailable";
        return result;
    }

    TempFileGuard temp { juce::File(makeTempPath(settings_.outputFile)) };
    temp.file.deleteFile();
    temp.file.getParentDirectory().createDirectory();

    OfflineRenderGuard offline { appCore_ };
    if (!appCore_.beginOfflineRender(blockSize))
    {
        result.message = appCore_.getLastOfflineRenderError();
        if (result.message.isEmpty())
            result.message = "Could not acquire offline render callback barrier";
        return result;
    }
    offline.active = true;

    setProgressDetail("Rendering full mix");
    juce::String error;
    if (!renderOneFile(*format, temp.file, sampleRate, blockSize, startSample, endSample,
                       true, 0, 1, error))
    {
        result.cancelled = threadShouldExit() || shutdownState_.isCancellationRequested();
        result.message = result.cancelled ? "Export cancelled" : error;
        return result;
    }

    offline.endNow();
    setProgressDetail("Publishing " + settings_.outputFile.getFileName());
    progress_.store(0.98f, std::memory_order_release);
    if (!ExportPublicationCore::publish(temp.file, settings_.outputFile))
    {
        result.message = "Could not replace final export file";
        return result;
    }

    temp.committed = true;
    progress_.store(1.0f, std::memory_order_release);
    result.success = true;
    result.message = "Export complete";
    return result;
}

ExportRenderCore::Result ExportRenderCore::renderStems(double sampleRate, int blockSize,
                                                       int64_t startSample, int64_t endSample)
{
    Result result;
    if (settings_.outputDirectory == juce::File{} || settings_.stemTargets.empty())
    {
        result.message = "No stem destination or targets selected";
        return result;
    }
    if (settings_.outputDirectory.exists())
    {
        result.message = "Stem package already exists. Choose a new folder name.";
        return result;
    }

    auto snapshot = appCore_.getRoutingGraph().getSnapshotPublisher().get();
    if (!snapshot)
    {
        result.message = "Routing snapshot is unavailable";
        return result;
    }

    const auto stageName = "." + settings_.outputDirectory.getFileName()
        + ".apex-staging-" + juce::Uuid().toString();
    TempDirectoryGuard stage { settings_.outputDirectory.getSiblingFile(stageName) };
    if (!stage.directory.createDirectory())
    {
        result.message = "Could not create the stem staging package";
        return result;
    }

    OfflineRenderGuard offline { appCore_ };
    if (!appCore_.beginOfflineRender(blockSize))
    {
        result.message = appCore_.getLastOfflineRenderError();
        if (result.message.isEmpty())
            result.message = "Could not acquire offline render callback barrier";
        return result;
    }
    offline.active = true;

    juce::StringArray manifestLines;
    manifestLines.add("APEX Stem Export");
    manifestLines.add("routingSnapshotVersion=" + juce::String((juce::int64) snapshot->version));
    manifestLines.add("sampleRate=" + juce::String(sampleRate, 1));
    manifestLines.add("frames=" + juce::String((juce::int64) (endSample - startSample)));
    manifestLines.add("format=" + exportFormatName(settings_.format));
    manifestLines.add("masterProcessing=" + juce::String(settings_.applyMasterProcessingToStems ? "full" : "pre-master"));

    const int targetCount = (int) settings_.stemTargets.size();
    for (int index = 0; index < targetCount; ++index)
    {
        if (threadShouldExit() || shutdownState_.isCancellationRequested())
        {
            result.cancelled = true;
            result.message = "Stem export cancelled";
            return result;
        }

        const auto& target = settings_.stemTargets[(size_t) index];
        auto mask = StemRouteMaskCore::build(*snapshot, target);
        if (!mask)
        {
            result.message = "Could not resolve stem target: " + target.displayName;
            return result;
        }
        if (!appCore_.beginOfflineStemPass(mask, blockSize, startSample,
                                           settings_.applyMasterProcessingToStems))
        {
            result.message = "Could not prepare stem pass: " + target.displayName;
            return result;
        }

        auto format = createAudioFormat(settings_.format);
        const auto fileName = makeStemFileName(index, target, settings_.format);
        const auto stemFile = stage.directory.getChildFile(fileName);
        setProgressDetail("Stem " + juce::String(index + 1) + " / " + juce::String(targetCount)
                          + "  •  " + target.displayName);

        juce::String error;
        if (!format || !renderOneFile(*format, stemFile, sampleRate, blockSize,
                                      startSample, endSample,
                                      settings_.applyMasterProcessingToStems,
                                      index, targetCount, error))
        {
            result.cancelled = threadShouldExit() || shutdownState_.isCancellationRequested();
            result.message = result.cancelled ? "Stem export cancelled" : error;
            return result;
        }

        manifestLines.add(juce::String(index + 1) + "|" + target.trackId + "|"
            + (target.kind == StemTargetKind::FolderBus ? "folder" : "track") + "|" + fileName);
    }

    setProgressDetail("Finalizing stem package");
    if (!stage.directory.getChildFile("APEX_STEMS.txt").replaceWithText(manifestLines.joinIntoString("\n")))
    {
        result.message = "Could not write stem package manifest";
        return result;
    }

    offline.endNow();
    progress_.store(0.99f, std::memory_order_release);
    setProgressDetail("Publishing stem package");
    if (!stage.directory.moveFileTo(settings_.outputDirectory))
    {
        result.message = "Could not publish the complete stem package";
        return result;
    }

    stage.committed = true;
    progress_.store(1.0f, std::memory_order_release);
    result.success = true;
    result.message = "Stem export complete";
    return result;
}

bool ExportRenderCore::renderOneFile(juce::AudioFormat& format,
                                     const juce::File& file,
                                     double sampleRate,
                                     int blockSize,
                                     int64_t startSample,
                                     int64_t endSample,
                                     bool applyMasterProcessing,
                                     int targetIndex,
                                     int targetCount,
                                     juce::String& error)
{
    file.deleteFile();
    auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
    if (!stream)
    {
        error = "Could not create export file: " + file.getFileName();
        return false;
    }

    const int writerBits = settings_.format == ExportAudioFormat::OggVorbis ? 32 : settings_.bitDepth;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        format.createWriterFor(stream.get(), sampleRate, 2, writerBits, {}, settings_.qualityIndex));
    if (!writer)
    {
        error = "Could not create " + exportFormatName(settings_.format) + " writer";
        return false;
    }
    stream.release();

    juce::AudioBuffer<float> buffer(2, blockSize);
    const int64_t totalSamples = endSample - startSample;
    int64_t timelineSample = startSample;
    while (timelineSample < endSample)
    {
        if (threadShouldExit() || shutdownState_.isCancellationRequested())
            return false;

        const int count = (int) juce::jmin<int64_t>(blockSize, endSample - timelineSample);
        buffer.clear();
        if (!appCore_.renderOfflineBlock(buffer, count, timelineSample, applyMasterProcessing))
        {
            error = "Offline render failed";
            return false;
        }
        if (!writer->writeFromAudioSampleBuffer(buffer, 0, count))
        {
            error = "Could not write " + file.getFileName();
            return false;
        }
        timelineSample += count;
        const double local = (double) (timelineSample - startSample) / (double) totalSamples;
        const double overall = ((double) targetIndex + local) / (double) juce::jmax(1, targetCount);
        progress_.store((float) juce::jlimit(0.0, 0.97, overall * 0.97), std::memory_order_release);
    }

    writer.reset();
    return verifyRenderedFile(format, file, sampleRate, totalSamples, error);
}

bool ExportRenderCore::verifyRenderedFile(juce::AudioFormat& format,
                                          const juce::File& file,
                                          double sampleRate,
                                          int64_t expectedFrames,
                                          juce::String& error) const
{
    auto input = std::unique_ptr<juce::FileInputStream>(file.createInputStream());
    if (!input || file.getSize() <= 0)
    {
        error = "Rendered file is empty";
        return false;
    }
    auto reader = std::unique_ptr<juce::AudioFormatReader>(format.createReaderFor(input.release(), true));
    if (!reader || std::abs(reader->sampleRate - sampleRate) > 0.001 || reader->numChannels != 2)
    {
        error = "Rendered file failed format verification";
        return false;
    }

    const int64_t tolerance = settings_.format == ExportAudioFormat::OggVorbis ? 8192 : 0;
    if (std::llabs((int64_t) reader->lengthInSamples - expectedFrames) > tolerance)
    {
        error = "Rendered file failed frame-count verification";
        return false;
    }
    return true;
}

std::unique_ptr<juce::AudioFormat> ExportRenderCore::createAudioFormat(ExportAudioFormat format)
{
    switch (format)
    {
        case ExportAudioFormat::Wav:       return std::make_unique<juce::WavAudioFormat>();
        case ExportAudioFormat::Aiff:      return std::make_unique<juce::AiffAudioFormat>();
        case ExportAudioFormat::Flac:      return std::make_unique<juce::FlacAudioFormat>();
        case ExportAudioFormat::OggVorbis: return std::make_unique<juce::OggVorbisAudioFormat>();
    }
    return {};
}

juce::String ExportRenderCore::makeStemFileName(int index, const StemExportTarget& target,
                                                ExportAudioFormat format)
{
    auto name = juce::File::createLegalFileName(target.displayName.trim());
    if (name.isEmpty()) name = "Stem";
    auto stable = juce::File::createLegalFileName(target.trackId);
    return juce::String(index + 1).paddedLeft('0', 3) + "_" + name + "__" + stable
        + exportFormatExtension(format);
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

bool ExportRenderCore::isSupportedBitDepth(ExportAudioFormat format, int bitDepth) noexcept
{
    switch (format)
    {
        case ExportAudioFormat::Wav:       return bitDepth == 16 || bitDepth == 24 || bitDepth == 32;
        case ExportAudioFormat::Aiff:      return bitDepth == 16 || bitDepth == 24;
        case ExportAudioFormat::Flac:      return bitDepth == 16 || bitDepth == 24;
        case ExportAudioFormat::OggVorbis: return bitDepth == 32;
    }
    return false;
}

juce::String ExportRenderCore::makeTempPath(const juce::File& outputFile)
{
    return outputFile.getFullPathName() + ".apex-export-" + juce::Uuid().toString() + ".tmp";
}

} // namespace DAW
