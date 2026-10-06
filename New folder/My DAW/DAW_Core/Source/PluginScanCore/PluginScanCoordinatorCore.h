#pragma once
#include <JuceHeader.h>
#include "PluginScannerProcessCore.h"
#include "PluginCrashIsolationCore.h"
#include "PluginScanResultTransportCore.h"
#include "../PluginSafetyCore/PluginBlacklistCore.h"
#include "../PluginSafetyCore/PluginQuarantineCore.h"
#include "../PluginSafetyCore/PluginLoadRetryPolicyCore.h"
#include "../PluginStorageCore/PluginCacheCore.h"
#include "../PluginSecurityCore/PluginLoadFailureClassifierCore.h"
#include "../PluginSecurityCore/PluginBlockedDllLoggerCore.h"
#include "../PluginSecurityCore/HostBuildTrustModeCore.h"

namespace DAW {

/**
 * PluginScanCoordinatorCore
 *
 * Nucleus: orchestrates safe out-of-process plugin scanning.
 *
 * Batch mode (default):
 *   1. Build skip list from cache + blacklist
 *   2. Launch ONE subprocess with --scan-batch and all search paths
 *   3. Subprocess uses JUCE PluginDirectoryScanner + deadman file
 *   4. Parse streamed PLUGIN_OK / PLUGIN_FAIL blocks from stdout
 *   5. If subprocess crashes → read deadman → blacklist → relaunch
 *   6. Repeat until all plugins scanned or max retries reached
 *
 * This is ~50-100x faster than one-subprocess-per-plugin because
 * it avoids repeated JUCE app initialization overhead.
 */
class PluginScanCoordinatorCore : private juce::Thread
{
public:
    struct ScanProgress
    {
        int    totalFiles    = 0;
        int    scannedSoFar  = 0;
        int    succeeded     = 0;
        int    failed        = 0;
        int    skippedCache  = 0;
        int    skippedBlack  = 0;
        int    quarantined   = 0;
        float  progress      = 0.f;
        juce::String currentPlugin;
        juce::String lastError;
    };

    PluginScanCoordinatorCore(PluginBlacklistCore& blacklist,
                               PluginCacheCore& cache,
                               juce::KnownPluginList& knownPlugins)
        : juce::Thread("PluginScanCoordinator"),
          blacklist_(blacklist), cache_(cache), knownPlugins_(knownPlugins)
    {
    }

    ~PluginScanCoordinatorCore() override
    {
        stopThread(10000);
    }

    void setSearchPaths(const juce::FileSearchPath& paths) { searchPaths_ = paths; }
    void setTimeoutMs(int ms) { timeoutMs_ = ms; }
    void setForceRescan(bool force) { forceRescan_ = force; }

    void startScan(std::function<void(const ScanProgress&)> progressCb = nullptr,
                   std::function<void(const ScanProgress&)> doneCb = nullptr)
    {
        // Log IMMEDIATELY before anything else
        auto logPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core");
        logPath.createDirectory();
        auto logFile = logPath.getChildFile("scan_debug.log");
        logFile.replaceWithText("=== startScan() CALLED ===\n" + juce::Time::getCurrentTime().toString(true, true) + "\n");

        DBG("=== PluginScanCoordinatorCore::startScan CALLED ===");

        if (isThreadRunning())
        {
            logFile.appendText("Already scanning, ignoring\n");
            DBG("Already scanning, ignoring");
            return;
        }

        logFile.appendText("Starting scan thread...\n");
        DBG("Starting scan thread...");
        progressCb_ = std::move(progressCb);
        doneCb_     = std::move(doneCb);
        progress_   = {};
        startThread(juce::Thread::Priority::low);
        logFile.appendText("startThread() called\n");
        DBG("Scan thread started");
    }

