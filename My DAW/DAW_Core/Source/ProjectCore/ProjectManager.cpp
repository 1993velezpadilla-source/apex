#include "ProjectManager.h"
#include "../AppCore/ApplicationCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../Automation/AutomationLaneStoreCore.h"
#include "../VocalTuneCore/ApexTuneIntegrationCore.h"
#include "../VocalTuneCore/ApexTuneProjectStateCore.h"
#include "ProjectSampleRateReconcileCore.h"

namespace DAW {

static constexpr int kCurrentProjectVersion = 9;   // v9: clip-local tape stop lanes + sidechain bus resolution

void ProjectManager::setSubsystems(const Subsystems& s)
{
    subs_ = s;

    if (subs_.clips)
    {
        subs_.clips->onClipRemoved = [this](const ClipID& clipId)
        {
            if (subs_.automation == nullptr || subs_.clips == nullptr)
                return;

            subs_.automation->removeOrphanedClipAutomation(
                [clips = subs_.clips](const juce::String& clipId) -> bool
                {
                    return clips->getClip(clipId) != nullptr;
                });

            if (subs_.appCore != nullptr)
                if (auto* integration = subs_.appCore->getVocalTuneIntegrationPtr())
                    integration->onClipDeleted(clipId);
        };
    }
}

bool ProjectManager::performAutosaveNow()
{
    if (!autosaveEnabled_ || !dirty_ || !projectStateUsable_)
        return false;

    juce::ValueTree state;
    juce::String error;
    if (!buildState(state, error))
    {
        if (error.isEmpty())
            error = "Project state capture failed; autosave was not published.";
        juce::Logger::writeToLog("[PROJECT] legacy autosave rejected: " + error);
        return false;
    }

    auto xml = state.createXml();
    if (xml == nullptr)
    {
        juce::Logger::writeToLog("[PROJECT] legacy autosave rejected: XML conversion failed");
        return false;
    }

    const auto backupDir = getBackupDir();
    const auto directoryResult = backupDir.createDirectory();
    if (directoryResult.failed())
    {
        juce::Logger::writeToLog("[PROJECT] legacy autosave directory failed: "
                                 + directoryResult.getErrorMessage());
        return false;
    }

    const auto autosaveFile = backupDir.getChildFile("autosave.dawproj");
    juce::TemporaryFile temporary(autosaveFile, juce::TemporaryFile::useHiddenFile);
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
        juce::Logger::writeToLog("[PROJECT] legacy autosave candidate write failed");
        return false;
    }

    if (juce::XmlDocument::parse(temporary.getFile()) == nullptr)
    {
        juce::Logger::writeToLog("[PROJECT] legacy autosave candidate parse failed");
        return false;
    }

    juce::String publicationError;
    if (!DAW::publishFileTransactionally(temporary.getFile(),
                                         autosaveFile,
                                         publicationError))
    {
        juce::Logger::writeToLog("[PROJECT] legacy autosave publication rejected: "
                                 + publicationError);
        return false;
    }

    return true;
}

