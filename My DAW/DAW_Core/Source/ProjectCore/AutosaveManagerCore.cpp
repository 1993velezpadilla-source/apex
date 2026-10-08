#include "AutosaveManagerCore.h"
#include "ProjectManager.h"
#include "RecoverySessionCore.h"
#include "AutosaveWriteJobCore.h"

namespace DAW {

namespace
{
    static void logAlways(const juce::String& message)
    {
        juce::Logger::writeToLog(message);
    }
}

// ── AutosaveManagerCore ──────────────────────────────────────────────────────

AutosaveManagerCore::AutosaveManagerCore()
    : ioPool_(1),   // single worker
      publicationAuthority_(std::make_shared<AutosavePublicationAuthority>())
{
}

AutosaveManagerCore::~AutosaveManagerCore()
{
    (void) shutdown();
}

void AutosaveManagerCore::prepare(ProjectManager& pm, RecoverySessionCore* sessionCore)
{
    pm_          = &pm;
    sessionCore_ = sessionCore;
    writing_.store(false);
    if (publicationAuthority_ == nullptr)
        publicationAuthority_ = std::make_shared<AutosavePublicationAuthority>();

    stopTimer();
    if (!publicationAuthority_->beginGeneration())
    {
        // A previous admitted commit is still draining.  Keep the manager
        // unprepared: opening a new generation here could let an old commit
        // overlap a new generation's authoritative publication.
        prepared_ = false;
        shutdownStarted_ = true;
        lastShutdownDrained_ = false;
        return;
    }

    shutdownStarted_ = false;
    lastShutdownDrained_ = true;
    prepared_    = true;

    if (enabled_)
        startTimer(intervalSeconds_ * 1000);
}

bool AutosaveManagerCore::shutdown()
{
    if (shutdownStarted_)
        return lastShutdownDrained_;

    shutdownStarted_ = true;
    stopTimer();
    prepared_ = false;
    if (publicationAuthority_ != nullptr)
        publicationAuthority_->revoke();
    writing_.store(false);

    lastShutdownDrained_ = ioPool_.removeAllJobs(true, shutdownWaitTimeoutMs_);
    if (!lastShutdownDrained_)
    {
        juce::Logger::writeToLog(
            "[Autosave] Shutdown wait expired; publication authority remains revoked");
    }

    return lastShutdownDrained_;
}

void AutosaveManagerCore::setEnabled(bool on)
{
    enabled_ = on;
    if (prepared_)
    {
        if (on)  startTimer(intervalSeconds_ * 1000);
        else     stopTimer();
    }
}

void AutosaveManagerCore::setIntervalSeconds(int secs)
{
    intervalSeconds_ = juce::jmax(10, secs);
    if (prepared_ && enabled_)
        startTimer(intervalSeconds_ * 1000);
}

void AutosaveManagerCore::setKeepCount(int count)
{
    keepCount_ = juce::jmax(1, count);
}

void AutosaveManagerCore::markDirty(const juce::String& reason)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Check whether this is plugin-only dirty (throttle)
    bool isPluginChange = reason.startsWithIgnoreCase("plugin");

    // Every edit advances the revision, including throttled plugin-only edits.
    // An older in-flight autosave must never acknowledge a newer revision.
    ++dirtyRevision_;
    userDirty_.store(true);

    if (isPluginChange)
    {
        auto now = juce::Time::getCurrentTime();
        if ((now - lastPluginOnlyDirtyTime_).inSeconds() < kPluginThrottleSecs)
            return;   // throttle — don't set autosaveDirty_ yet
        lastPluginOnlyDirtyTime_ = now;
    }

    autosaveDirty_.store(true);
}

void AutosaveManagerCore::markCleanManualSave()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    // A completed manual save supersedes any older in-flight autosave.
    ++sessionEpoch_;
    userDirty_.store(false);
    // autosaveDirty_ is intentionally NOT cleared here — the next timer tick
    // will be a no-op since the manual save already wrote the project.
    autosaveDirty_.store(false);
}