    bool isScanning() const { return isThreadRunning(); }
    const ScanProgress& getProgress() const { return progress_; }

private:
    PluginBlacklistCore& blacklist_;
    PluginCacheCore&     cache_;
    juce::KnownPluginList& knownPlugins_;
    PluginQuarantineCore quarantine_;
    PluginLoadRetryPolicyCore retryPolicy_;
    juce::FileSearchPath searchPaths_;
    int  timeoutMs_   = 900000;  // 15 min — Waves/UAD shells can take a long time
    bool forceRescan_ = false;
    static constexpr int maxCrashRetries_ = 50;

    ScanProgress progress_;
    std::function<void(const ScanProgress&)> progressCb_;
    std::function<void(const ScanProgress&)> doneCb_;
    int parsedOkBlocks_ = 0;
    int parsedFailBlocks_ = 0;
    int parsedSkippedBlocks_ = 0;
    int cacheInsertCount_ = 0;

    // ── Thread entry ─────────────────────────────────────────────────────

    void run() override
    {
        // Create log file first thing
        auto logPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core");
        logPath.createDirectory();
        auto logFile = logPath.getChildFile("scan_debug.log");

        try {
            logFile.replaceWithText("=== SCAN THREAD STARTED ===\n" + juce::Time::getCurrentTime().toString(true, true) + "\n");
        } catch (...) {}

        auto log = [logFile](const juce::String& msg) mutable {
            try {
                logFile.appendText(msg + "\n");
            } catch (...) {}
            DBG(msg);
        };

        log("Thread is running");
        log("Search paths: " + searchPaths_.toString());
        for (int i = 0; i < searchPaths_.getNumPaths(); ++i)
            log("  Search path [" + juce::String(i) + "]: " + searchPaths_[i].getFullPathName());

        log("Checking previous deadman...");
        checkPreviousDeadman();
        log("Deadman check done");

        // Build skip list — skip everything that is cached and not stale
        juce::StringArray skipList;
        log("forceRescan = " + juce::String(forceRescan_ ? "YES" : "NO"));
        if (!forceRescan_)
        {
            log("Building skip list...");
            for (auto& e : blacklist_.getEntries())
            {
                if (e.reason != PluginBlacklistCore::Reason::PendingRetry)
                {
                    skipList.add(e.pluginPath);
                    progress_.skippedBlack++;
                }
            }
            log("Blacklisted: " + juce::String(progress_.skippedBlack));

            auto cachedPaths = cache_.getCachedPaths();
            log("Cache unique paths: " + juce::String(cachedPaths.size()));
            for (auto& path : cachedPaths)
            {
                if (!cache_.isCacheStale(path))
                {
                    skipList.add(path);
                    progress_.skippedCache++;
                }
            }
            log("Skipped from cache: " + juce::String(progress_.skippedCache));
            log("Skip list total: " + juce::String(skipList.size()));
        }

        log("Posting progress...");
        progress_.currentPlugin = "Starting batch scan...";
        postProgress();
        log("Launching batch subprocess...");

        parsedOkBlocks_ = 0;
        parsedFailBlocks_ = 0;
        parsedSkippedBlocks_ = 0;
        cacheInsertCount_ = 0;

        int crashRetries = 0;
        bool scanComplete = false;

        while (!scanComplete && crashRetries < maxCrashRetries_ && !threadShouldExit())
        {
            scanComplete = runBatchSubprocess(skipList);

            // Add all newly cached paths to skipList so a relaunch after a crash
            // doesn't re-scan plugins that already succeeded in this session.
            for (auto& p : cache_.getCachedPaths())
                skipList.addIfNotAlreadyThere(p);

            if (!scanComplete)
            {
                // Subprocess crashed — read deadman to find the culprit
                auto deadmanFile = getDeadmanFile();
                auto crashedPlugin = deadmanFile.loadFileAsString().trim();

                // Deadman may contain multiple lines if wibu wrote to it
                // Use only the first path line
                crashedPlugin = crashedPlugin.upToFirstOccurrenceOf("\n", false, false).trim();
                crashedPlugin = crashedPlugin.upToFirstOccurrenceOf("\r", false, false).trim();

                if (crashedPlugin.isNotEmpty())
                {
                    // Check if the partial results file shows this plugin was an
                    // Application Control / Wibu TLS block (not a real crash).
                    // These plugins work fine in other DAWs — don't blacklist them.
                    auto resultsFile = getDeadmanFile().getSiblingFile("scan_results_partial.txt");
                    bool wasAppControlBlock = resultsFile.loadFileAsString()
                        .containsIgnoreCase("Application Control");

                    if (wasAppControlBlock)
                    {
                        DBG("Subprocess blocked by AppControl on: " + crashedPlugin + " - NOT blacklisting");
                    }
                    else
                    {
                        DBG("Subprocess crashed on: " + crashedPlugin + " - blacklisting");
                        blacklist_.add(crashedPlugin,
                                       PluginBlacklistCore::Reason::CrashedDuringScan,
                                       "Subprocess crashed while scanning this plugin");
                        quarantine_.quarantine(crashedPlugin, "scan_subprocess_crash");
                        progress_.quarantined++;
                    }

                    skipList.addIfNotAlreadyThere(crashedPlugin);
                    progress_.failed++;
                }

                crashRetries++;
                progress_.currentPlugin = "Recovering from crash...";
                postProgress();
            }
        }

        // Clean deadman file
        getDeadmanFile().deleteFile();

        // Rebuild KnownPluginList from cache
        rebuildKnownList();

        // Obj 5: Final validation summary
        log("=== SCAN FINAL SUMMARY ===");
        log("  Parsed PLUGIN_OK blocks: " + juce::String(parsedOkBlocks_));
        log("  Parsed PLUGIN_FAIL blocks: " + juce::String(parsedFailBlocks_));
        log("  Parsed PLUGIN_SKIPPED blocks: " + juce::String(parsedSkippedBlocks_));
        log("  Cache inserts: " + juce::String(cacheInsertCount_));
        log("  Skipped from cache: " + juce::String(progress_.skippedCache));
        log("  Skipped from blacklist: " + juce::String(progress_.skippedBlack));
        log("  Quarantined: " + juce::String(progress_.quarantined));
        log("  Cache total entries: " + juce::String(cache_.getCount()));
        log("  Blacklist total entries: " + juce::String(blacklist_.getCount()));

        DBG("PluginScanCoordinator: scan complete - " +
            juce::String(progress_.succeeded) + " OK, " +
            juce::String(progress_.failed) + " failed, " +
            juce::String(progress_.skippedCache) + " cached, " +
            juce::String(progress_.skippedBlack) + " blacklisted");

        progress_.progress = 1.f;
        progress_.currentPlugin = "Complete";

        if (doneCb_)
        {
            auto cb = doneCb_;
            auto prog = progress_;
            juce::MessageManager::callAsync([cb, prog]() { cb(prog); });
        }
    }