bool ProjectManager::buildState(juce::ValueTree& state,
                                juce::String& error) const
{
    state = juce::ValueTree("DAWProject");
    error.clear();
    state.setProperty("version", kCurrentProjectVersion, nullptr);
    state.setProperty("name",    projectName_, nullptr);
    state.setProperty("savedAt", juce::Time::getCurrentTime().toISO8601(true), nullptr);

    // Project sample-rate identity (pre-beta integrity): persist the
    // authoritative device-granted rate ONLY when it is proven. A missing
    // property is meaningful (legacy/unprovable) — never invent one (Brain §3).
    if (subs_.appCore)
    {
        const double projectRate = subs_.appCore->getCurrentSampleRate();
        if (projectRate > 0.0)
            state.setProperty("projectSampleRate", projectRate, nullptr);
    }
    juce::ValueTree masterPhaseD("MasterPhaseD");
    masterPhaseD.setProperty("masterGain", 1.0f, nullptr);

    // Read live ceiling state from the audio engine rather than hardcoding.
    // This preserves the user's selected ceiling mode and dB value across save/load.
    if (subs_.appCore)
    {
        auto& ceiling = subs_.appCore->getMasterBus().getCeiling();
        masterPhaseD.setProperty("ceilingMode",
            DAW::MasterCeilingCore::modeToString(ceiling.getMode()), nullptr);
        masterPhaseD.setProperty("ceilingDb", ceiling.getCeilingDb(), nullptr);
    }
    else
    {
        masterPhaseD.setProperty("ceilingMode", "SoftClip", nullptr);
        masterPhaseD.setProperty("ceilingDb", -0.1f, nullptr);
    }

    masterPhaseD.setProperty("ditherMode", "Off", nullptr);
    masterPhaseD.setProperty("ditherBits", 24, nullptr);
    masterPhaseD.setProperty("meterDisplay", "PostFader", nullptr);
    state.addChild(masterPhaseD, -1, nullptr);

    if (subs_.tracks)     state.addChild(subs_.tracks->getState().createCopy(), -1, nullptr);
    if (subs_.clips)
    {
        auto clipsState = subs_.clips->getState().createCopy();

        if (subs_.appCore != nullptr)
        {
            if (auto* integration = subs_.appCore->getVocalTuneIntegrationPtr())
            {
                for (int i = 0; i < clipsState.getNumChildren(); ++i)
                {
                    auto clipTree = clipsState.getChild(i);
                    const auto clipId = clipTree.getProperty("id").toString();
                    auto* clip = subs_.clips->getClip(clipId);
                    if (clip == nullptr || clip->getType() != ClipType::Audio)
                        continue;

                    if (auto vocalState = integration->getClipState(clipId))
                        clipTree.appendChild(apex::vocaltune::ApexTuneProjectStateCore::toValueTree(*vocalState), nullptr);
                }
            }
        }

        state.addChild(clipsState, -1, nullptr);
    }
    if (subs_.markers)    state.addChild(subs_.markers->getState().createCopy(), -1, nullptr);
    if (subs_.routing)    state.addChild(subs_.routing->getState().createCopy(), -1, nullptr);
    if (subs_.faderRange) state.addChild(subs_.faderRange->toValueTree().createCopy(), -1, nullptr);
    if (subs_.appState)   state.addChild(subs_.appState->getState().createCopy(), -1, nullptr);
    if (subs_.folderBus)  state.addChild(subs_.folderBus->getState().createCopy(), -1, nullptr);
    if (subs_.automation)
    {
        auto clipExists = [clips = subs_.clips](const juce::String& clipId) -> bool
        {
            return clips != nullptr && clips->getClip(clipId) != nullptr;
        };

        state.addChild(subs_.automation->getStateFilteredByClipExistence(subs_.clips ? std::function<bool(const juce::String&)>(clipExists)
                                                                                     : std::function<bool(const juce::String&)>{}).createCopy(),
                       -1,
                       nullptr);
    }

    auto apexAutomationState = apex::automation::AutomationLaneStore::getInstance().getState();
    if (apexAutomationState.isValid() && apexAutomationState.getNumChildren() > 0)
        state.addChild(apexAutomationState.createCopy(), -1, nullptr);

    // Plugin chains - delegate to ApplicationCore
    if (subs_.appCore)
    {
        state.addChild(subs_.appCore->getClickStateTree().createCopy(), -1, nullptr);

        juce::ValueTree pluginState;
        if (! subs_.appCore->capturePluginChainsState(pluginState, error))
        {
            state = juce::ValueTree();
            return false;
        }
        if (pluginState.isValid())
            state.addChild(pluginState.createCopy(), -1, nullptr);

        // Quick Track Builder project-scoped role-color families — saved
        // with the project so reload restores exact colors (no rerandomize).
        auto quickTrackColors = subs_.appCore->getQuickTrackColorsState();
        if (quickTrackColors.isValid())
            state.addChild(quickTrackColors.createCopy(), -1, nullptr);
    }

    if (subs_.transport)
    {
        juce::ValueTree tv("Transport");
        tv.setProperty("tempo",    subs_.transport->getTempo(),                  nullptr);
        tv.setProperty("position", (juce::int64)subs_.transport->getPosition(),  nullptr);
        tv.setProperty("looping",  subs_.transport->isLooping(),                 nullptr);
        state.addChild(tv, -1, nullptr);
    }

    return true;
}

