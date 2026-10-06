#pragma once
#include <JuceHeader.h>
#include "PluginBrowserFeedCore.h"
#include "PluginScanAuditLogCore.h"
#include "PluginScanCacheWriterCore.h"
#include "PluginScanDeadmanCore.h"
#include "PluginScanFailureStoreCore.h"
#include "PluginScanFormatsCore.h"
#include "PluginScanIpcProtocolCore.h"
#include "PluginScanJobBuilderCore.h"
#include "PluginScanPathsCore.h"
#include "PluginScanRecoveryCore.h"
#include "PluginScanResultCore.h"
#include "PluginScanSummaryCore.h"
#include "../PluginStorageCore/PluginCacheCore.h"

namespace DAW {

class PluginScanHostCoordinatorCore : private juce::Thread
{
public:
    struct ScanProgress
    {
        int candidatesDiscovered = 0;
        int candidatesAttempted = 0;
        int successCount = 0;
        int failCount = 0;
        int skippedCount = 0;
        int crashCount = 0;
        int timeoutCount = 0;
        float progress = 0.0f;
        juce::String currentPlugin;
        juce::String lastError;
    };

    PluginScanHostCoordinatorCore(PluginScanPathsCore& paths,
                                  PluginScanFormatsCore& formats,
                                  PluginCacheCore& cache,
                                  PluginScanFailureStoreCore& failureStore,
                                  PluginBrowserFeedCore& browserFeed,
                                  juce::KnownPluginList& knownPlugins)
        : juce::Thread("PluginScanHostCoordinator"),
          paths_(paths),
          formats_(formats),
          cache_(cache),
          failureStore_(failureStore),
          browserFeed_(browserFeed),
          knownPlugins_(knownPlugins)
    {
    }

    ~PluginScanHostCoordinatorCore() override
    {
        stopThread(5000);
    }

    void setForceRescan(bool shouldForce) { forceRescan_ = shouldForce; }
    void setTimeoutMs(int timeoutMs) { timeoutMs_ = timeoutMs; }

    void startScan(std::function<void(const ScanProgress&)> progressCallback,
                   std::function<void(const ScanProgress&)> doneCallback)
    {
        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScanHostCoordinatorCore::startScan",
            forceRescan_ ? "clean" : "normal",
            cache_.getCount(),
            failureStore_.getCount(),
            "timeoutMs=" + juce::String(timeoutMs_)
                + " knownPlugins=" + juce::String(knownPlugins_.getNumTypes()));

        if (isThreadRunning())
            return;

        progressCallback_ = std::move(progressCallback);
        doneCallback_ = std::move(doneCallback);
        progress_ = {};
        summary_ = {};
        startThread(juce::Thread::Priority::low);
    }

    bool isScanning() const { return isThreadRunning(); }
    const ScanProgress& getProgress() const { return progress_; }
    const PluginScanSummary& getSummary() const { return summary_; }

private:
    PluginScanPathsCore& paths_;
    PluginScanFormatsCore& formats_;
    PluginCacheCore& cache_;
    PluginScanFailureStoreCore& failureStore_;
    PluginBrowserFeedCore& browserFeed_;
    juce::KnownPluginList& knownPlugins_;
    PluginScanJobBuilderCore jobBuilder_;
    PluginScanCacheWriterCore cacheWriter_;
    PluginScanDeadmanCore deadman_;
    PluginScanRecoveryCore recovery_;
    ScanProgress progress_;
    PluginScanSummary summary_;
    std::function<void(const ScanProgress&)> progressCallback_;
    std::function<void(const ScanProgress&)> doneCallback_;
    int timeoutMs_ = 900000;
    bool forceRescan_ = false;
    int skippedCacheFreshCount_ = 0;
    int skippedRetryFalseCount_ = 0;

    void run() override
    {
        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScanHostCoordinatorCore::run",
            forceRescan_ ? "clean" : "normal",
            cache_.getCount(),
            failureStore_.getCount(),
            "summaryFile=\"" + PluginScanAuditLogCore::getLogFile("scan_summary.log").getFullPathName()
                + "\" hostIpcFile=\"" + PluginScanAuditLogCore::getLogFile("scan_host_ipc.log").getFullPathName()
                + "\" deadmanFile=\"" + deadman_.getFile().getFullPathName()
                + "\" partialFile=\"" + deadman_.getFile().getSiblingFile("scan_results_partial.txt").getFullPathName() + "\"");

        PluginScanAuditLogCore::replaceWithHeader("scan_host_ipc.log", "HOST_SCAN_START");
        PluginScanAuditLogCore::replaceWithHeader("scan_summary.log", "SCAN_SUMMARY_START");
        recovery_.recoverInterruptedScan(deadman_, failureStore_);

        auto formatNames = formats_.getRegisteredFormatNames();
        PluginScanAuditLogCore::appendLine("scan_summary.log", "FORMATS=" + formatNames.joinIntoString(", "));
        for (const auto& path : paths_.getPathStrings())
            PluginScanAuditLogCore::appendLine("scan_summary.log", "PATH=" + path);

