#pragma once
#include <JuceHeader.h>
#include "ExportSettingsCore.h"
#include <atomic>
#include <functional>

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

    using ProgressCallback = std::function<void(float)>;
    using CompletionCallback = std::function<void(Result)>;

    ExportRenderCore(ApplicationCore& appCore, ExportSettings settings);
    ~ExportRenderCore() override;

    bool startExport(ProgressCallback progressCallback, CompletionCallback completionCallback);
    void requestCancel() noexcept;
    bool isExportRunning() const noexcept;

    std::atomic<bool> cancelRequested { false };

private:
    void run() override;
    Result render();
    int64_t detectProjectEndSample(double sampleRate) const;
    static bool isSupportedBitDepth(int bitDepth) noexcept;
    static juce::String makeTempPath(const juce::File& outputFile);

    ApplicationCore& appCore_;
    ExportSettings settings_;
    ProgressCallback progressCallback_;
    CompletionCallback completionCallback_;
    std::atomic<bool> running_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportRenderCore)
};

} // namespace DAW