juce::ValueTree ProjectManager::buildState() const
{
    juce::ValueTree state;
    juce::String error;
    if (! buildState(state, error))
    {
        if (error.isNotEmpty())
            juce::Logger::writeToLog("[PROJECT] state snapshot rejected: " + error);
        return {};
    }
    return state;
}

bool ProjectManager::loadFromFile(const juce::File& file)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    lastLoadError_.clear();
    lastLoadWarning_.clear();
    reportLoadProgress("Reading project", 0.02, file.getFileName(), 0, 1);

    auto xml = juce::XmlDocument::parse(file);
    if (!xml)
    {
        lastLoadError_ = "Project XML could not be parsed";
        return false;
    }

    auto state = juce::ValueTree::fromXml(*xml);
    reportLoadProgress("Validating project", 0.06, file.getFileName(), 1, 1);
    if (!state.isValid() || !state.hasType("DAWProject"))
    {
        lastLoadError_ = "Project root is missing or invalid";
        return false;
    }

    const int storedVersion = (int)state.getProperty("version", 0);
    if (storedVersion > kCurrentProjectVersion)
    {
        lastLoadError_ = "Project version " + juce::String(storedVersion)
            + " is newer than supported version " + juce::String(kCurrentProjectVersion);
        return false;
    }

    lastLoadedCollapsedFolderIds_.clear();
    if (auto collapsed = state.getChildWithName("CollapsedFolders"); collapsed.isValid())
        for (int i = 0; i < collapsed.getNumChildren(); ++i)
        {
            const auto id = collapsed.getChild(i).getProperty("trackId").toString();
            if (id.isNotEmpty()) lastLoadedCollapsedFolderIds_.push_back(id);
        }

    try
    {
        if (!restoreFromState(state))
            return false;
    }
    catch (const std::exception& e)
    {
        lastLoadError_ = "Unhandled project restore exception: " + juce::String(e.what());
        juce::Logger::writeToLog("[PROJECT] " + lastLoadError_);
        return false;
    }
    catch (...)
    {
        lastLoadError_ = "Unhandled unknown project restore exception";
        juce::Logger::writeToLog("[PROJECT] " + lastLoadError_);
        return false;
    }

    projectFile_ = file;
    projectName_ = file.getFileNameWithoutExtension();
    dirty_ = false;
    reportLoadProgress("Finalizing project", 1.0, file.getFileName(), 1, 1);
    listeners_.call([](Listener& l) { l.projectLoaded(); });
    return true;
}

bool ProjectManager::newProject()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    lastLoadError_.clear();
    lastLoadedCollapsedFolderIds_.clear();

    if (subs_.appCore != nullptr && !subs_.appCore->beginProjectStateRestore())
    {
        lastLoadError_ = "New Project rejected while recording, finalizing, exporting, or waiting for realtime callbacks";
        return false;
    }

    bool success = false;
    {
        struct GateGuard
        {
            ApplicationCore* appCore = nullptr;
            bool succeeded = false;
            ~GateGuard() { if (appCore != nullptr) appCore->endProjectStateRestore(succeeded); }
        } gate { subs_.appCore };

        projectStateUsable_ = false;
        try
        {
            if (subs_.folderBus) subs_.folderBus->clear();
            if (subs_.tracks)    subs_.tracks->deleteAllTracks();
            if (subs_.clips)     subs_.clips->deleteAllClips();
            if (subs_.markers)   subs_.markers->removeAllMarkers();
            if (subs_.transport) { subs_.transport->stop(); subs_.transport->setPosition(0); }
            if (subs_.appCore)
            {
                subs_.appCore->clearAllPluginChains();
                subs_.appCore->reloadAudioFiles();
                // Quick Track Builder: fresh project-scoped role-color map.
                // The application-global plugin registry and the global
                // QuickTrack template store are NOT touched.
                subs_.appCore->resetQuickTrackColorsForNewProject();
            }

            projectFile_ = juce::File();
            projectName_ = "Untitled";
            dirty_ = false;
            projectStateUsable_ = true;
            success = true;
            gate.succeeded = true;
        }
        catch (const std::exception& e)
        {
            lastLoadError_ = "New Project failed after mutation began: " + juce::String(e.what());
        }
        catch (...)
        {
            lastLoadError_ = "New Project failed after mutation began with an unknown exception";
        }
    }

    if (!success)
    {
        juce::Logger::writeToLog("[PROJECT] " + lastLoadError_ + "; realtime audio remains suppressed");
        return false;
    }

    listeners_.call([](Listener& l) { l.projectLoaded(); });
    return true;
}