    // ── Batch subprocess ─────────────────────────────────────────────────

    bool runBatchSubprocess(const juce::StringArray& skipList)
    {
        auto exePath = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getFullPathName();
        auto deadmanPath = getDeadmanFile().getFullPathName();
        auto searchPathsStr = searchPaths_.toString();
        auto skipStr = skipList.joinIntoString("|");

        juce::String cmd = "\"" + exePath + "\""
            + " --scan-batch"
            + " --search-paths \"" + searchPathsStr + "\""
            + " --deadman \"" + deadmanPath + "\""
            + " --skip \"" + skipStr + "\"";

        DBG("=== LAUNCHING BATCH SCANNER ===");
        DBG("Command: " + cmd);
        for (int i = 0; i < searchPaths_.getNumPaths(); ++i)
            DBG("  Search path [" + juce::String(i) + "]: " + searchPaths_[i].getFullPathName());
        DBG("Skip count: " + juce::String(skipList.size()));

        // Obj 4: Create raw stdout capture log
        auto logDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core");
        logDir.createDirectory();
        auto stdoutLogFile = logDir.getChildFile("scan_worker_stdout.log");
        stdoutLogFile.replaceWithText("=== SUBPROCESS STDOUT CAPTURE ===\n"
            + juce::Time::getCurrentTime().toString(true, true) + "\n");

        int succeededBefore = progress_.succeeded;
        int failedBefore = progress_.failed;

        juce::ChildProcess proc;
        if (!proc.start(cmd))
        {
            progress_.lastError = "Failed to launch batch scanner subprocess";
            DBG("ERROR: Failed to launch subprocess!");
            return true;  // Don't retry if we can't even launch
        }

        DBG("Subprocess launched successfully, reading stdout...");

        // Read output incrementally
        auto startTime = juce::Time::getMillisecondCounter();
        juce::String accumulatedOutput;
        int totalBytesRead = 0;

        while (proc.isRunning())
        {
            if (threadShouldExit())
            {
                proc.kill();
                return true;
            }

            // Read available output
            char buf[4096];
            auto bytesRead = proc.readProcessOutput(buf, sizeof(buf) - 1);
            if (bytesRead > 0)
            {
                buf[bytesRead] = 0;
                totalBytesRead += (int)bytesRead;
                auto chunk = juce::String::fromUTF8(buf, (int)bytesRead);
                accumulatedOutput += chunk;

                // Obj 4: Append raw stdout to debug log
                stdoutLogFile.appendText(chunk);

                DBG("Read " + juce::String((int)bytesRead) + " bytes from subprocess (total: " + juce::String(totalBytesRead) + ")");

                // Process complete blocks as they arrive
                processStreamedOutput(accumulatedOutput);

                // Reset timeout on activity
                startTime = juce::Time::getMillisecondCounter();
            }
            else
            {
                juce::Thread::sleep(50);
            }

            auto elapsed = juce::Time::getMillisecondCounter() - startTime;
            if ((int)elapsed > timeoutMs_)
            {
                proc.kill();
                progress_.lastError = "Batch scan timed out";
                DBG("ERROR: Batch scan timed out after " + juce::String(timeoutMs_ / 1000) + "s");
                return true;  // Don't retry on timeout
            }
        }

        // Read any remaining output from pipe
        auto remaining = proc.readAllProcessOutput();
        if (remaining.isNotEmpty())
        {
            accumulatedOutput += remaining;
            stdoutLogFile.appendText(remaining);
            processStreamedOutput(accumulatedOutput);
        }

        auto exitCode = proc.getExitCode();
        bool gotScanComplete = accumulatedOutput.contains("SCAN_COMPLETE");

        // Read partial results file only as crash recovery when stdout missed SCAN_COMPLETE
        auto resultsFile = getDeadmanFile().getSiblingFile("scan_results_partial.txt");
        if (!gotScanComplete && resultsFile.existsAsFile())
        {
            auto fileContent = resultsFile.loadFileAsString();
            if (fileContent.isNotEmpty())
            {
                DBG("Reading partial results file for crash recovery: " + juce::String(fileContent.length()) + " bytes");
                processStreamedOutput(fileContent);
                gotScanComplete = fileContent.contains("SCAN_COMPLETE");
            }
        }

        // Obj 4: Warn if no per-plugin blocks were parsed in this subprocess run
        int newOk = progress_.succeeded - succeededBefore;
        int newFail = progress_.failed - failedBefore;
        if (newOk == 0 && newFail == 0)
        {
            DBG("WARNING: worker completed without any PLUGIN_OK/PLUGIN_FAIL blocks");
            DBG("  Accumulated stdout length: " + juce::String(accumulatedOutput.length()));
            DBG("  Total bytes read from pipe: " + juce::String(totalBytesRead));
            DBG("  Exit code: " + juce::String(exitCode));
            DBG("  SCAN_COMPLETE received: " + juce::String(gotScanComplete ? "YES" : "NO"));
        }
        else
        {
            DBG("Subprocess results: " + juce::String(newOk) + " OK, " + juce::String(newFail) + " FAIL");
        }

        bool completed = gotScanComplete || exitCode == 0;
        DBG("Subprocess exit=" + juce::String(exitCode) + " completed=" + (completed ? "YES" : "NO"));
        return completed;
    }

