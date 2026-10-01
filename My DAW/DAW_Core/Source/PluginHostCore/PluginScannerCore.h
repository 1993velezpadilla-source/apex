#pragma once
#include <JuceHeader.h>
#include "../PluginScanCore/PluginBrowserFeedCore.h"
#include "../PluginScanCore/PluginScanAuditLogCore.h"
#include "../PluginScanCore/PluginScanDeadmanCore.h"
#include "../PluginScanCore/PluginScanFailureStoreCore.h"
#include "../PluginScanCore/PluginScanFormatsCore.h"
#include "../PluginScanCore/PluginScanHostCoordinatorCore.h"
#include "../PluginScanCore/PluginScanPathsCore.h"
#include "../PluginStorageCore/PluginCacheCore.h"
#include "../G10Core/G10NativePluginFormat.h"
#include "../C4Core/C4NativePluginFormat.h"
#include "../ParametricEQCore/ParametricEQNativePluginFormat.h"

namespace DAW {

/**
 * PluginScannerCore
 *
 * Nucleus: single public façade for the rebuilt modular plugin scan system.
 */
class PluginScannerCore
{
public:
    PluginScannerCore()
        : coordinator_(paths_, formats_, cache_, failureStore_, browserFeed_, knownPlugins_)
    {
        auto cacheFile = PluginCacheCore::getDefaultFile();
        auto failureFile = PluginScanFailureStoreCore::getDefaultFile();

        cacheFile.getParentDirectory().createDirectory();

        cache_.loadFromFile(cacheFile);
        failureStore_.loadFromFile(failureFile);
        seedNativeG10Entry();
        seedNativeC4Entry();
        seedNativeParametricEQEntry();
        browserFeed_.rebuildFromCache(cache_, knownPlugins_);

        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScannerCore::PluginScannerCore",
            "constructor-load",
            cache_.getCount(),
            failureStore_.getCount(),
            "cacheFile=\"" + cacheFile.getFullPathName()
                + "\" failureFile=\"" + failureFile.getFullPathName() + "\"");

        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScannerCore::PluginScannerCore",
            "constructor-preload-known",
            cache_.getCount(),
            failureStore_.getCount(),
            "knownPlugins=" + juce::String(knownPlugins_.getNumTypes()));
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginScannerCore::PluginScannerCore"
                " cacheCount=" + juce::String(cache_.getCount())
                + " failureCount=" + juce::String(failureStore_.getCount())
                + " knownPluginCount=" + juce::String(knownPlugins_.getNumTypes()));
        DBG("PluginScannerCore: loaded " + juce::String(cache_.getCount()) + " cached, "
            + juce::String(failureStore_.getCount()) + " failed entries");
    }

    ~PluginScannerCore()
    {
        savePersistentData();
    }

    // ── Search paths ─────────────────────────────────────────────────────

    void addDefaultSearchPaths()
    {
        paths_.buildDefaultPaths(formats_);
        DBG("PluginScannerCore: search paths = " + paths_.toDisplayString());
    }

    void addSearchPath(const juce::File& dir)
    {
        paths_.addSearchPath(dir);
    }

    // ── Scanning ─────────────────────────────────────────────────────────

    /** Start safe subprocess-based scan. */
    void startScan(std::function<void(float, const juce::String&)> progressCb = nullptr,
                   std::function<void()> doneCb = nullptr)
    {
        beginScan(false, std::move(progressCb), std::move(doneCb), "normal");
    }

    /** Force a full rescan (ignores cache). */
    void startFullRescan(std::function<void(float, const juce::String&)> progressCb = nullptr,
                         std::function<void()> doneCb = nullptr)
    {
        cache_.clear();
        failureStore_.clear();
        knownPlugins_.clear();
        // C6-plugin-registry: suppress persistence until the rescan
        // COMPLETES. Without this, an app exit mid-scan persisted the empty
        // in-memory cache over the valid on-disk plugin database, making the
        // next launch look unscanned.
        suppressPersistenceUntilScanCompletes_ = true;
        beginScan(true, std::move(progressCb), std::move(doneCb), "full-rescan");
    }

    bool isScanning() const { return coordinator_.isScanning(); }

    /** Obj 10: Clean full rescan — wipe ALL scan state files before rescanning. */
    void cleanFullRescan(std::function<void(float, const juce::String&)> progressCb = nullptr,
                         std::function<void()> doneCb = nullptr)
    {
        auto cacheFile = PluginCacheCore::getDefaultFile();
        auto failureFile = PluginScanFailureStoreCore::getDefaultFile();
        auto deadmanFile = PluginScanDeadmanCore().getFile();
        auto summaryFile = PluginScanAuditLogCore::getLogFile("scan_summary.log");
        auto hostIpcFile = PluginScanAuditLogCore::getLogFile("scan_host_ipc.log");
        auto workerEntryFile = PluginScanAuditLogCore::getLogFile("scan_worker_entry.log");
        auto workerResultsFile = PluginScanAuditLogCore::getLogFile("scan_worker_results.log");
        auto partialFile = deadmanFile.getSiblingFile("scan_results_partial.txt");
        auto startupTraceFile = PluginScanAuditLogCore::getStartupTraceFile();

        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScannerCore::cleanFullRescan",
            "clean",
            cache_.getCount(),
            failureStore_.getCount(),
            "cacheFile=\"" + cacheFile.getFullPathName()
                + "\" failureFile=\"" + failureFile.getFullPathName()
                + "\" deadmanFile=\"" + deadmanFile.getFullPathName()
                + "\" partialFile=\"" + partialFile.getFullPathName()
                + "\" summaryFile=\"" + summaryFile.getFullPathName()
                + "\" hostIpcFile=\"" + hostIpcFile.getFullPathName()
                + "\" workerEntryFile=\"" + workerEntryFile.getFullPathName()
                + "\" workerResultsFile=\"" + workerResultsFile.getFullPathName()
                + "\" startupTraceFile=\"" + startupTraceFile.getFullPathName() + "\"");

        cache_.clear();
        failureStore_.clear();
        knownPlugins_.clear();
        suppressPersistenceUntilScanCompletes_ = true;

        cacheFile.deleteFile();
        failureFile.deleteFile();
        partialFile.deleteFile();
        deadmanFile.deleteFile();
        PluginScanAuditLogCore::resetLogs();

        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScannerCore::cleanFullRescan",
            "clean-after-reset",
            cache_.getCount(),
            failureStore_.getCount(),
            "knownPlugins=" + juce::String(knownPlugins_.getNumTypes()));

        DBG("PluginScannerCore: all scan state wiped for clean full rescan");
        beginScan(true, std::move(progressCb), std::move(doneCb), "clean");
    }

    // ── Accessors ────────────────────────────────────────────────────────

    const juce::KnownPluginList& getKnownPlugins() const { return knownPlugins_; }
    juce::KnownPluginList&       getKnownPlugins()       { return knownPlugins_; }

    juce::AudioPluginFormatManager& getFormatManager() { return formats_.getManager(); }

    PluginCacheCore&       getCache()       { return cache_; }
    const PluginCacheCore& getCache() const { return cache_; }

    PluginScanFailureStoreCore&       getFailureStore()       { return failureStore_; }
    const PluginScanFailureStoreCore& getFailureStore() const { return failureStore_; }

    int ensureBrowserVisibleDataReady(const juce::String& caller)
    {
        auto cacheCount = cache_.getCount();
        auto knownBefore = knownPlugins_.getNumTypes();
        auto scannerId = juce::String::toHexString((juce::int64) reinterpret_cast<uintptr_t>(this));

        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            caller + " ensureBrowserVisibleDataReady start"
                " scannerId=0x" + scannerId
                + " cacheCount=" + juce::String(cacheCount)
                + " knownBefore=" + juce::String(knownBefore)
                + " scanRunning=" + juce::String(isScanning() ? 1 : 0));

        if (knownBefore == 0 && cacheCount > 0 && !isScanning())
            browserFeed_.rebuildFromCache(cache_, knownPlugins_);

        auto knownAfter = knownPlugins_.getNumTypes();
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            caller + " ensureBrowserVisibleDataReady end"
                " scannerId=0x" + scannerId
                + " cacheCount=" + juce::String(cache_.getCount())
                + " knownAfter=" + juce::String(knownAfter)
                + " browserFeedCount=" + juce::String(knownAfter)
                + " scanRunning=" + juce::String(isScanning() ? 1 : 0));

        return knownAfter;
    }

    void clearPluginList()
    {
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginScannerCore::clearPluginList WIPED_GLOBAL_REGISTRY"
                " cacheCountBefore=" + juce::String(cache_.getCount())
                + " knownBefore=" + juce::String(knownPlugins_.getNumTypes()));
        knownPlugins_.clear();
        cache_.clear();
    }

    // ── Legacy serialization (forwards to cache) ─────────────────────────

    void saveToFile(const juce::File& f) const
    {
        if (auto xml = knownPlugins_.createXml())
            xml->writeTo(f);
    }

    void loadFromFile(const juce::File& f)
    {
        if (auto xml = juce::XmlDocument::parse(f))
            knownPlugins_.recreateFromXml(*xml);
    }

