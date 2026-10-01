#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../TransportCore/TransportController.h"
#include "../MarkerCore/MarkerManager.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../MixerScaleCore/FaderRangeCore.h"
#include "../StateCore/ApplicationState.h"
#include "FilePublicationCore.h"
#include "ProjectUpgradeReport.h"

// Forward declarations
namespace DAW {
    class ApplicationCore;
    class MixerFolderIntegrationCore;
}

namespace DAW {

/**
 * ProjectManager — save/load/autosave orchestration.
 *
 * Serializes all subsystem state into a single ValueTree.
 * Writes to XML with .dawproj extension.
 *
 * Features:
 *  - Save / Load (file path provided by caller — no UI dialogs here)
 *  - Periodic autosave to backup directory
 *  - Versioned backup rotation (max 20 backups)
 *  - Dirty tracking
 *  - Listener for UI sync
 *
 * The caller (MainComponent / DAWMenuBar) is responsible for
 * showing file chooser dialogs and calling saveToFile/loadFromFile.
 */
class ProjectManager : private juce::Timer
{
public:
    /** Subsystem references — set once after all subsystems are created. */
    struct Subsystems
    {
        TrackManager*        tracks    = nullptr;
        ClipManager*         clips     = nullptr;
        TransportController* transport = nullptr;
        MarkerManager*       markers   = nullptr;
        RoutingGraph*        routing   = nullptr;
        FaderRangeCore*      faderRange = nullptr;
        ApplicationState*    appState  = nullptr;
        ApplicationCore*     appCore = nullptr;  // For plugin chain serialization
        class FolderBusCore* folderBus = nullptr;
        class MasterRouteStateCore* masterRoute = nullptr;
        class AutomationManagerCore* automation = nullptr;
    };

    ProjectManager() { startTimer(autosaveIntervalMs_); }
    ~ProjectManager() override { stopTimer(); }

    void setSubsystems(const Subsystems& s);

    // ── Project info ──────────────────────────────────────────────────────────

    juce::String getProjectName() const        { return projectName_; }
    juce::File   getProjectFile() const        { return projectFile_; }
    juce::File   getCurrentProjectFile() const { return projectFile_; }
    bool         isDirty() const               { return dirty_; }
    bool         isUserDirty() const noexcept  { return dirty_; }
    void         markDirty()                   { dirty_ = true; }

    /** Optional message-thread-only augmentation for one manual-save snapshot.
     *  The callback runs after the complete state is built and before XML is
     *  written, so the resulting state still uses the normal single
     *  transactional publication boundary. */
    using SaveStateAugmenter = std::function<bool(juce::ValueTree&, juce::String&)>;

    /**
     * Build a complete ValueTree snapshot for autosave.
     * Delegates to the existing buildState() — no separate serialization path.
     */
    juce::ValueTree buildAutosaveValueTree() const { return buildState(); }
    bool buildAutosaveValueTree(juce::ValueTree& state,
                                juce::String& error) const
    {
        return buildState(state, error);
    }

    // ── Save / Load ───────────────────────────────────────────────────────────

    /** Report of everything the load-time repair chain changed in the most
     *  recently loaded project. The "Upgrade Project…" tool reads this after
     *  a load to show the user exactly what was repaired. */
    const ProjectUpgradeReport& getLastUpgradeReport() const noexcept
    {
        return lastUpgradeReport_;
    }

    /** Save current state to the active project file. Returns false if no file set. */
    bool save()
    {
        if (projectFile_ == juce::File())
            return false;
        return saveToFile(projectFile_);
    }

    /** Save current state to the specified file. */
    bool saveToFile(const juce::File& file)
    {
        return saveToFile(file, SaveStateAugmenter{});
    }

