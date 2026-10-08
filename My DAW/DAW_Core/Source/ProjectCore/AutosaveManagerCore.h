#pragma once
#include <JuceHeader.h>

#include <memory>
#include <cstdint>

namespace DAW {

class ProjectManager;
class RecoverySessionCore;
class AutosavePublicationAuthority;

/**
 * AutosaveManagerCore — timed autosave with atomic writes, backup rotation,
 * dirty-state tracking, and recording/export safety gates.
 *
 * Owns:
 *   - autosaveDirty flag (cleared after each successful autosave)
 *   - userDirty flag     (cleared ONLY by manual Save; drives title-bar asterisk)
 *   - background thread pool for file I/O (never blocks message thread)
 *
 * Thread model: timer fires on message thread, I/O runs on background thread.
 */
class AutosaveManagerCore : private juce::Timer
{
public:
    AutosaveManagerCore();
    ~AutosaveManagerCore() override;

    /** Wire to project manager and optional session core. Call before any other method. */
    void prepare(ProjectManager& projectManager,
                 RecoverySessionCore* sessionCore = nullptr);

    /**
     * Revoke publication authority, stop scheduling, and wait only for the
     * configured bounded shutdown window. A false result means a worker may
     * still be draining, but it has no authority to publish.
     */
    bool shutdown();

    // ── Configuration ────────────────────────────────────────────────────

    void setEnabled(bool shouldEnable);
    void setIntervalSeconds(int seconds);
    void setKeepCount(int count);          ///< How many numbered backups to retain (default 10)

    // ── Dirty state ──────────────────────────────────────────────────────

    /** Mark project changed. reason is logged if autosave is deferred. */
    void markDirty(const juce::String& reason);   // message thread

    /** Call after user Save succeeds. Clears userDirty only. */
    void markCleanManualSave();

    /** Invalidate old autosave completions after a successful Load/New Project. */
    void onProjectLoaded();

    bool isUserDirty()      const noexcept { return userDirty_.load(); }
    bool isAutosaveDirty()  const noexcept { return autosaveDirty_.load(); }

    // ── Safety gates ─────────────────────────────────────────────────────

    void onRecordingStarted();
    void onRecordingFinalized();
    void onExportStarted();
    void onExportFinished();
    void onPluginScanStarted();
    void onPluginScanFinished();

    /**
     * Optional: set a query lambda that returns true when recording is active.
     * If set, AutosaveManagerCore polls this instead of relying on push calls.
     */
    void setIsRecordingQuery(std::function<bool()> fn) { isRecordingQuery_ = std::move(fn); }

    /** Optional: set a query lambda that returns true when export is in progress. */
    void setIsExportingQuery(std::function<bool()> fn) { isExportingQuery_ = std::move(fn); }

    // ── Manual trigger ───────────────────────────────────────────────────

    /** Force an autosave attempt now, regardless of timer. */
    void triggerAutosaveNow(const juce::String& reason);

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    /** Test-only bounded wait override for deterministic timeout coverage. */
    void setShutdownWaitTimeoutForTesting(int milliseconds) noexcept
    {
        shutdownWaitTimeoutMs_ = juce::jmax(0, milliseconds);
    }
#endif

    // ── Queries ──────────────────────────────────────────────────────────

    bool       isAutosaveInProgress() const noexcept;
    juce::File getLatestAutosaveFile() const;
    juce::Time getLastAutosaveTime()   const noexcept;

    // ── Status callbacks (called on message thread) ───────────────────────

    /** Called after a successful autosave. Receives the timestamp string. */
    std::function<void(const juce::String& timeStr)> onAutosaveSucceeded;

    /** Called after a failed autosave. Receives an error description. */
    std::function<void(const juce::String& reason)> onAutosaveFailed;

private:
    // juce::Timer
    void timerCallback() override;

    // Safety gate check — returns false and fills reason if we must defer.
    bool canAutosaveNow(juce::String& deferReasonOut) const;

    // Kicks off background write. Must be called from message thread.
    void performAutosaveAsync();

    // Limit plugin-only bookkeeping frequency; never suppress autosave-dirty.
    static constexpr int kPluginThrottleSecs = 10;
    juce::Time lastPluginOnlyDirtyTime_;

    // ── Owned objects ────────────────────────────────────────────────────
    ProjectManager*        pm_          = nullptr;
    RecoverySessionCore*   sessionCore_ = nullptr;

    // Background I/O — single-job pool so only one write runs at a time.
    juce::ThreadPool       ioPool_;
    std::shared_ptr<AutosavePublicationAuthority> publicationAuthority_;
    // ── Config ───────────────────────────────────────────────────────────
    bool   enabled_          = true;
    int    intervalSeconds_  = 120;
    int    keepCount_        = 10;

    // ── Runtime state ────────────────────────────────────────────────────
    std::atomic<bool>        userDirty_      { false };
    std::atomic<bool>        autosaveDirty_  { false };
    std::atomic<bool>        writing_        { false };

    // Message-thread-only revisions. Worker completions return through callAsync.
    std::uint64_t            dirtyRevision_  = 0;
    std::uint64_t            sessionEpoch_   = 0;
    std::atomic<bool>        recording_      { false };
    std::atomic<bool>        exporting_      { false };
    std::atomic<bool>        scanning_       { false };
    std::atomic<bool>        diagRecordingFinalizedAutosave_ { false };

    std::function<bool()>    isRecordingQuery_;
    std::function<bool()>    isExportingQuery_;

    juce::Time               lastAutosaveTime_;
    juce::File               latestAutosaveFile_;
    mutable juce::ReadWriteLock fileLock_;

    bool                     prepared_ = false;
    bool                     shutdownStarted_ = false;
    bool                     lastShutdownDrained_ = true;
    int                      shutdownWaitTimeoutMs_ = 5000;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AutosaveManagerCore)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutosaveManagerCore)
};

} // namespace DAW
