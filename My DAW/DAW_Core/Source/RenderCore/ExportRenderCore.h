#pragma once
#include <JuceHeader.h>
#include "ExportSettingsCore.h"
#include "ExportShutdownStateCore.h"
#include "AsyncCompletionStateCore.h"
#include "../AudioEngineCore/AudioEngine.h"
#include <atomic>
#include <functional>
#include <memory>

namespace DAW {

class ApplicationCore;
class ClipManager;

class ExportRenderCore final : public juce::Thread
{
public:
    struct Result
    {
        bool success = false;
        bool cancelled = false;
        juce::String message;
    };

    using CompletionCallback = std::function<void(Result)>;

    ExportRenderCore(ApplicationCore& appCore, ExportSettings settings);
    ~ExportRenderCore() override;

    bool startExport(CompletionCallback completionCallback);
    void requestCancel() noexcept;
    void prepareForShutdown() noexcept;
    bool isExportRunning() const noexcept;
    float getProgress() const noexcept;
    juce::String getProgressDetail() const;
    bool dispatchPendingCompletion();

private:
    void run() override;
    Result render();
    Result renderFullMix(double engineSampleRate, int blockSize, int64_t startSample, int64_t endSample);
    Result renderStems(double engineSampleRate, int blockSize, int64_t startSample, int64_t endSample);
    int64_t detectProjectEndSample(double sampleRate) const;
    static bool isSupportedBitDepth(ExportAudioFormat format, int bitDepth) noexcept;
    static juce::String makeTempPath(const juce::File& outputFile);
    static std::unique_ptr<juce::AudioFormat> createAudioFormat(ExportAudioFormat format);
    static juce::String makeStemFileName(int index, const StemExportTarget& target, ExportAudioFormat format);
    bool renderOneFile(juce::AudioFormat& format,
                       const juce::File& file,
                       double sampleRate,
                       int blockSize,
                       int64_t startSample,
                       int64_t endSample,
                       bool applyMasterProcessing,
                       int targetIndex,
                       int targetCount,
                       juce::String& error);
    bool verifyRenderedFile(juce::AudioFormat& format,
                            const juce::File& file,
                            double sampleRate,
                            int64_t expectedFrames,
                            juce::String& error) const;
    void setProgressDetail(const juce::String& detail);

    ApplicationCore& appCore_;
    ExportSettings settings_;
    CompletionCallback completionCallback_;
    std::atomic<bool> running_ { false };
    std::atomic<float> progress_ { 0.0f };
    mutable juce::CriticalSection progressDetailLock_;
    juce::String progressDetail_;
    std::shared_ptr<AsyncCompletionStateCore<Result>> completionState_ =
        std::make_shared<AsyncCompletionStateCore<Result>>();
    ExportShutdownStateCore shutdownState_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportRenderCore)
};

} // namespace DAW