    /** Save current state to the specified file with an optional pre-save
     *  state augmentation.
     *
     *  Crash-safe: writes to a temporary file first, then transactionally
     *  replaces the target.  On publication failure the previous project
     *  file remains intact and the candidate is never authoritative.
     */
    bool saveToFile(const juce::File& file,
                    const SaveStateAugmenter& augmentState)
    {
        lastSaveError_.clear();
        if (!projectStateUsable_)
        {
            lastSaveError_ = "Project state is not usable; save was not published.";
            return false;
        }

        juce::ValueTree state;
        if (! buildState(state, lastSaveError_))
        {
            if (lastSaveError_.isEmpty())
                lastSaveError_ = "Project state capture failed; save was not published.";
            juce::Logger::writeToLog("[PROJECT] save rejected: " + lastSaveError_);
            return false;
        }
        if (augmentState && !augmentState(state, lastSaveError_))
        {
            if (lastSaveError_.isEmpty())
                lastSaveError_ = "Project state augmentation failed; save was not published.";
            juce::Logger::writeToLog("[PROJECT] save state augmentation rejected: "
                                     + lastSaveError_);
            return false;
        }

        auto xml   = state.createXml();
        if (!xml)
        {
            lastSaveError_ = "Project state could not be converted to XML.";
            return false;
        }

        // Write and close a unique candidate in the destination directory.
        // FilePublicationCore owns the final replacement; do not use
        // delete-then-move here because a failed move would lose the last
        // known-good project.
        juce::TemporaryFile temporary(file, juce::TemporaryFile::useHiddenFile);
        bool candidateReady = false;
        {
            juce::FileOutputStream out(temporary.getFile());
            if (out.openedOk())
            {
                xml->writeTo(out);
                out.flush();
                candidateReady = out.getStatus().wasOk();
            }
        }

        if (!candidateReady || !temporary.getFile().existsAsFile())
        {
            lastSaveError_ = "Project XML could not be written to the temporary file.";
            return false;
        }

        // Reopen the candidate through the normal XML parser before changing
        // the canonical pathname.  This catches a structurally incomplete
        // candidate independently of the stream status.
        if (juce::XmlDocument::parse(temporary.getFile()) == nullptr)
        {
            lastSaveError_ = "Project XML candidate could not be parsed.";
            return false;
        }

        // Historical backup is optional and is never rollback authority.  A
        // failed backup must not make the canonical publication unsafe.
        if (file.existsAsFile())
        {
            juce::String backupError;
            if (!createBackup(file, backupError))
                juce::Logger::writeToLog("[PROJECT] optional backup failed: " + backupError);
        }

        if (!DAW::publishFileTransactionally(temporary.getFile(), file, lastSaveError_))
        {
            if (lastSaveError_.isEmpty())
                lastSaveError_ = "Temporary project file could not be published.";
            juce::Logger::writeToLog("[PROJECT] save publication rejected: " + lastSaveError_);
            return false;
        }

        projectFile_ = file;
        projectName_ = file.getFileNameWithoutExtension();
        dirty_        = false;
        listeners_.call([](Listener& l) { l.projectSaved(); });
        return true;
    }

    /** Load project state from the specified file. */
    bool loadFromFile(const juce::File& file);

    /** Reset to a fresh empty project. */
    bool newProject();

    // ── State building (for external use, e.g., portable packages) ────────────

    /** Build a complete ValueTree snapshot of the project. */
    juce::ValueTree buildState() const;
    /** Result-bearing snapshot used by manual save and autosave publication. */
    bool buildState(juce::ValueTree& state, juce::String& error) const;

    /** Restore all subsystems from a ValueTree snapshot. */
    bool restoreFromState(const juce::ValueTree& state);

    bool isProjectStateUsable() const noexcept { return projectStateUsable_; }
    juce::String getLastLoadError() const { return lastLoadError_; }
    juce::String getLastLoadWarning() const { return lastLoadWarning_; }
    juce::String getLastSaveError() const { return lastSaveError_; }

    struct LoadProgress
    {
        juce::String stage;
        juce::String currentItem;
        int completed = 0;
        int total = 0;
        double fraction = 0.0;
        bool determinate = true;
    };
    using LoadProgressCallback = std::function<void(const LoadProgress&)>;
    void setLoadProgressCallback(LoadProgressCallback callback) { loadProgressCallback_ = std::move(callback); }
    const std::vector<TrackID>& getLastLoadedCollapsedFolderIds() const noexcept
    {
        return lastLoadedCollapsedFolderIds_;
    }

    // ── Autosave ──────────────────────────────────────────────────────────────

    void setAutosaveEnabled(bool on)
    {
        if (externalAutosaveManaged_ && on)
            return;

        autosaveEnabled_ = on;
        if (on)
            startTimer(autosaveIntervalMs_);
        else
            stopTimer();
    }
    bool isAutosaveEnabled() const   { return autosaveEnabled_; }