        auto jobs = jobBuilder_.buildJobs(paths_, formats_);
        logCandidateInventory(jobs);
        summary_.candidatesDiscovered = static_cast<int>(jobs.size());
        progress_.candidatesDiscovered = summary_.candidatesDiscovered;
        skippedCacheFreshCount_ = 0;
        skippedRetryFalseCount_ = 0;

        for (auto& job : jobs)
        {
            if (threadShouldExit())
                break;

            if (!forceRescan_ && cache_.hasCachedEntry(job.filePath) && !cache_.isCacheStale(job.filePath))
            {
                job.state = PluginScanState::Skipped;
                ++summary_.skippedCount;
                ++progress_.skippedCount;
                ++skippedCacheFreshCount_;
                ++progress_.candidatesAttempted;
                progress_.currentPlugin = juce::File(job.filePath).getFileName();
                updateProgress();
                PluginScanAuditLogCore::appendLine("scan_host_ipc.log",
                    "SKIPPED jobId=" + juce::String(job.jobId) + " path=" + job.filePath + " reason=CacheFresh");
                continue;
            }

            if (!failureStore_.shouldRetry(job.filePath))
            {
                ++summary_.skippedCount;
                ++progress_.skippedCount;
                ++skippedRetryFalseCount_;
                ++progress_.candidatesAttempted;
                PluginScanAuditLogCore::appendLine("scan_host_ipc.log",
                    "SKIPPED jobId=" + juce::String(job.jobId) + " path=" + job.filePath + " reason=FailureStoreShouldNotRetry");
                continue;
            }

            PluginScanResult result;
            runWorker(job, result);
            handleResult(result);
        }

        browserFeed_.rebuildFromCache(cache_, knownPlugins_);
        summary_.finalVisiblePluginCount = knownPlugins_.getNumTypes();
        PluginScanAuditLogCore::appendLine("scan_summary.log",
            "SKIP_COUNTS cacheFresh=" + juce::String(skippedCacheFreshCount_)
                + " retryFalse=" + juce::String(skippedRetryFalseCount_));
        PluginScanAuditLogCore::appendLine("scan_summary.log", PluginScanSummaryCore::toLogString(summary_));