bool ProjectManager::restoreFromState(const juce::ValueTree& state)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    lastLoadError_.clear();
    lastLoadWarning_.clear();

    if (!state.isValid() || !state.hasType("DAWProject"))
    {
        lastLoadError_ = "Project restore rejected an invalid root";
        return false;
    }

    // C6-editor-lifetime: close every plugin editor while the audio engine is
    // still live — the proven-safe manual-close conditions. The restore then
    // never destroys a native editor window inside the suspended teardown
    // storm, so no plugin window proc can run against torn-down state.
    if (subs_.appCore != nullptr)
        subs_.appCore->closeAllPluginEditors();

    if (subs_.appCore != nullptr && !subs_.appCore->beginProjectStateRestore())
    {
        lastLoadError_ = "Project restore rejected while recording, finalizing, exporting, or waiting for realtime callbacks";
        return false;
    }

    bool restoreSucceeded = true;
    projectStateUsable_ = false;
    struct GateGuard
    {
        ApplicationCore* appCore = nullptr;
        bool& succeeded;
        ~GateGuard() { if (appCore != nullptr) appCore->endProjectStateRestore(succeeded); }
    } gate { subs_.appCore, restoreSucceeded };

    reportLoadProgress("Preparing project state", 0.09, {}, 0, 16);

    const auto recordFailure = [this, &restoreSucceeded](const juce::String& stage,
                                                         const juce::String& detail)
    {
        restoreSucceeded = false;
        const auto message = "restore " + stage + " failed"
            + (detail.isNotEmpty() ? ": " + detail : juce::String());
        if (lastLoadError_.isNotEmpty())
            lastLoadError_ += "; ";
        lastLoadError_ += message;
        juce::Logger::writeToLog("[PROJECT] " + message);
    };

    // CRITICAL: clear FolderBusCore maps BEFORE restoring tracks.
    // TrackManager::restoreState deletes all tracks, which fires trackRemoved
    // for every track.  Each trackRemoved callback rebuilds the FolderBus
    // mute/solo snapshot by walking FolderBusCore maps.  If those maps still
    // reference the OLD tracks (which are being destroyed), the rebuild walks
    // stale data and can crash.  Clearing here ensures rebuildFolderBusSnapshot()
    // sees an empty FolderBusCore and takes no action.
    if (subs_.folderBus)
    {
        try { subs_.folderBus->clear(); }
        catch (const std::exception& e) { recordFailure("FolderBus pre-clear", e.what()); }
        catch (...) { recordFailure("FolderBus pre-clear", "unknown exception"); }
    }

    // Each subsystem restore is guarded so diagnostics identify the owning
    // stage. Any caught failure makes the complete restore fail; callers must
    // not publish project identity or resume realtime traversal of partial
    // state.

    if (subs_.appCore)
    {
        reportLoadProgress("Clearing previous plugin state", 0.12, {}, 1, 16, false);
        try { subs_.appCore->clearAllPluginChains(); }
        catch (const std::exception& e) { recordFailure("PluginChain pre-clear", e.what()); }
        catch (...) { recordFailure("PluginChain pre-clear", "unknown exception"); }
    }

    if (subs_.faderRange)
    {
        try {
            auto fv = state.getChildWithName("FaderRange");
            subs_.faderRange->fromValueTree(fv);
        } catch (const std::exception& e) {
            recordFailure("FaderRange", e.what());
        } catch (...) {
            recordFailure("FaderRange", "unknown exception");
        }
    }
    if (subs_.tracks)
    {
        const auto tv = state.getChildWithName("Tracks");
        reportLoadProgress("Restoring tracks", 0.18, {}, 2, 16,
                           tv.isValid());
        try {
            if (tv.isValid()) subs_.tracks->restoreState(tv);
        } catch (const std::exception& e) {
            recordFailure("Tracks", e.what());
        } catch (...) {
            recordFailure("Tracks", "unknown exception");
        }
    }
    if (subs_.clips)
    {
        const auto cv = state.getChildWithName("Clips");
        reportLoadProgress("Restoring clips", 0.28, {}, 3, 16, cv.isValid());
        try {
            if (cv.isValid()) subs_.clips->restoreState(cv);

            if (cv.isValid() && subs_.appCore != nullptr)
            {
                if (auto* integration = subs_.appCore->getVocalTuneIntegrationPtr())
                {
                    for (int i = 0; i < cv.getNumChildren(); ++i)
                    {
                        auto clipTree = cv.getChild(i);
                        const auto clipId = clipTree.getProperty("id").toString();
                        auto* clip = subs_.clips->getClip(clipId);
                        if (clip == nullptr || clip->getType() != ClipType::Audio)
                            continue;

                        auto vocalStateTree = clipTree.getChildWithName(apex::vocaltune::ApexTuneProjectStateCore::idRoot);
                        if (!vocalStateTree.isValid())
                            continue;

                        apex::vocaltune::ApexTuneClipState vocalState;
                        if (apex::vocaltune::ApexTuneProjectStateCore::fromValueTree(vocalStateTree, vocalState))
                        {
                            integration->restoreClipState(clipId, std::move(vocalState));
                            integration->scheduleRerenderAfterLoad(clipId);
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            recordFailure("Clips", e.what());
        } catch (...) {
            recordFailure("Clips", "unknown exception");
        }
    }
    if (subs_.markers)
    {
        try {
            auto mv = state.getChildWithName("Markers");
            if (mv.isValid()) subs_.markers->restoreState(mv);
        } catch (const std::exception& e) {
            recordFailure("Markers", e.what());
        } catch (...) {
            recordFailure("Markers", "unknown exception");
        }
    }
    if (subs_.routing)
    {
        reportLoadProgress("Compiling routing", 0.39, {}, 5, 16);
        try {
            auto rv = state.getChildWithName("RoutingGraph");
            if (rv.isValid()) subs_.routing->restoreState(rv);
        } catch (const std::exception& e) {
            recordFailure("Routing", e.what());
        } catch (...) {
            recordFailure("Routing", "unknown exception");
        }
    }
    if (subs_.folderBus && subs_.routing && subs_.masterRoute && subs_.tracks)
    {
        reportLoadProgress("Restoring folder buses", 0.45, {}, 6, 16);
        try {
            auto fbv = state.getChildWithName("FolderBuses");
            if (fbv.isValid()) subs_.folderBus->restoreState(fbv, *subs_.routing, *subs_.masterRoute, *subs_.tracks);
        } catch (const std::exception& e) {
            recordFailure("FolderBus", e.what());
        } catch (...) {
            recordFailure("FolderBus", "unknown exception");
        }
    }
    if (subs_.appState)
    {
        try {
            auto av = state.getChildWithName("State");
            if (av.isValid()) subs_.appState->restoreState(av);
        } catch (const std::exception& e) {
            recordFailure("AppState", e.what());
        } catch (...) {
            recordFailure("AppState", "unknown exception");
        }
    }

    if (subs_.automation)
    {
        reportLoadProgress("Restoring automation", 0.53, {}, 8, 16);
        try {
            auto automation = state.getChildWithName("Automation");
            auto clipExists = [clips = subs_.clips](const juce::String& clipId) -> bool
            {
                return clips != nullptr && clips->getClip(clipId) != nullptr;
            };

            subs_.automation->restoreState(automation,
                                           subs_.clips ? std::function<bool(const juce::String&)>(clipExists)
                                                       : std::function<bool(const juce::String&)>{});
        } catch (const std::exception& e) {
            recordFailure("Automation", e.what());
        } catch (...) {
            recordFailure("Automation", "unknown exception");
        }
    }

    try {
        apex::automation::AutomationLaneStore::getInstance().restoreState(state.getChildWithName("APEXAutomation"));
    } catch (const std::exception& e) {
        recordFailure("APEXAutomation", e.what());
    } catch (...) {
        recordFailure("APEXAutomation", "unknown exception");
    }

    // ── Project migrations (v8 → v9) ─────────────────────────────────────
    // Sequential, idempotent and additive-only: projects older than the
    // current format are converted here, after every subsystem has been
    // restored. Nothing the user created is deleted.
    const int migrationSourceVersion = (int) state.getProperty("version", 0);
    lastUpgradeReport_.reset(migrationSourceVersion, kCurrentProjectVersion);
    // NOTE: the repair runs UNCONDITIONALLY, not only for version < 9. Every
    // step is idempotent and additive-only, and a project that was opened and
    // re-saved by a build after the version bump is stamped v9 while still
    // carrying the broken legacy data — a version gate would then never repair
    // it. Running the (idempotent) repair on every load guarantees old
    // projects heal regardless of how they were re-saved.
    if (subs_.automation != nullptr && subs_.clips != nullptr && subs_.tracks != nullptr)
    {
        int convertedLanes = 0;

        for (int t = 0; t < subs_.tracks->getNumTracks(); ++t)
        {
            auto* track = subs_.tracks->getTrack(t);
            if (track == nullptr)
                continue;

            const auto trackId = track->getID();

            // v8 stored the tape stop as a TRACK-level lane. The engine reads
            // clip.<id>.tape_stop, so such a project would otherwise lose its
            // tape stop entirely. Convert it to one clip-local lane per
            // intersected clip, clipping the curve to each clip's range.
            auto* legacy = subs_.automation->findLane(trackId,
                                                      AutomationLaneCore::trackTapeStopParameterId);
            if (legacy == nullptr || legacy->points.size() < 2)
                continue;

            juce::Array<Clip*> clipsOnTrack;
            subs_.clips->getClipsOnTrack(trackId, clipsOnTrack);

            int convertedForTrack = 0;
            for (auto* clip : clipsOnTrack)
            {
                if (clip == nullptr)
                    continue;

                const auto clipStart = (int64_t) clip->getStartPosition();
                const auto clipEnd   = clipStart + (int64_t) clip->getLength();
                const auto paramId   = AutomationLaneCore::makeClipTapeStopParameterId(clip->getID());

                // Idempotent: never overwrite an existing clip lane.
                if (subs_.automation->findLane(trackId, paramId) != nullptr)
                    continue;

                // Only clips the legacy curve actually intersects.
                if ((int64_t) legacy->points.back().timeSamples < clipStart
                    || (int64_t) legacy->points.front().timeSamples > clipEnd)
                    continue;

                auto& lane = subs_.automation->getOrCreateLane(trackId, paramId);
                lane.clear();
                lane.setDefaultValue(0.0f);
                for (const auto& p : legacy->points)
                    lane.addPoint(juce::jlimit(clipStart, clipEnd, (int64_t) p.timeSamples), p.value);
                lane.setEnabled(true);
                ++convertedForTrack;
            }

            if (convertedForTrack > 0)
            {
                // The legacy lane has no consumer any more: deactivate it (not
                // deleted) so the migrated project never plays it twice.
                subs_.automation->setLaneEnabled(trackId,
                                                 AutomationLaneCore::trackTapeStopParameterId, false);
                convertedLanes += convertedForTrack;
            }
        }

        if (convertedLanes > 0)
        {
            subs_.automation->publishSnapshot();
            juce::Logger::writeToLog("[MIGRATION] v8->v9: " + juce::String(convertedLanes)
                + " track.tape_stop lane(s) converted to clip-local tape stop lanes.");
            lastUpgradeReport_.add("Tape stop lanes migrated to clip-local",
                                   convertedLanes);
        }
    }


    if (subs_.appCore)
    {
        reportLoadProgress("Restoring plugins", 0.62, {}, 10, 16, false);
        try {
            auto cv = state.getChildWithName("ClickState");
            subs_.appCore->restoreClickStateTree(cv);
        } catch (const std::exception& e) {
            recordFailure("ClickState", e.what());
        } catch (...) {
            recordFailure("ClickState", "unknown exception");
        }
    }

    // Plugin chains - delegate to ApplicationCore
    if (subs_.appCore)
    {
        try {
            auto pv = state.getChildWithName("PluginChains");
            if (pv.isValid())
            {
                juce::String pluginRestoreError;
                if (!subs_.appCore->restorePluginChainsState(pv, pluginRestoreError))
                    recordFailure("PluginChains",
                                  pluginRestoreError.isNotEmpty()
                                      ? pluginRestoreError
                                      : "plugin chains could not be restored");
                else if (pluginRestoreError.isNotEmpty())
                {
                    if (lastLoadWarning_.isNotEmpty())
                        lastLoadWarning_ += "; ";
                    lastLoadWarning_ += "PluginChains: " + pluginRestoreError;
                    juce::Logger::writeToLog(
                        "[PROJECT] recoverable plugin restore warning: " + pluginRestoreError);
                }
            }
        } catch (const std::exception& e) {
            recordFailure("PluginChains", e.what());
        } catch (...) {
            recordFailure("PluginChains", "unknown exception");
        }

        // v8 -> v9 (part 2): sidechain connections saved with bus index 0 are
        // resolved to the plugin's first non-main auxiliary input bus and
        // PERSISTED, so migrated projects carry the clean format instead of
        // relying on the runtime fallback forever. Runs after the plugin
        // chains are loaded (it needs their bus layouts). Idempotent — and run
        // unconditionally, because a v9-stamped project may still carry the
        // legacy data (see the note above).
        {
            subs_.appCore->migrateLegacySidechainBusIndices(&lastUpgradeReport_);

            // Plugins can still be instantiating when the load returns, so the
            // bus layouts above may be empty and the auxiliary bus never gets
            // enabled — old projects then show the sidechain cable but EXT
            // receives nothing. Re-sync ONCE shortly after the load, when the
            // real layouts exist (a one-shot deferral, not polling). The
            // ApplicationCore outlives the ProjectManager, so the raw capture
            // is safe.
            if (auto* appCorePtr = subs_.appCore)
            {
                juce::Timer::callAfterDelay(1500, [appCorePtr]()
                {
                    appCorePtr->refreshSidechainBusConfig();
                    appCorePtr->dumpSidechainDiagnostics("after deferred re-sync");
                });
            }
        }
    }

    // Quick Track Builder project-scoped role-color families — restore the
    // exact mapping BEFORE any new Quick Builder operation can run.
    if (subs_.appCore)
    {
        try {
            auto qc = state.getChildWithName("QuickTrackColors");
            if (qc.isValid())
                subs_.appCore->restoreQuickTrackColorsState(qc);
        } catch (const std::exception& e) {
            recordFailure("QuickTrackColors", e.what());
        } catch (...) {
            recordFailure("QuickTrackColors", "unknown exception");
        }
    }

    if (subs_.transport)
    {
        reportLoadProgress("Restoring transport", 0.72, {}, 12, 16);
        try {
            auto tv = state.getChildWithName("Transport");
            if (tv.isValid())
            {
                subs_.transport->setTempo((double)tv.getProperty("tempo", 120.0));
                subs_.transport->setPosition(
                    (SamplePosition)(juce::int64)tv.getProperty("position", 0));
                subs_.transport->setLooping((bool)tv.getProperty("looping", false));
            }
        } catch (const std::exception& e) {
            recordFailure("Transport", e.what());
        } catch (...) {
            recordFailure("Transport", "unknown exception");
        }
    }

    // Restore ceiling mode and dB from saved project state.
    if (subs_.appCore)
    {
        try {
            auto mpd = state.getChildWithName("MasterPhaseD");
            if (mpd.isValid())
            {
                auto& ceiling = subs_.appCore->getMasterBus().getCeiling();
                const auto modeStr = mpd.getProperty("ceilingMode", "SoftClip").toString();
                ceiling.setMode(DAW::MasterCeilingCore::stringToMode(modeStr));
                ceiling.setCeilingDb((float)mpd.getProperty("ceilingDb", -0.1));
            }
        } catch (const std::exception& e) {
            recordFailure("MasterCeiling", e.what());
        } catch (...) {
            recordFailure("MasterCeiling", "unknown exception");
        }
    }

    // ── Project sample-rate identity (pre-beta integrity, 2026-07-26) ─────
    // Runs LAST, after every subsystem restored its sample-domain state.
    // Only proven mismatches reconcile; nothing is silently reinterpreted.
    try
    {
        reportLoadProgress("Reconciling project timing", 0.78, {}, 13, 16);
        const bool metadataPresent = state.hasProperty("projectSampleRate");
        const double storedRate = (double) state.getProperty("projectSampleRate", 0.0);
        const double deviceRate = (subs_.appCore != nullptr)
            ? subs_.appCore->getCurrentSampleRate() : 0.0;

        const auto plan = ProjectSampleRateReconcile::makePlan(storedRate, deviceRate, metadataPresent);
        lastLoadRateReport_.action          = (int) plan.action;
        lastLoadRateReport_.projectRate     = plan.projectRate;
        lastLoadRateReport_.deviceRate      = plan.deviceRate;
        lastLoadRateReport_.factor          = plan.factor;
        lastLoadRateReport_.metadataPresent = plan.metadataPresent;

        if (ProjectSampleRateReconcile::shouldReconcile(plan))
        {
            if (subs_.clips)
                ProjectSampleRateReconcile::reconcileClips(*subs_.clips, plan.factor, plan.deviceRate);

            if (subs_.automation)
                ProjectSampleRateReconcile::reconcileAutomation(*subs_.automation, plan.factor);

            if (subs_.transport)
                subs_.transport->setPosition((SamplePosition) ProjectSampleRateReconcile::scalePosition(
                    (int64_t) subs_.transport->getPosition(), plan.factor));

            if (subs_.markers)
                for (int i = 0; i < subs_.markers->getNumMarkers(); ++i)
                    if (auto* m = subs_.markers->getMarker(i))
                    {
                        m->position = (SamplePosition) ProjectSampleRateReconcile::scalePosition((int64_t) m->position, plan.factor);
                        m->length   = (SamplePosition) ProjectSampleRateReconcile::scalePosition((int64_t) m->length, plan.factor);
                    }

            // The independent PPQ AutomationLaneStore remains rate-independent;
            // AutomationManagerCore's sample-domain lanes were reconciled above.
            juce::Logger::writeToLog("[PROJECT] sample-rate mismatch reconciled seconds-preserving: project "
                + juce::String(plan.projectRate, 1) + " Hz -> device " + juce::String(plan.deviceRate, 1)
                + " Hz (factor " + juce::String(plan.factor, 6) + ")");
        }
        else if (plan.action == ProjectSampleRateReconcile::Action::legacyUnverified)
        {
            juce::Logger::writeToLog("[PROJECT] legacy project without sample-rate metadata: timeline "
                "interpreted at the current device rate and marked UNVERIFIED — no rate was invented "
                "(Brain §3). Verify timing, then re-save to pin the authoritative rate.");
        }
        else if (plan.action == ProjectSampleRateReconcile::Action::deviceRateUnknown)
        {
            juce::Logger::writeToLog("[PROJECT] project rate " + juce::String(plan.projectRate, 1)
                + " Hz but the device rate is unavailable: timeline NOT reinterpreted. "
                "Reopen after the audio device is running to reconcile.");
        }
    }
    catch (const std::exception& e)
    {
        recordFailure("SampleRateReconcile", e.what());
    }
    catch (...)
    {
        recordFailure("SampleRateReconcile", "unknown exception");
    }

    if (restoreSucceeded && subs_.appCore != nullptr)
    {
        try
        {
            // AudioFileManager is callback-visible. Replace its cache before
            // releasing the whole-project callback-admission gate.
            reportLoadProgress("Loading project audio", 0.82, {}, 0, 0, false);
            subs_.appCore->reloadAudioFiles([this](int completed, int total, const juce::String& fileName)
            {
                const double local = total > 0 ? (double) completed / (double) total : 1.0;
                reportLoadProgress("Loading project audio", 0.82 + local * 0.15,
                                   fileName, completed, total, total > 0);
            });
        }
        catch (const std::exception& e)
        {
            recordFailure("AudioFiles", e.what());
        }
        catch (...)
        {
            recordFailure("AudioFiles", "unknown exception");
        }
    }

    projectStateUsable_ = restoreSucceeded;
    if (!restoreSucceeded)
        juce::Logger::writeToLog("[PROJECT] restore failed after mutation began; realtime audio remains suppressed until recovery succeeds");

    return restoreSucceeded;
}

} // namespace DAW