    /**
     * ApplicationCore owns the production autosave worker.  Standalone tools
     * may release this mode and retain ProjectManager's transactional timer.
     */
    void setExternalAutosaveManaged(bool externallyManaged)
    {
        externalAutosaveManaged_ = externallyManaged;
        setAutosaveEnabled(!externallyManaged);
    }

    bool isExternalAutosaveManaged() const noexcept
    {
        return externalAutosaveManaged_;
    }

    void setAutosaveInterval(int ms)
    {
        autosaveIntervalMs_ = ms;
        if (autosaveEnabled_)
            startTimer(ms);
        else
            stopTimer();
    }
    int  getAutosaveInterval() const { return autosaveIntervalMs_; }

    /** Run the standalone legacy autosave path once on the control thread. */
    bool performAutosaveNow();

    /** Get the backup directory for the current project. */
    juce::File getBackupDir() const
    {
        if (projectFile_ == juce::File())
            return juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("DAWCore_Backups");
        return projectFile_.getParentDirectory().getChildFile(".backups");
    }

    // ── Listener ──────────────────────────────────────────────────────────────

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void projectSaved() {}
        virtual void projectLoaded() {}
        virtual void autosaveCompleted() {}
    };

    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    /** Outcome of the last project load's sample-rate reconcile
     *  (pre-beta project integrity, 2026-07-26). Action values match
     *  ProjectSampleRateReconcile::Action: 0 = none, 1 = reconciled,
     *  2 = legacy/unverified, 3 = device rate unknown. The GUI phase can
     *  surface this to the user; the load itself is never blocked. */
    struct LoadRateReport
    {
        int    action = 0;
        double projectRate = 0.0;   // stored rate (0 = absent)
        double deviceRate = 0.0;    // granted device rate at load
        double factor = 1.0;        // deviceRate/projectRate when reconciled
        bool   metadataPresent = false;
    };
    LoadRateReport getLastLoadRateReport() const noexcept { return lastLoadRateReport_; }

private:
    Subsystems   subs_;
    juce::File   projectFile_;
    ProjectUpgradeReport lastUpgradeReport_;
    juce::String projectName_       = "Untitled";
    bool         dirty_             = false;
    bool         autosaveEnabled_   = true;
    bool         externalAutosaveManaged_ = false;
    int          autosaveIntervalMs_ = 60000; // 1 minute
    LoadRateReport lastLoadRateReport_;
    bool         projectStateUsable_  = true;
    juce::String lastLoadError_;
    juce::String lastLoadWarning_;
    juce::String lastSaveError_;
    LoadProgressCallback loadProgressCallback_;
    std::vector<TrackID> lastLoadedCollapsedFolderIds_;

    juce::ListenerList<Listener> listeners_;

    void reportLoadProgress(const juce::String& stage,
                            double fraction,
                            const juce::String& currentItem = {},
                            int completed = 0,
                            int total = 0,
                            bool determinate = true)
    {
        if (loadProgressCallback_)
            loadProgressCallback_({ stage, currentItem, completed, total,
                                    juce::jlimit(0.0, 1.0, fraction), determinate });
    }

    // ── Backup helpers ────────────────────────────────────────────────────────

    bool createBackup(const juce::File& original, juce::String& error)
    {
        error.clear();
        auto backupDir = original.getParentDirectory().getChildFile(".backups");
        if (backupDir.createDirectory().failed())
        {
            error = "Could not create backup directory: "
                + backupDir.getFullPathName();
            return false;
        }

        const auto backupStem = original.getFileNameWithoutExtension() + "_"
            + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        auto backupFile = backupDir.getChildFile(backupStem + ".dawproj");
        if (backupFile.existsAsFile())
            backupFile = backupDir.getNonexistentChildFile(backupStem, ".dawproj", false);

        if (!DAW::FilePublicationCore::copyHistoricalFile(original, backupFile, error))
            return false;

        // Rotate — keep max 20 backups
        auto backups = backupDir.findChildFiles(
            juce::File::findFiles, false, "*.dawproj");
        backups.sort();
        while (backups.size() > 20)
        {
            if (!DAW::FilePublicationCore::deleteHistoricalFile(backups[0], error))
                return false;
            backups.remove(0);
        }

        return true;
    }

    void timerCallback() override
    {
        if (performAutosaveNow())
            listeners_.call([](Listener& l) { l.autosaveCompleted(); });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectManager)
};

} // namespace DAW