        progress_.progress = 1.0f;
        progress_.currentPlugin = "Complete";
        dispatchDone();
    }

    void runWorker(const PluginScanJobBuilderCore::ScanJob& job, PluginScanResult& outResult)
    {
        progress_.currentPlugin = juce::File(job.filePath).getFileName();
        updateProgress();
        deadman_.writeCurrentCandidate(job);

        auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        auto command = PluginScanIpcProtocolCore::buildWorkerCommand(executable, job);
        PluginScanAuditLogCore::appendLine("scan_host_ipc.log", "LAUNCH " + command);

        juce::ChildProcess process;
        if (!process.start(command))
        {
            outResult = PluginScanResultCore::makeBasicResult(job.jobId, job.filePath, job.format, PluginScanState::Failed);
            outResult.failureReason = "LaunchFailed";
            outResult.errorText = "Failed to launch worker process.";
            return;
        }

        bool gotTerminalResult = false;
        auto startTime = juce::Time::getMillisecondCounter();
        juce::String buffer;

        while (process.isRunning())
        {
            if (threadShouldExit())
            {
                process.kill();
                break;
            }

            char chunk[2048] = {};
            auto bytesRead = process.readProcessOutput(chunk, sizeof(chunk) - 1);
            if (bytesRead > 0)
            {
                chunk[bytesRead] = 0;
                buffer += juce::String::fromUTF8(chunk, static_cast<int>(bytesRead));
                parseWorkerOutput(buffer, outResult, gotTerminalResult);
            }
            else
            {
                juce::Thread::sleep(25);
            }

            auto elapsed = static_cast<int>(juce::Time::getMillisecondCounter() - startTime);
            if (elapsed > timeoutMs_)
            {
                process.kill();
                outResult = PluginScanResultCore::makeBasicResult(job.jobId, job.filePath, job.format, PluginScanState::TimedOut);
                outResult.failureReason = "TimedOut";
                outResult.errorText = "Worker timed out.";
                return;
            }
        }

        auto remaining = process.readAllProcessOutput();
        if (remaining.isNotEmpty())
        {
            buffer += remaining;
            parseWorkerOutput(buffer, outResult, gotTerminalResult);
        }

        if (gotTerminalResult)
            return;

        auto exitCode = process.getExitCode();
        outResult = PluginScanResultCore::makeBasicResult(job.jobId, job.filePath, job.format,
            exitCode == 0 ? PluginScanState::Failed : PluginScanState::Crashed);
        outResult.failureReason = exitCode == 0 ? "MissingTerminalResult" : "Crashed";
        outResult.errorText = exitCode == 0
            ? "Worker exited without a terminal result message."
            : "Worker exited abnormally with code " + juce::String(exitCode) + ".";
    }

    void parseWorkerOutput(juce::String& buffer,
                           PluginScanResult& outResult,
                           bool& gotTerminalResult)
    {
        while (buffer.containsChar('\n'))
        {
            auto line = buffer.upToFirstOccurrenceOf("\n", false, false).trim();
            buffer = buffer.fromFirstOccurrenceOf("\n", false, false);

            if (line.isEmpty())
                continue;

            PluginScanAuditLogCore::appendLine("scan_host_ipc.log", "RX " + line);

            juce::String messageType;
            juce::var payload;
            if (!PluginScanIpcProtocolCore::parseMessage(line, messageType, payload))
                continue;

            if (messageType == "CandidateResult")
            {
                outResult = PluginScanResultCore::fromVar(payload);
                gotTerminalResult = PluginScanStateCore::isTerminal(outResult.resultType);
            }
        }
    }

    void handleResult(const PluginScanResult& result)
    {
        ++summary_.candidatesAttempted;
        ++progress_.candidatesAttempted;
        progress_.currentPlugin = juce::File(result.candidatePath).getFileName();
        progress_.lastError = result.errorText;

        switch (result.resultType)
        {
            case PluginScanState::Success:
                ++summary_.successCount;
                ++progress_.successCount;
                summary_.cacheInserts += cacheWriter_.persistSuccessfulResult(result, cache_);
                failureStore_.remove(result.candidatePath);
                deadman_.clear();
                break;
            case PluginScanState::Skipped:
                ++summary_.skippedCount;
                ++progress_.skippedCount;
                deadman_.clear();
                break;
            case PluginScanState::TimedOut:
                ++summary_.timeoutCount;
                ++progress_.timeoutCount;
                ++summary_.failCount;
                ++progress_.failCount;
                summary_.failureStoreInserts += failureStore_.storeResult(result);
                cache_.remove(result.candidatePath);
                break;
            case PluginScanState::Crashed:
                ++summary_.crashCount;
                ++progress_.crashCount;
                ++summary_.failCount;
                ++progress_.failCount;
                summary_.failureStoreInserts += failureStore_.storeResult(result);
                cache_.remove(result.candidatePath);
                break;
            case PluginScanState::Failed:
                ++summary_.failCount;
                ++progress_.failCount;
                summary_.failureStoreInserts += failureStore_.storeResult(result);
                cache_.remove(result.candidatePath);
                deadman_.clear();
                break;
            case PluginScanState::Pending:
            case PluginScanState::Scanning:
                break;
        }

        auto cacheFile = PluginCacheCore::getDefaultFile();
        auto failureFile = PluginScanFailureStoreCore::getDefaultFile();
        cacheFile.getParentDirectory().createDirectory();
        cache_.saveToFile(cacheFile);
        failureStore_.saveToFile(failureFile);
        updateProgress();
    }

    void updateProgress()
    {
        auto total = juce::jmax(1, progress_.candidatesDiscovered);
        progress_.progress = static_cast<float>(progress_.candidatesAttempted) / static_cast<float>(total);
        if (!progressCallback_)
            return;

        auto callback = progressCallback_;
        auto progressCopy = progress_;
        juce::MessageManager::callAsync([callback, progressCopy]() { callback(progressCopy); });
    }

    void dispatchDone()
    {
        if (!doneCallback_)
            return;

        auto callback = doneCallback_;
        auto progressCopy = progress_;
        juce::MessageManager::callAsync([callback, progressCopy]() { callback(progressCopy); });
    }

    void logCandidateInventory(const std::vector<PluginScanJobBuilderCore::ScanJob>& jobs)
    {
        std::map<juce::String, int> formatCounts;
        std::map<juce::String, int> folderCounts;
        std::map<juce::String, int> vendorClueCounts;

        for (const auto& job : jobs)
        {
            ++formatCounts[job.format];

            auto folder = juce::File(job.filePath).getParentDirectory().getFullPathName();
            ++folderCounts[folder];

            auto clue = deriveVendorClue(job.filePath);
            ++vendorClueCounts[clue];
        }

        PluginScanAuditLogCore::appendLine("scan_summary.log",
            "CANDIDATE_TOTAL=" + juce::String(static_cast<int>(jobs.size())));

        for (const auto& [format, count] : formatCounts)
            PluginScanAuditLogCore::appendLine("scan_summary.log",
                "CANDIDATE_FORMAT format=" + format
                    + " count=" + juce::String(count));

        for (const auto& [folder, count] : folderCounts)
            PluginScanAuditLogCore::appendLine("scan_summary.log",
                "CANDIDATE_FOLDER path=" + folder
                    + " count=" + juce::String(count));

        for (const auto& [clue, count] : vendorClueCounts)
            PluginScanAuditLogCore::appendLine("scan_summary.log",
                "CANDIDATE_VENDOR_CLUE clue=" + clue
                    + " count=" + juce::String(count));
    }

    static juce::String deriveVendorClue(const juce::String& path)
    {
        auto name = juce::File(path).getFileNameWithoutExtension().trim();
        static constexpr const char* separators[] = { "-", "_", " " };
        for (auto separator : separators)
        {
            if (name.contains(separator))
                return name.upToFirstOccurrenceOf(separator, false, false).trim();
        }

        return name;
    }
};

} // namespace DAW