    void processStreamedOutput(juce::String& buffer)
    {
        // Process complete PLUGIN_OK/PLUGIN_FAIL/PLUGIN_SKIPPED...PLUGIN_END blocks
        while (true)
        {
            auto endIdx = buffer.indexOf("PLUGIN_END");
            if (endIdx < 0) break;

            auto blockEnd = endIdx + 10; // length of "PLUGIN_END"
            auto block = buffer.substring(0, blockEnd);
            buffer = buffer.substring(blockEnd).trimStart();

            // Obj 9: Block boundary and parse logging
            if (block.contains("PLUGIN_OK"))
            {
                DBG("[PARSE] PLUGIN_OK block received, length=" + juce::String(block.length()));
                processOkBlock(block);
                parsedOkBlocks_++;
            }
            else if (block.contains("PLUGIN_FAIL"))
            {
                DBG("[PARSE] PLUGIN_FAIL block received, length=" + juce::String(block.length()));
                processFailBlock(block);
                parsedFailBlocks_++;
            }
            else if (block.contains("PLUGIN_SKIPPED"))
            {
                DBG("[PARSE] PLUGIN_SKIPPED block received");
                parsedSkippedBlocks_++;
            }
            else
            {
                DBG("[PARSE] Unknown block type, raw: " + block.substring(0, 200));
            }
        }
    }