void AutosaveManagerCore::onProjectLoaded()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // A new/restored project is a different recovery session, even if it has
    // the same path or the user has created another unsaved "Untitled" project.
    ++sessionEpoch_;
    dirtyRevision_ = 0;
    userDirty_.store(false);
    autosaveDirty_.store(false);
    lastPluginOnlyDirtyTime_ = {};
    lastAutosaveTime_ = {};
    diagRecordingFinalizedAutosave_.store(false);
    {
        const juce::ScopedWriteLock sl(fileLock_);
        latestAutosaveFile_ = {};
    }
    // Do not reset writing_ here: the previous job must drain before a new
    // project is allowed to start another autosave.
}

void AutosaveManagerCore::onRecordingStarted()   { recording_.store(true);  }
void AutosaveManagerCore::onRecordingFinalized()
{
    recording_.store(false);
    // Trigger immediately if we were dirty
    if (autosaveDirty_.load())
        triggerAutosaveNow("recording_finalized");
}

void AutosaveManagerCore::onExportStarted()      { exporting_.store(true);  }
void AutosaveManagerCore::onExportFinished()     { exporting_.store(false); }
void AutosaveManagerCore::onPluginScanStarted()  { scanning_.store(true);   }
void AutosaveManagerCore::onPluginScanFinished() { scanning_.store(false);  }

void AutosaveManagerCore::triggerAutosaveNow(const juce::String& reason)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    const bool isRecordingFinalized = reason == "recording_finalized";
    if (isRecordingFinalized)
        logAlways("[APEX-DIAG-CRASH] autosave(recording_finalized) ENTER");

    if (!prepared_ || !pm_) return;

    juce::String deferReason;
    if (!canAutosaveNow(deferReason))
    {
        juce::Logger::writeToLog("[Autosave] Deferred (" + deferReason + ")");
        if (isRecordingFinalized)
            logAlways("[APEX-DIAG-CRASH] autosave(recording_finalized) EXIT ok=0");
        return;
    }

    juce::Logger::writeToLog("[Autosave] Triggering autosave - reason: " + reason);
    if (isRecordingFinalized)
        diagRecordingFinalizedAutosave_.store(true);
    performAutosaveAsync();
}

bool AutosaveManagerCore::isAutosaveInProgress() const noexcept
{
    return writing_.load();
}

juce::File AutosaveManagerCore::getLatestAutosaveFile() const
{
    const juce::ScopedReadLock sl(fileLock_);
    return latestAutosaveFile_;
}

juce::Time AutosaveManagerCore::getLastAutosaveTime() const noexcept
{
    return lastAutosaveTime_;
}

// ── Private ──────────────────────────────────────────────────────────────────

void AutosaveManagerCore::timerCallback()
{
    if (!enabled_ || !autosaveDirty_.load()) return;

    juce::String deferReason;
    if (!canAutosaveNow(deferReason))
    {
        juce::Logger::writeToLog("[Autosave] Timer tick — deferred: " + deferReason);
        return;
    }

    performAutosaveAsync();
}

bool AutosaveManagerCore::canAutosaveNow(juce::String& reason) const
{
    if (writing_.load())  { reason = "write in progress";  return false; }

    if (pm_ != nullptr && !pm_->isProjectStateUsable())
    {
        reason = "project restore incomplete";
        return false;
    }

    bool rec = isRecordingQuery_ ? isRecordingQuery_() : recording_.load();
    if (rec)              { reason = "recording";          return false; }

    bool exp = isExportingQuery_ ? isExportingQuery_() : exporting_.load();
    if (exp)              { reason = "export in progress"; return false; }

    if (scanning_.load()) { reason = "plugin scan";        return false; }
    return true;
}