public:
    /** Optional callback fired with true when a scan starts and false when it ends.
     *  Wire this to MessageBoxSuppressorCore::setScanActive() so the suppressor
     *  only runs EnumWindows during active scan sessions. */
    std::function<void(bool)> onScanActiveChanged;

private:
    PluginScanFormatsCore         formats_;
    PluginScanPathsCore           paths_;
    juce::KnownPluginList          knownPlugins_;
    PluginCacheCore                cache_;
    PluginScanFailureStoreCore     failureStore_;
    PluginBrowserFeedCore          browserFeed_;
    PluginScanHostCoordinatorCore  coordinator_;
    bool                           suppressPersistenceUntilScanCompletes_ = false;

    void beginScan(bool forceRescan,
                   std::function<void(float, const juce::String&)> progressCb,
                   std::function<void()> doneCb,
                   const juce::String& mode)
    {
        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScannerCore::startScan",
            mode,
            cache_.getCount(),
            failureStore_.getCount(),
            "knownPlugins=" + juce::String(knownPlugins_.getNumTypes())
                + " forceRescan=" + juce::String(forceRescan ? 1 : 0));

        if (coordinator_.isScanning())
            return;

        // Activate the MessageBox suppressor for the duration of this scan
        if (onScanActiveChanged) onScanActiveChanged(true);

        coordinator_.setForceRescan(forceRescan);

        // Re-seed the intrinsic APEX G10/C4/Parametric EQ entries so they survive
        // full/clean rescans (the coordinator never clears the cache, but the
        // entries must exist before the scan's done-callback rebuilds the
        // browser feed).
        seedNativeG10Entry();
        seedNativeC4Entry();
        seedNativeParametricEQEntry();

        coordinator_.startScan(
            [progressCb](const PluginScanHostCoordinatorCore::ScanProgress& p)
            {
                if (progressCb) progressCb(p.progress, p.currentPlugin);
            },
            [this, doneCb](const PluginScanHostCoordinatorCore::ScanProgress&)
            {
                suppressPersistenceUntilScanCompletes_ = false;
                browserFeed_.rebuildFromCache(cache_, knownPlugins_);
                PluginScanAuditLogCore::appendLine(
                    "plugin_ui_flow.log",
                    "PluginScannerCore::startScan done"
                        " finalCacheEntryCount=" + juce::String(cache_.getCount())
                        + " finalKnownPluginCount=" + juce::String(knownPlugins_.getNumTypes())
                        + " failureCount=" + juce::String(failureStore_.getCount()));
                savePersistentData();
                // Deactivate the suppressor now that the scan is complete
                if (onScanActiveChanged) onScanActiveChanged(false);
                if (doneCb) doneCb();
            });
    }

    /** Seed the intrinsic APEX G10 plugin into the cache. addOrUpdate()
        dedupes on path+uniqueId, so repeated calls (constructor + every
        beginScan) never create duplicate entries. The entry has no real
        filesystem path (fileModTime == 0), which is fine: the native format
        never rescans and doesPluginStillExist() is intrinsic. */
    void seedNativeG10Entry()
    {
        auto g10Desc = APEX::G10::G10NativePluginFormat::createG10Description();
        cache_.cacheFromDescription(g10Desc);
    }

    /** Seed the intrinsic APEX C4 plugin into the cache (same contract as
        seedNativeG10Entry). */
    void seedNativeC4Entry()
    {
        auto c4Desc = APEX::C4::C4NativePluginFormat::createC4Description();
        cache_.cacheFromDescription(c4Desc);
    }

    /** Seed the intrinsic APEX Parametric EQ plugin into the cache. */
    void seedNativeParametricEQEntry()
    {
        auto description = APEX::ParametricEQ::NativePluginFormat::createDescription();
        cache_.cacheFromDescription(description);
    }

    void savePersistentData()
    {
        if (suppressPersistenceUntilScanCompletes_)
        {
            PluginScanAuditLogCore::appendStartupTrace(
                "PluginScannerCore::savePersistentData",
                "suppressed",
                cache_.getCount(),
                failureStore_.getCount(),
                "knownPlugins=" + juce::String(knownPlugins_.getNumTypes()));
            return;
        }

        auto cacheFile = PluginCacheCore::getDefaultFile();
        auto failureFile = PluginScanFailureStoreCore::getDefaultFile();

        // C6-plugin-registry: never overwrite a valid on-disk database with
        // an empty in-memory list. The empty state is only ever persisted
        // deliberately (cleanFullRescan deletes the file first) — an
        // accidental in-memory wipe (or any future misuse of
        // clearPluginList) must not destroy the user's plugin database.
        if (cache_.getCount() == 0
            && cacheFile.existsAsFile()
            && cacheFile.getSize() > 0)
        {
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginScannerCore::savePersistentData SKIPPED_EMPTY_OVERWRITE"
                    " cacheFile=\"" + cacheFile.getFullPathName()
                    + "\" fileSize=" + juce::String(cacheFile.getSize()));
            juce::Logger::writeToLog("[PLUGIN REGISTRY] refusing to overwrite non-empty plugin database with an empty list");
            return;
        }

        cacheFile.getParentDirectory().createDirectory();
        cache_.saveToFile(cacheFile);
        failureStore_.saveToFile(failureFile);
        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScannerCore::savePersistentData",
            "persist",
            cache_.getCount(),
            failureStore_.getCount(),
            "cacheFile=\"" + cacheFile.getFullPathName()
                + "\" failureFile=\"" + failureFile.getFullPathName()
                + "\" knownPlugins=" + juce::String(knownPlugins_.getNumTypes()));
        DBG("PluginScannerCore: saved " + juce::String(cache_.getCount()) + " cached, "
            + juce::String(failureStore_.getCount()) + " failed entries");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScannerCore)
};

} // namespace DAW