    void processOkBlock(const juce::String& block)
    {
        auto kv = parseBlock(block);
        auto path = kv.getValue("path", "");
        if (path.isEmpty()) return;

        progress_.currentPlugin = juce::File(path).getFileName();
        progress_.succeeded++;
        progress_.scannedSoFar++;
        postProgress();

        PluginCacheCore::CachedPlugin entry;
        entry.path         = path;
        entry.format       = kv.getValue("format", "");
        entry.name         = kv.getValue("name", juce::File(path).getFileNameWithoutExtension());
        entry.manufacturer = kv.getValue("manufacturer", "Unknown");
        entry.category     = kv.getValue("category", "");
        entry.uniqueId     = kv.getValue("uniqueId", "");
        entry.isInstrument = kv.getValue("isInstrument", "0") == "1";
        entry.numInputs    = kv.getValue("numInputs", "0").getIntValue();
        entry.numOutputs   = kv.getValue("numOutputs", "0").getIntValue();
        entry.hasMidi      = false;
        entry.scanTimestamp = juce::Time::currentTimeMillis();
        entry.fileModTime   = juce::File(path).exists()
            ? juce::File(path).getLastModificationTime().toMilliseconds() : 0;

        int cacheSizeBefore = cache_.getCount();
        cache_.addOrUpdate(entry);
        cacheInsertCount_++;
        bool wasNew = cache_.getCount() > cacheSizeBefore;
        blacklist_.remove(path);
        retryPolicy_.reset(path);

        DBG("  CACHE " + juce::String(wasNew ? "INSERT" : "UPDATE") + ": "
            + entry.name + " | path=" + path
            + " | uniqueId=" + entry.uniqueId
            + " | manufacturer=" + entry.manufacturer
            + " | format=" + entry.format
            + " | cacheSize=" + juce::String(cache_.getCount()));
    }

    void processFailBlock(const juce::String& block)
    {
        auto kv = parseBlock(block);
        auto path = kv.getValue("path", "");
        if (path.isEmpty()) return;

        auto fileName = juce::File(path).getFileName();
        progress_.currentPlugin = fileName;
        progress_.failed++;
        progress_.scannedSoFar++;
        progress_.lastError = kv.getValue("error", "Unknown");
        postProgress();

        auto failure = PluginLoadFailureClassifierCore::classify(
            path, progress_.lastError, "batch_scan");
        PluginBlockedDllLoggerCore::log(failure);

        // Application Control / Wibu TLS block: the plugin works fine in other
        // DAWs — DO NOT permanently blacklist it. Just log and skip this session.
        bool isAppControlBlock =
            progress_.lastError.containsIgnoreCase("Application Control") ||
            progress_.lastError.containsIgnoreCase("blocked this file")   ||
            progress_.lastError.containsIgnoreCase("TLS")                 ||
            progress_.lastError.containsIgnoreCase("wibu");

        if (isAppControlBlock)
        {
            DBG("  SKIP (AppControl, not blacklisted): " + fileName);
            return;   // skip without blacklisting — will retry next launch
        }

        quarantine_.quarantine(path, categoryToReasonCode(failure.category));
        progress_.quarantined++;
        blacklist_.add(path, mapBlacklistReason(failure), progress_.lastError);
        retryPolicy_.recordFailure(path);

        DBG("  FAIL: " + fileName + " - " + progress_.lastError);
    }

