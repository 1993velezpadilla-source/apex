#include "AutosaveManagerCore.h"
#include "ProjectManager.h"
#include "RecoverySessionCore.h"

namespace DAW {

// ── Background write job ─────────────────────────────────────────────────────

class AutosaveWriteJob : public juce::ThreadPoolJob
{
public:
    AutosaveWriteJob(juce::ValueTree stateToWrite,
                     juce::File      targetDir,
                     juce::String    projectName,
                     int             keepCount,
                     std::function<void(bool, juce::File)> completionCallback)
        : juce::ThreadPoolJob("AutosaveWrite"),
          state_      (std::move(stateToWrite)),
          targetDir_  (std::move(targetDir)),
          projectName_(std::move(projectName)),
          keepCount_  (keepCount),
          onComplete_ (std::move(completionCallback))
    {}

    JobStatus runJob() override
    {
        targetDir_.createDirectory();

        auto latestFile = targetDir_.getChildFile(projectName_ + ".autosave.latest.apex");
        auto tmpFile    = targetDir_.getChildFile(projectName_ + ".autosave.latest.tmp");

        // 1. Write to .tmp
        bool writeOk = false;
        {
            juce::FileOutputStream out(tmpFile);
            if (out.openedOk())
            {
                auto xml = state_.createXml();
                if (xml != nullptr)
                {
                    xml->writeTo(out);
                    writeOk = !out.failedToOpen() && out.getStatus().wasOk();
                }
            }
        }

        if (!writeOk)
        {
            tmpFile.deleteFile();
            juce::Logger::writeToLog("[Autosave] Write failed - tmp file deleted");
            if (onComplete_) onComplete_(false, {});
            return jobHasFinished;
        }

        // 2. Rotate: rename existing .latest → numbered slot
        if (latestFile.existsAsFile())
        {
            rotateBackups(latestFile);
        }

        // 3. Rename tmp → latest (atomic on NTFS)
        if (!tmpFile.moveFileTo(latestFile))
        {
            juce::Logger::writeToLog("[Autosave] Rename tmp->latest failed. Keeping tmp, not corrupting latest.");
            if (onComplete_) onComplete_(false, {});
            return jobHasFinished;
        }

        juce::Logger::writeToLog("[Autosave] Autosave written: " + latestFile.getFullPathName());
        if (onComplete_) onComplete_(true, latestFile);
        return jobHasFinished;
    }

private:
    void rotateBackups(const juce::File& latestFile)
    {
        // Find next available slot number
        int nextSlot = 1;
        for (int i = keepCount_; i >= 1; --i)
        {
            auto candidate = targetDir_.getChildFile(
                projectName_ + ".autosave." + juce::String(i).paddedLeft('0', 3) + ".apex");
            if (!candidate.existsAsFile())
            {
                nextSlot = i;
                break;
            }
            nextSlot = i;
        }

        auto dest = targetDir_.getChildFile(
            projectName_ + ".autosave." + juce::String(nextSlot).paddedLeft('0', 3) + ".apex");

        latestFile.moveFileTo(dest);

        // Prune: delete numbered backups beyond keepCount_
        auto all = targetDir_.findChildFiles(
            juce::File::findFiles, false, projectName_ + ".autosave.???.apex");
        all.sort();

        while (all.size() > keepCount_)
        {
            auto& oldest = all.getReference(0);
            if (oldest.moveToTrash())
                ;
            else
                oldest.deleteFile();
            all.remove(0);
        }
    }

    juce::ValueTree state_;
    juce::File      targetDir_;
    juce::String    projectName_;
    int             keepCount_;
    std::function<void(bool, juce::File)> onComplete_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutosaveWriteJob)
};

// ── AutosaveManagerCore ──────────────────────────────────────────────────────

AutosaveManagerCore::AutosaveManagerCore()
    : ioPool_(1)   // single worker
{
}

AutosaveManagerCore::~AutosaveManagerCore()
{
    shutdown();
}

void AutosaveManagerCore::prepare(ProjectManager& pm, RecoverySessionCore* sessionCore)
{
    pm_          = &pm;
    sessionCore_ = sessionCore;
    prepared_    = true;

    if (enabled_)
        startTimer(intervalSeconds_ * 1000);
}

void AutosaveManagerCore::shutdown()
{
    stopTimer();
    ioPool_.removeAllJobs(true, 5000);
    prepared_ = false;
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
    userDirty_.store(false);
    // autosaveDirty_ is intentionally NOT cleared here — the next timer tick
    // will be a no-op since the manual save already wrote the project.
    autosaveDirty_.store(false);
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

    if (!prepared_ || !pm_) return;

    juce::String deferReason;
    if (!canAutosaveNow(deferReason))
    {
        juce::Logger::writeToLog("[Autosave] Deferred (" + deferReason + ")");
        return;
    }

    juce::Logger::writeToLog("[Autosave] Triggering autosave - reason: " + reason);
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
        juce::Logger::writeToLog("[Autosave] Timer tick - deferred: " + deferReason);
        return;
    }

    performAutosaveAsync();
}

bool AutosaveManagerCore::canAutosaveNow(juce::String& reason) const
{
    if (writing_.load())  { reason = "write in progress";  return false; }

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
    if (!pm_) return;

    // Snapshot on message thread
    auto state = pm_->buildAutosaveValueTree();

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

    auto* sessionCoreCapture = sessionCore_;

    auto completionCallback = [this, sessionCoreCapture, projectFile]
        (bool success, juce::File writtenFile)
    {
        // Runs on background thread — schedule UI/state updates back to message thread
        juce::MessageManager::callAsync([this, success, writtenFile, projectFile, sessionCoreCapture]()
        {
            writing_.store(false);

            if (success)
            {
                autosaveDirty_.store(false);
                lastAutosaveTime_ = juce::Time::getCurrentTime();

                {
                    const juce::ScopedWriteLock sl(fileLock_);
                    latestAutosaveFile_ = writtenFile;
                }

                // Update session lock with new autosave path
                if (sessionCoreCapture)
                {
                    bool unsaved = (projectFile == juce::File());
                    sessionCoreCapture->updateCurrentProject(projectFile, writtenFile, unsaved);
                }

                auto timeStr = lastAutosaveTime_.formatted("%I:%M %p");
                juce::Logger::writeToLog("[Autosave] Success at " + timeStr);
                if (onAutosaveSucceeded) onAutosaveSucceeded(timeStr);
            }
            else
            {
                juce::Logger::writeToLog("[Autosave] Failed - check disk permissions");
                if (onAutosaveFailed) onAutosaveFailed("check disk permissions");
            }
        });
    };

    int kc = keepCount_;
    ioPool_.addJob(new AutosaveWriteJob(std::move(state),
                                        autosaveDir,
                                        projectName,
                                        kc,
                                        std::move(completionCallback)),
                   /*deleteWhenDone=*/true);
}

} // namespace DAW
