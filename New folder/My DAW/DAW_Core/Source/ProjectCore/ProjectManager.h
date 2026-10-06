#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../TransportCore/TransportController.h"
#include "../MarkerCore/MarkerManager.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../MixerScaleCore/FaderRangeCore.h"
#include "../StateCore/ApplicationState.h"

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

    /**
     * Build a complete ValueTree snapshot for autosave.
     * Delegates to the existing buildState() — no separate serialization path.
     */
    juce::ValueTree buildAutosaveValueTree() const { return buildState(); }

    // ── Save / Load ───────────────────────────────────────────────────────────

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
        auto state = buildState();
        auto xml   = state.createXml();
        if (!xml) return false;

        // Create versioned backup of existing file
        if (file.existsAsFile())
            createBackup(file);

        if (xml->writeTo(file))
        {
            projectFile_ = file;
            projectName_ = file.getFileNameWithoutExtension();
            dirty_        = false;
            listeners_.call([](Listener& l) { l.projectSaved(); });
            return true;
        }
        return false;
    }

    /** Load project state from the specified file. */
    bool loadFromFile(const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse(file);
        if (!xml) return false;

        auto state = juce::ValueTree::fromXml(*xml);
        if (!state.isValid() || state.getType().toString() != "DAWProject")
            return false;

        restoreFromState(state);
        projectFile_ = file;
        projectName_ = file.getFileNameWithoutExtension();
        dirty_        = false;
        listeners_.call([](Listener& l) { l.projectLoaded(); });
        return true;
    }

    /** Reset to a fresh empty project. */
    void newProject()
    {
        if (subs_.tracks)    subs_.tracks->deleteAllTracks();
        if (subs_.clips)     subs_.clips->deleteAllClips();
        if (subs_.markers)   subs_.markers->removeAllMarkers();
        if (subs_.transport) { subs_.transport->stop(); subs_.transport->setPosition(0); }

        projectFile_ = juce::File();
        projectName_ = "Untitled";
        dirty_        = false;

        listeners_.call([](Listener& l) { l.projectLoaded(); });
    }

    // ── State building (for external use, e.g., portable packages) ────────────

    /** Build a complete ValueTree snapshot of the project. */
    juce::ValueTree buildState() const;

    /** Restore all subsystems from a ValueTree snapshot. */
    void restoreFromState(const juce::ValueTree& state);

    // ── Autosave ──────────────────────────────────────────────────────────────

    void setAutosaveEnabled(bool on) { autosaveEnabled_ = on; }
    bool isAutosaveEnabled() const   { return autosaveEnabled_; }

    void setAutosaveInterval(int ms) { autosaveIntervalMs_ = ms; startTimer(ms); }
    int  getAutosaveInterval() const { return autosaveIntervalMs_; }

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

private:
    Subsystems   subs_;
    juce::File   projectFile_;
    juce::String projectName_       = "Untitled";
    bool         dirty_             = false;
    bool         autosaveEnabled_   = true;
    int          autosaveIntervalMs_ = 60000; // 1 minute

    juce::ListenerList<Listener> listeners_;

    // ── Backup helpers ────────────────────────────────────────────────────────

    void createBackup(const juce::File& original)
    {
        auto backupDir = getBackupDir();
        backupDir.createDirectory();

        auto backupFile = backupDir.getChildFile(
            original.getFileNameWithoutExtension() + "_"
            + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S")
            + ".dawproj");
        original.copyFileTo(backupFile);

        // Rotate — keep max 20 backups
        auto backups = backupDir.findChildFiles(
            juce::File::findFiles, false, "*.dawproj");
        backups.sort();
        while (backups.size() > 20)
        {
            backups[0].deleteFile();
            backups.remove(0);
        }
    }

    void timerCallback() override
    {
        if (!autosaveEnabled_ || !dirty_) return;

        auto backupDir = getBackupDir();
        backupDir.createDirectory();
        auto autosaveFile = backupDir.getChildFile("autosave.dawproj");

        auto state = buildState();
        auto xml   = state.createXml();
        if (xml && xml->writeTo(autosaveFile))
            listeners_.call([](Listener& l) { l.autosaveCompleted(); });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectManager)
};

} // namespace DAW