void AutosaveManagerCore::performAutosaveAsync()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!pm_ || !prepared_ || shutdownStarted_ || publicationAuthority_ == nullptr)
        return;

    const auto authority = publicationAuthority_;
    const auto generation = authority->currentGeneration();
    if (!authority->isGenerationOpen(generation))
        return;

    // Snapshot on message thread.  Sandbox state capture is a control-plane
    // operation whose failure must prevent a new autosave artifact from being
    // published; never serialize a partial tree as if it were valid.
    juce::ValueTree state;
    juce::String snapshotError;
    if (! pm_->buildAutosaveValueTree(state, snapshotError))
    {
        const auto reason = snapshotError.isNotEmpty()
            ? snapshotError
            : juce::String("Project state capture failed; autosave was not published.");
        juce::Logger::writeToLog("[Autosave] Snapshot rejected — " + reason);
        if (diagRecordingFinalizedAutosave_.exchange(false))
            logAlways("[APEX-DIAG-CRASH] autosave(recording_finalized) EXIT ok=0");
        if (onAutosaveFailed)
            onAutosaveFailed(reason);
        return;
    }

    // Capture the document and mutation revision represented by this snapshot.
    // Both counters are message-thread-only; completion also runs on it.
    const auto snapshotRevision = dirtyRevision_;
    const auto snapshotEpoch = sessionEpoch_;

    // Determine target directory
    auto projectFile = pm_->getCurrentProjectFile();
    auto projectName = pm_->getProjectName();
    if (projectName.isEmpty()) projectName = "Untitled";

    juce::File autosaveDir;
    if (projectFile != juce::File() && projectFile.getParentDirectory().isDirectory())
    {
        autosaveDir = projectFile.getParentDirectory().getChildFile(".apex_autosaves");
    }
    else
    {
        // Unsaved project
        autosaveDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("APEX")
                          .getChildFile("UnsavedProjectAutosaves");
    }

    writing_.store(true);

    juce::WeakReference<AutosaveManagerCore> weakThis(this);

    auto completionCallback = [weakThis, authority, generation, projectFile, snapshotRevision, snapshotEpoch]
        (bool success, juce::File writtenFile, juce::String failureReason)
    {
        // Runs on background thread — schedule UI/state updates back to message thread
        juce::MessageManager::callAsync([weakThis, authority, generation, success,
                                          writtenFile, projectFile, failureReason,
                                          snapshotRevision, snapshotEpoch]()
        {
            if (authority == nullptr || !authority->isGenerationOpen(generation))
                return;

            auto* self = weakThis.get();
            if (self == nullptr || self->shutdownStarted_ || !self->prepared_)
                return;

            self->writing_.store(false);

            // A previous project or a snapshot superseded by manual Save must
            // not alter the new session's dirty state, recovery lock or UI.
            if (self->sessionEpoch_ != snapshotEpoch)
            {
                self->diagRecordingFinalizedAutosave_.store(false);
                return;
            }

            if (success)
            {
                // Preserve edits made after the captured snapshot.
                self->autosaveDirty_.store(self->dirtyRevision_ != snapshotRevision);
                self->lastAutosaveTime_ = juce::Time::getCurrentTime();

                {
                    const juce::ScopedWriteLock sl(self->fileLock_);
                    self->latestAutosaveFile_ = writtenFile;
                }

                // Update session lock with new autosave path
                if (self->sessionCore_)
                {
                    bool unsaved = (projectFile == juce::File());
                    self->sessionCore_->updateCurrentProject(projectFile, writtenFile, unsaved);
                }

                auto timeStr = self->lastAutosaveTime_.formatted("%I:%M %p");
                juce::Logger::writeToLog("[Autosave] Success at " + timeStr);
                if (self->onAutosaveSucceeded) self->onAutosaveSucceeded(timeStr);
            }
            else
            {
                const auto reason = failureReason.isNotEmpty()
                    ? failureReason
                    : juce::String("check disk permissions");
                juce::Logger::writeToLog("[Autosave] Failed — " + reason);
                if (self->onAutosaveFailed) self->onAutosaveFailed(reason);
            }

            if (self->diagRecordingFinalizedAutosave_.exchange(false))
                logAlways("[APEX-DIAG-CRASH] autosave(recording_finalized) EXIT ok=" + juce::String(success ? 1 : 0));
        });
    };

    int kc = keepCount_;
    if (!prepared_ || shutdownStarted_ || !authority->isGenerationOpen(generation))
    {
        writing_.store(false);
        return;
    }

    ioPool_.addJob(new detail::AutosaveWriteJob(std::move(state),
                                                autosaveDir,
                                                projectName,
                                                kc,
                                                authority,
                                                generation,
                                                std::move(completionCallback)),
                   /*deleteWhenDone=*/true);
}

} // namespace DAW