    static juce::StringPairArray parseBlock(const juce::String& block)
    {
        juce::StringPairArray kv;
        auto lines = juce::StringArray::fromLines(block);
        for (auto& line : lines)
        {
            auto eq = line.indexOfChar('=');
            if (eq > 0)
                kv.set(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
        }
        return kv;
    }

    static PluginBlacklistCore::Reason mapBlacklistReason(const PluginLoadFailureInfo& failure)
    {
        switch (failure.category)
        {
            case PluginLoadFailureCategory::SecurityPolicyBlocked: return PluginBlacklistCore::Reason::PolicyBlockedDependency;
            case PluginLoadFailureCategory::CopyProtectionRuntimeBlocked: return PluginBlacklistCore::Reason::BlockedCopyProtectionRuntime;
            case PluginLoadFailureCategory::RuntimeDependencyFailure:
                if (failure.rawErrorText.containsIgnoreCase("tls initialization failed"))
                    return PluginBlacklistCore::Reason::TlsInitFailed;
                if (failure.rawErrorText.containsIgnoreCase("bad image"))
                    return PluginBlacklistCore::Reason::BadImageDependency;
                return PluginBlacklistCore::Reason::InvalidBinary;
            case PluginLoadFailureCategory::TimedOut: return PluginBlacklistCore::Reason::TimedOutDuringScan;
            case PluginLoadFailureCategory::CrashDuringInitialisation: return PluginBlacklistCore::Reason::CrashedDuringScan;
            default: return PluginBlacklistCore::Reason::UnknownSecurityFailure;
        }
    }

    static juce::String categoryToReasonCode(PluginLoadFailureCategory category)
    {
        switch (category)
        {
            case PluginLoadFailureCategory::SecurityPolicyBlocked: return "policy_blocked_dependency";
            case PluginLoadFailureCategory::CopyProtectionRuntimeBlocked: return "blocked_copy_protection_runtime";
            case PluginLoadFailureCategory::RuntimeDependencyFailure: return "runtime_dependency_failure";
            case PluginLoadFailureCategory::TimedOut: return "timeout";
            case PluginLoadFailureCategory::CrashDuringInitialisation: return "crash_during_init";
            default: return "unknown_security_failure";
        }
    }

    // ── Deadman's pedal ──────────────────────────────────────────────────

    void checkPreviousDeadman()
    {
        // Just delete the deadman from a previous run — the crash handler
        // already blacklisted the right plugin when the crash occurred.
        // We don't re-blacklist here to avoid false positives.
        getDeadmanFile().deleteFile();
    }

    static juce::File getDeadmanFile()
    {
        return juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
                .getChildFile("DAW_Core")
                .getChildFile("scan_deadman.txt");
    }

    // ── Rebuild KnownPluginList ──────────────────────────────────────────

    void rebuildKnownList()
    {
        juce::MessageManager::callAsync([this]()
        {
            knownPlugins_.clear();
            cache_.populateKnownList(knownPlugins_);
        });
    }

    // ── Progress posting ─────────────────────────────────────────────────

    void postProgress()
    {
        if (!progressCb_) return;
        auto cb = progressCb_;
        auto prog = progress_;
        juce::MessageManager::callAsync([cb, prog]() { cb(prog); });
    }
};

} // namespace DAW
