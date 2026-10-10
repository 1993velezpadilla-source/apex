#pragma once
#include <JuceHeader.h>
#include "../StateCore/ApplicationState.h"
#include "../TransportCore/TransportController.h"
#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../GamepadCore/GamepadManager.h"
#include "../GamepadCore/GamepadMapper.h"
#include "../InteractionModeCore/InteractionModeManager.h"
#include "../MarkerCore/MarkerManager.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../ProjectCore/ProjectManager.h"
#include "../ProjectCore/AutosaveManagerCore.h"
#include "../QuickTrackCore/QuickTrackColorSystem.h"
#include "../QuickTrackCore/QuickTrackTemplate.h"
#include "../ProjectCore/CrashRecoveryCore.h"
#include "../ProjectCore/RecoverySessionCore.h"
#include "../CommandCore/CommandManager.h"
#include "../AudioEngineCore/AudioEngine.h"
#include "../ProjectCore/ProjectUpgradeReport.h"
#include "../AudioEngineCore/AudioFileManager.h"
#include "../MasterCore/MasterBusEngine.h"
#include "../OutputRoutingCore/OutputRoutingEngine.h"
#include "../MonitorCore/ControlRoomEngine.h"
#include "../RecordingCore/RecordingEngine.h"
#include "../InputMonitorCore/LiveInputMonitorEngine.h"
#include "../ClickCore/ClickEngineCore.h"
#include "../ClickCore/ClickStateModel.h"
#include "../RecordingCore/RecordArmSafetyCore.h"
#include "../RecordingCore/OfflineRenderBarrierCore.h"
#include "../RecordingCore/RecordingCallbackPolicyCore.h"
#include "../PluginHostCore/PluginScannerCore.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../PluginHostCore/HostedPluginIsolationCore.h"
#include "../PluginHostCore/PluginPlayheadInfoCore.h"
#include "../PluginHostCore/ClipRegionPluginCore.h"
#include "../MixerScaleCore/FaderRangeCore.h"
#include "../Bubblegum/BubblegumV2System.h"
#include "../FolderBusCore/FolderBusCore.h"
#include "../FolderBusCore/FolderBusMuteCore.h"
#include "../FolderBusCore/FolderBusSoloCore.h"
#include "../FolderBusCore/FolderBusStateModel.h"
#include "../FolderBusCore/FolderBusRoutingValidator.h"
#include "../MidiCore/MidiInputCore.h"
#include "../MidiCore/PianoRollPlaybackCore.h"
#include "../MidiCore/VirtualMidiKeyboardCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../AutomationCore/PluginAutomationGestureCore.h"
#include "../AutomationCore/PluginAutomationRecorderCore.h"
#include "../AutomationCore/LastTouchedPluginParameterCore.h"
#include "../MeteringCore/TrackPeakMeterManagerCore.h"
#include "../DiagnosticsCore/CallbackAuditCore.h"

namespace apex { namespace vocaltune {
class IApexTuneEngineAdapter;
class ApexTuneIntegrationCore;
}}

namespace DAW {

// Main application nucleus - orchestrates all subsystems
class ApplicationCore : private TrackManager::Listener,
                        private Track::Listener,
                         private RoutingGraph::Listener,
                        private FaderRangeCore::Listener,
                        private TransportController::Listener
{
public:
    ApplicationCore();
    ~ApplicationCore();

    // Initialization
    void initialize();
    void shutdown();
    void queueVocalTuneBackgroundJob(std::function<void()> job);

    // Subsystem access (read-only getters)
    ApplicationState& getState() { return appState_; }
    const ApplicationState& getState() const { return appState_; }
    TransportController& getTransport() { return *transport_; }
    const TransportController& getTransport() const { return *transport_; }
    TrackManager& getTrackManager() { return *trackManager_; }
    const TrackManager& getTrackManager() const { return *trackManager_; }
    ClipManager& getClipManager() { return *clipManager_; }
    const ClipManager& getClipManager() const { return *clipManager_; }
    MarkerManager& getMarkerManager() { return *markerManager_; }
    const MarkerManager& getMarkerManager() const { return *markerManager_; }
    RoutingGraph& getRoutingGraph() { return *routingGraph_; }
    const RoutingGraph& getRoutingGraph() const { return *routingGraph_; }
    ProjectManager& getProjectManager() { return *projectManager_; }
    const ProjectManager& getProjectManager() const { return *projectManager_; }
    AudioEngine& getAudioEngine() { return audioEngine_; }
    const AudioEngine& getAudioEngine() const { return audioEngine_; }
    AudioFileManager& getAudioFileManager() { return audioFileManager_; }
    const AudioFileManager& getAudioFileManager() const { return audioFileManager_; }
    MasterBusEngine& getMasterBus() { return masterBus_; }
    const MasterBusEngine& getMasterBus() const { return masterBus_; }
    OutputRoutingEngine& getOutputRouting() { return outputRouting_; }
    const OutputRoutingEngine& getOutputRouting() const { return outputRouting_; }
    ControlRoomEngine& getControlRoom() { return controlRoom_; }
    const ControlRoomEngine& getControlRoom() const { return controlRoom_; }
    RecordingEngine& getRecordingEngine() { return recordingEngine_; }
    const RecordingEngine& getRecordingEngine() const { return recordingEngine_; }
    LiveInputMonitorEngine& getLiveInputMonitor() noexcept { return liveInputMonitor_; }
    const LiveInputMonitorEngine& getLiveInputMonitor() const noexcept { return liveInputMonitor_; }
    void setHardwareInputChannelCount(int inputChannels) noexcept
    {
        const int validInputChannels = juce::jmax(0, inputChannels);
        hardwareInputChannelCount_.store(validInputChannels, std::memory_order_release);
    }
    ClickEngineCore& getClickEngine() noexcept { return clickEngine_; }
    const ClickEngineCore& getClickEngine() const noexcept { return clickEngine_; }
    ClickStateModel& getClickState() noexcept { return clickState_; }
    const ClickStateModel& getClickState() const noexcept { return clickState_; }
    PluginScannerCore& getPluginScanner()  { return pluginScanner_; }
    const PluginScannerCore& getPluginScanner() const { return pluginScanner_; }
    PluginPlayheadInfoCore& getPluginPlayheadInfoCore() noexcept { return pluginPlayheadInfoCore_; }
    TrackPeakMeterManagerCore& getTrackPeakMeterManager() noexcept { return trackPeakMeterManager_; }
    const PluginPlayheadInfoCore& getPluginPlayheadInfoCore() const noexcept { return pluginPlayheadInfoCore_; }
    ClipRegionPluginCore& getClipRegionPluginCore() noexcept { return clipRegionPluginCore_; }
    const ClipRegionPluginCore& getClipRegionPluginCore() const noexcept { return clipRegionPluginCore_; }
    /** Close native clip-FX editor windows before any destructive project reset. */
    void setBeforeClipFxProjectReset(std::function<void()> callback)
    {
        beforeClipFxProjectReset_ = std::move(callback);
    }
    /** Message-thread callback: release plugin editor windows before clip FX is deleted. */
    void setBeforeClipFxClipRemoved(std::function<void(const ClipID&)> callback)
    {
        beforeClipFxClipRemoved_ = std::move(callback);
    }
    void notifyBeforeClipFxClipRemoved(const ClipID& clipId)
    {
        if (beforeClipFxClipRemoved_)
            beforeClipFxClipRemoved_(clipId);
    }
    BubblegumV2System& getBubblegumV2()      { return bubblegumV2_; }
    const BubblegumV2System& getBubblegumV2() const { return bubblegumV2_; }
    FolderBusCore& getFolderBus()              { return folderBus_; }
    const FolderBusCore& getFolderBus() const { return folderBus_; }
    DAW::FolderBusStateModel& getFolderBusStateModel() { return folderBusStateModel_; }
    const DAW::FolderBusStateModel& getFolderBusStateModel() const { return folderBusStateModel_; }
    FolderBusRoutingValidator& getFolderBusValidator() { return folderBusValidator_; }
    const FolderBusRoutingValidator& getFolderBusValidator() const { return folderBusValidator_; }
    FaderRangeCore& getFaderRangeCore() { return faderRangeCore_; }
    const FaderRangeCore& getFaderRangeCore() const { return faderRangeCore_; }
    PianoRollPlaybackCore& getMidiPlayback() noexcept { return midiPlayback_; }
    MidiInputCore& getMidiInput() noexcept { return midiInput_; }
    VirtualMidiKeyboardCore& getVirtualKeyboard() noexcept { return virtualKeyboard_; }
    AutomationManagerCore& getAutomationManager() noexcept { return automationManager_; }
    const AutomationManagerCore& getAutomationManager() const noexcept { return automationManager_; }

    AutosaveManagerCore& getAutosaveManager() noexcept { return *autosaveManager_; }
    CrashRecoveryCore&   getCrashRecovery()   noexcept { return *crashRecovery_; }
    RecoverySessionCore& getRecoverySession() noexcept { return *recoverySession_; }

    // ── Autosave / recovery public API ────────────────────────────────────────

    /** Mark project dirty for autosave (call from any subsystem event on message thread). */
    void markProjectDirty(const juce::String& reason)
    {
        if (autosaveManager_) autosaveManager_->markDirty(reason);
        if (projectManager_)  projectManager_->markDirty();
    }

    /** Call after a successful manual Save to clear userDirty and status. */
    void notifyManualSave()
    {
        if (autosaveManager_) autosaveManager_->markCleanManualSave();
    }

    /** Returns true if a recovered project is open (banner should be shown). */
    bool isRecoveredProject() const noexcept { return isRecoveredProject_; }
    void clearRecoveredFlag()                { isRecoveredProject_ = false; }

    /** Called by MainComponent after it is visible — checks and shows recovery prompt. */
    void checkAndShowRecoveryPromptIfNeeded(juce::Component* parent);

    /** Rebuild and publish the FolderBus mute/solo snapshot. Call after any mute/solo/topology change. */
    void rebuildFolderBusSnapshot()
    {
        DAW::FolderBusSnapshot snap;
        snap.effectivelyMutedTracks = folderBusMute_.buildEffectiveMuteSet(*trackManager_, folderBus_);
        snap.effectiveSoloSet       = folderBusSolo_.buildEffectiveSoloSet(*trackManager_, folderBus_);
        folderBusStateModel_.update(std::move(snap));
    }

    /** Publish per-track chain latencies so the audio thread can recompute
     *  PDC without touching pluginChains_ or the live RoutingGraph.
     *  MESSAGE THREAD only (allocates). Call after any chain mutation that
     *  may alter latency. */
    void publishPluginChainLatencies()
    {
        auto latencies = std::make_shared<AudioEngine::ChainLatencyMap>();
        for (const auto& [trackId, chain] : pluginChains_)
            if (chain)
                (*latencies)[trackId] = HostedPluginIsolationCore::effectiveHostedLatencySamples(
                    chain->totalLatencySamples());
        audioEngine_.publishChainLatencies(std::move(latencies));
    }

    /** C3: publish an immutable snapshot of the plugin-chain map so the
     *  audio thread never iterates/finds in the live std::map (RB-tree
     *  rotation during a concurrent find is UB). Shared ownership keeps
     *  chains alive while any audio-thread snapshot references them; plugin
     *  instance destruction is deferred by the C5 retire queue.
     *  MESSAGE THREAD only. Call after every pluginChains_ mutation. */
    void publishPluginChainsSnapshot()
    {
        auto snap = std::make_shared<AudioEngine::PluginChainSnapshotMap>();
        for (const auto& [trackId, chain] : pluginChains_)
            if (chain)
                (*snap)[trackId] = chain;
        audioEngine_.publishPluginChainsSnapshot(std::move(snap));
    }

    /** Get or create the plugin chain for a track ID. */
    PluginChainCore* getPluginChain(const TrackID& trackId)
    {
        auto it = pluginChains_.find(trackId);
        if (it != pluginChains_.end()) return it->second.get();
        auto chain = std::make_shared<PluginChainCore>();
        chain->setAutomationManager(&automationManager_);
        chain->setPlayheadInfoCore(&pluginPlayheadInfoCore_);
        chain->setAutomationContext(trackId, &pluginAutomationGestureCore_, &lastTouchedPluginParameterCore_);
        // Always prepare the chain so appendPlugin() / restoreState() can
        // load plugins even when the audio device has not started yet.
        // Use the live rate if available, otherwise fall back to a safe
        // default (44100 / 512).  The chain will be re-prepared with the
        // real device rate when prepareToPlay() fires later.
        const double sr = (currentSampleRate_ > 0.0) ? currentSampleRate_ : 44100.0;
        const int    bs = (currentBlockSize_  > 0)   ? currentBlockSize_  : 512;
        chain->prepare(sr, bs);

        // When the chain changes, inform the sidechain target list so it knows
        // whether this track has a plugin with a sidechain bus (bus count > 1).
        chain->onChainChanged = [this, trackId](int /*slotIndex*/)
        {
            auto* c = getPluginChain(trackId);
            bool capable = false;
            if (c)
            {
                for (int i = 0; i < c->getNumSlots(); ++i)
                {
                    auto* inst = c->getSlot(i);
                    if (inst && inst->getProcessor() &&
                        inst->getProcessor()->getBusCount(true) > 1)
                    {
                        capable = true;
                        break;
                    }
                }
            }
            bubblegumV2_.sidechainTargetList.markSidechainCapable(trackId, capable);
            if (pluginChainPublicationBatchDepth_ == 0)
            {
                publishPluginChainLatencies();
                audioEngine_.markMasterPdcDirty();
            }
        };

        auto* ptr = chain.get();
        pluginChains_[trackId] = std::move(chain);
        if (trackManager_)
            if (auto* track = trackManager_->getTrack(trackId))
                track->setPluginChain(ptr);
        if (pluginChainPublicationBatchDepth_ == 0)
        {
            publishPluginChainLatencies();
            publishPluginChainsSnapshot();
        }
        return ptr;
    }

    /** Clear all plugin chains (for new project). */
    void clearAllPluginChains()
    {
        // Null out the master bus pointer BEFORE destroying chains so the
        // audio thread never dereferences a freed PluginChainCore.
        masterBus_.setPluginChain(nullptr);
        pluginChains_.clear();

        const bool recreatedMaster = trackManager_ && trackManager_->hasMasterTrack();
        if (recreatedMaster)
            masterBus_.setPluginChain(getPluginChain(trackManager_->getMasterTrack()->getID()));
        else if (pluginChainPublicationBatchDepth_ == 0)
        {
            publishPluginChainLatencies();
            publishPluginChainsSnapshot();
        }
    }

    double getCurrentSampleRate() const { return currentSampleRate_; }
    int getCurrentBlockSize() const { return currentBlockSize_; }

    // Gamepad support (optional feature)
    GamepadManager& getGamepadManager() { return *gamepadManager_; }
    const GamepadManager& getGamepadManager() const { return *gamepadManager_; }
    GamepadMapper& getGamepadMapper() { return *gamepadMapper_; }
    const GamepadMapper& getGamepadMapper() const { return *gamepadMapper_; }

    void setGamepadEnabled(bool enabled);
    bool isGamepadEnabled() const;

    // Interaction mode (Touch/Hybrid/Desktop)
    InteractionModeManager& getInteractionManager() { return *interactionManager_; }
    const InteractionModeManager& getInteractionManager() const { return *interactionManager_; }

    // Audio engine integration
    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    bool beginRealtimeDeviceCallback() noexcept;
    void endRealtimeDeviceCallback() noexcept;
    bool waitForRealtimeDeviceCallbacksToDrain(uint32_t timeoutMs) const noexcept;
    bool beginProjectStateRestore(uint32_t timeoutMs = 2000) noexcept;
    void endProjectStateRestore(bool restoreSucceeded) noexcept;

    /** C6-editor-lifetime: close every plugin editor BEFORE a project-state
        restore begins. Editor teardown is proven safe while the audio engine
        is live (the exact conditions of a manual close), and running it here
        means the later chain teardown inside the restore finds no native
        editor windows to destroy — so no plugin window proc ever runs inside
        the topology-restore storm. Message thread only; must be called
        BEFORE beginProjectStateRestore(). */
    void closeAllPluginEditors();

    /** C6-sidechain-transaction: deactivate any sidechain connection whose
        plugin aux bus was not actually committed/enabled (negotiation
        rejected, drain timeout, or missing target). Republishes the snapshot
        so the UI can never show an ACTIVE route with a disabled plugin bus. */
    void rollbackUncommittedSidechainRoutes(
        const std::function<int(PluginChainCore&, const RoutingConnection&)>& resolveTargetSlot);
    juce::String getLastSidechainError() const { return lastSidechainError_; }

    bool isProjectStateRestoreSuspended() const noexcept
    {
        return projectStateRestoreSuspensionActive_.load(std::memory_order_acquire);
    }
    bool getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill,
                           const juce::AudioBuffer<float>* hardwareInputBuffer = nullptr,
                           int hardwareInputNumSamples = 0,
                           int validInputChannels = 0);
    bool beginOfflineRender(int blockSize);
    juce::String getLastOfflineRenderError() const { return lastOfflineRenderError_; }
    bool beginOfflineStemPass(std::shared_ptr<const AudioEngine::OfflineStemRenderMask> mask,
                              int blockSize,
                              int64_t timelineSample,
                              bool applyMasterProcessing);
    bool renderOfflineBlock(juce::AudioBuffer<float>& output, int numSamples, int64_t timelineSample,
                            bool applyMasterProcessing = true);
    void endOfflineRender();

    /** Reload audio buffers for all AudioClips from their source files. */
    void reloadAudioFiles(std::function<void(int, int, const juce::String&)> progress = {});

    // ── Plugin chain serialization (called by ProjectManager during save/load) ──────

    /** Result-bearing control-plane snapshot.  The returned tree is invalid
        when any worker-owned sandbox state cannot be captured. */
    bool capturePluginChainsState(juce::ValueTree& chains,
                                  juce::String& error) const;
    juce::ValueTree getPluginChainsState() const;
    /** Restore all chain records. A false result means the restore
        transaction could not be entered/completed (for example, the
        realtime drain failed). Slot-level mode/construction/state failures
        are recoverable diagnostics in `error`; unrelated chains still
        restore. */
    bool restorePluginChainsState(const juce::ValueTree& chains,
                                  juce::String& error);
    bool restorePluginChainsState(const juce::ValueTree& chains);

    juce::ValueTree getClickStateTree() const;
    void restoreClickStateTree(const juce::ValueTree& clickState);

    // ── Quick Track Builder project-scoped state (serialized by ProjectManager) ──

    /** Project-scoped role-color families (fresh per project, persisted). */
    QuickTrackColorSystem& getQuickTrackColors() noexcept { return *quickTrackColors_; }
    const QuickTrackColorSystem& getQuickTrackColors() const noexcept { return *quickTrackColors_; }

    /** Application-global Quick Track Builder templates (survive New
     *  Project / restart; stored in %APPDATA%/APEX/QuickTrackTemplates). */
    QuickTrackTemplateStore& getQuickTrackTemplates() noexcept { return quickTrackTemplates_; }
    const QuickTrackTemplateStore& getQuickTrackTemplates() const noexcept { return quickTrackTemplates_; }

    juce::ValueTree getQuickTrackColorsState() const;
    bool restoreQuickTrackColorsState(const juce::ValueTree& colors);

    /** New Project: fresh color map + fresh palette shuffle. */
    void resetQuickTrackColorsForNewProject();

    apex::vocaltune::ApexTuneIntegrationCore* getVocalTuneIntegrationPtr() noexcept { return vocalTuneIntegration_.get(); }
    const apex::vocaltune::ApexTuneIntegrationCore* getVocalTuneIntegrationPtr() const noexcept { return vocalTuneIntegration_.get(); }
    apex::vocaltune::ApexTuneIntegrationCore& getVocalTuneIntegration() { return *vocalTuneIntegration_; }
    const apex::vocaltune::ApexTuneIntegrationCore& getVocalTuneIntegration() const { return *vocalTuneIntegration_; }

    std::vector<RecordArmSafetyCore::OverwriteWarning> checkRecordArmSafety() const
    {
        if (!trackManager_ || !clipManager_ || !transport_) return {};
        return RecordArmSafetyCore::check(trackManager_.get(), clipManager_.get(), transport_->getPosition());
    }

private:
    void beginPluginChainPublicationBatch() noexcept
    {
        ++pluginChainPublicationBatchDepth_;
    }

    void endPluginChainPublicationBatch()
    {
        jassert(pluginChainPublicationBatchDepth_ > 0);
        if (pluginChainPublicationBatchDepth_ <= 0)
            return;
        if (--pluginChainPublicationBatchDepth_ == 0)
        {
            publishPluginChainLatencies();
            publishPluginChainsSnapshot();
            audioEngine_.markMasterPdcDirty();
        }
    }

    void syncPluginChainSidechainBusConfig();
    void releaseAllPluginChainsForShutdown();

    void prepareRenderGraph(double sampleRate, int samplesPerBlock);
    void preparePluginChainsForOffline(double sampleRate, int samplesPerBlock);
    void restorePluginChainsAfterOffline(double sampleRate, int samplesPerBlock);
    void logOfflinePluginChains(double sampleRate, int samplesPerBlock) const;

    // Core state
    ApplicationState appState_;

    // Subsystems (initialized in order)
    std::unique_ptr<TransportController> transport_;
    std::unique_ptr<TrackManager> trackManager_;
    std::unique_ptr<ClipManager> clipManager_;
    std::unique_ptr<MarkerManager> markerManager_;
    std::unique_ptr<RoutingGraph> routingGraph_;
    std::unique_ptr<ProjectManager> projectManager_;

    // Quick Track Builder state
    std::unique_ptr<QuickTrackColorSystem> quickTrackColors_;
    QuickTrackTemplateStore quickTrackTemplates_;

    // Gamepad support (optional)
    std::unique_ptr<GamepadManager> gamepadManager_;
    std::unique_ptr<GamepadMapper> gamepadMapper_;

    // Interaction mode manager
    std::unique_ptr<InteractionModeManager> interactionManager_;

    // Audio engine + master + output + monitor
    AudioEngine audioEngine_;
    AudioFileManager audioFileManager_;
    MasterBusEngine masterBus_;
    OutputRoutingEngine outputRouting_;
    ControlRoomEngine controlRoom_;
    RecordingEngine recordingEngine_;
    LiveInputMonitorEngine liveInputMonitor_;
    ClickStateModel clickState_;
    ClickEngineCore clickEngine_;
    juce::AudioBuffer<float> preservedHardwareInput_;
    PluginScannerCore pluginScanner_;
    PluginPlayheadInfoCore pluginPlayheadInfoCore_;
    ClipRegionPluginCore clipRegionPluginCore_;
    std::function<void()> beforeClipFxProjectReset_;
    std::function<void(const ClipID&)> beforeClipFxClipRemoved_;
    std::map<TrackID, std::shared_ptr<PluginChainCore>> pluginChains_;
    int pluginChainPublicationBatchDepth_ = 0;
    FaderRangeCore faderRangeCore_;
    TrackPeakMeterManagerCore trackPeakMeterManager_;
    double currentSampleRate_{0.0};
    int currentBlockSize_{0};
    std::atomic<int> hardwareInputChannelCount_ { 0 };
    std::atomic<int> activeDeviceCallbacks_ { 0 };
    std::atomic<bool> offlineRenderReady_ { false };
    juce::String lastOfflineRenderError_;
    juce::String lastSidechainError_;
    double offlineRestoreSampleRate_{0.0};
    int offlineRestoreBlockSize_{0};
    int offlineMasterDebugBlocksRemaining_{0};
    BubblegumV2System bubblegumV2_;
    std::atomic<int64_t> diagInputCallbackCount_ { 0 };
    std::atomic<int64_t> diagInputSamplesExpected_ { 0 };
    std::atomic<int64_t> diagInputSamplesPreserved_ { 0 };
    std::atomic<int> diagInputChannelsSeen_ { 0 };
    std::atomic<int64_t> diagInputShortBlockCount_ { 0 };
    std::atomic<int64_t> diagInputZeroBlockCount_ { 0 };
    std::atomic<uint32_t> diagLastInputLogMs_ { 0 };

    // True while the recorder is being fed silence because live hardware
    // input is missing mid-take (tape-machine guarantee). Used to log the
    // lost/restored transitions exactly once each.
    std::atomic<bool> recordingSilenceFallbackAnnounced_ { false };
    // RT-safe counters for recording transitions — drained on message thread.
    std::atomic<int64_t> recGuardSilenceCount_ { 0 };
    std::atomic<int64_t> recGuardRestoredCount_ { 0 };

public:
    /** Called by the host after ANY topology mutation. Sidechain connections
     *  need the destination plugin's auxiliary bus enabled; without this
     *  re-sync the chain skips the sidechain silently (cable visible, plugin
     *  set up, no signal). */
    void refreshSidechainBusConfig() { syncPluginChainSidechainBusConfig(); }

    /** v8 -> v9 project migration: sidechain connections saved with bus index
     *  0 are resolved to the destination plugin's first NON-MAIN auxiliary
     *  input bus and persisted, so migrated projects carry the clean format
     *  instead of relying on the runtime fallback forever. Returns how many
     *  connections were resolved. */
    int migrateLegacySidechainBusIndices(ProjectUpgradeReport* report = nullptr);

    /** Writes the COMPLETE sidechain state (connections, flags, bus index,
     *  destination chains and their real input buses) to
     *  %APPDATA%/DAW_Core/sidechain_diagnostic.log so a broken project can be
     *  diagnosed from data instead of guesses. */
    void dumpSidechainDiagnostics(const juce::String& context);

    /** Drain and log recording-transition counters. Call from message thread (e.g. timer). */
    void drainRecGuardCounters()
    {
        const auto silence = recGuardSilenceCount_.exchange(0, std::memory_order_relaxed);
        const auto restored = recGuardRestoredCount_.exchange(0, std::memory_order_relaxed);
        if (silence > 0)
            juce::Logger::writeToLog("[REC-GUARD] No live hardware input while recording x"
                + juce::String(silence) + " — writing silence to keep the take alive. "
                "Check input device / Windows microphone privacy settings.");
        if (restored > 0)
            juce::Logger::writeToLog("[REC-GUARD] Live hardware input restored x"
                + juce::String(restored) + " — recording real audio again.");
    }

    /** Phase D production maintenance entry. MESSAGE THREAD only; invoked from
        the established control-plane tick (MainComponent::inputWatchdogTick,
        ~2 s cadence). Runs sandbox worker death/hang detection and the
        bounded automatic restart ladder for every sandboxed slot in every
        plugin chain. No realtime work is performed here or by anything this
        calls on the audio thread. */
    void serviceSandboxWorkers()
    {
        // Chain-clear/destruction can hand bypass retirement to the
        // process-level owner after the chain has left this map.  Drain that
        // owner before servicing the still-live chains as well as their local
        // retirement lists.
        PluginChainCore::drainAllRetiredBypassCores();
        for (const auto& [trackId, chain] : pluginChains_)
        {
            juce::ignoreUnused(trackId);
            if (chain)
            {
                chain->drainRetiredBypassCores();
                chain->pollSandboxHealth();
            }
        }
    }

    // Set to true while the export thread owns the AudioEngine.
    // getNextAudioBlock() checks this and outputs silence so the device
    // callback never races with renderOfflineBlock() on shared buffers.
    std::atomic<bool> isOfflineRendering_ { false };

    // C1: true while the message thread re-prepares live plugin instances
    // (sidechain bus reconfiguration) after draining callbacks.
    // getNextAudioBlock() suppresses engine/plugin processing for the
    // bounded window; recording capture is unaffected.
    std::atomic<bool> pluginReprepareSuspensionActive_ { false };

    // Published before destructive whole-project replacement. Callback
    // admission rejects new work, then the control thread drains callbacks
    // already in flight before deleting tracks, clips, routes, or plugins.
    // A partial failed restore deliberately leaves this latched until a
    // subsequent recovery load or New Project succeeds.
    std::atomic<bool> projectStateRestoreSuspensionActive_ { false };

    // ── Callback stage telemetry (low-buffer forensics) ──────────────────
    // When armed (APEX_CALLBACK_AUDIT=1), getNextAudioBlock() measures
    // per-stage durations with hi-res ticks and captures spike context.
    // Written on the audio thread, read by MainComponent on the SAME thread
    // immediately after getNextAudioBlock() returns — no cross-thread access.
    struct CallbackStageTelemetry
    {
        std::array<int64_t, (size_t) kCallbackAuditStageCount> stageTicks {};
        uint8_t  flags = 0;              // bit0 playing, bit1 recording, bit2 fresh hw input
        uint32_t trackCount = 0;
        uint64_t graphVersion = 0;
    };

    void setCallbackStageTimingArmed (bool armed) noexcept
    {
        callbackStageTimingArmed_.store (armed, std::memory_order_release);
    }

    const CallbackStageTelemetry& getCallbackStageTelemetry() const noexcept
    {
        return callbackStageTelemetry_;
    }

private:
    std::atomic<bool> callbackStageTimingArmed_ { false };
    CallbackStageTelemetry callbackStageTelemetry_;

public:

    // FolderBus nucleos
    FolderBusCore            folderBus_;
    FolderBusMuteCore        folderBusMute_;
    FolderBusSoloCore        folderBusSolo_;
    DAW::FolderBusStateModel      folderBusStateModel_;
    FolderBusRoutingValidator folderBusValidator_;
    MidiInputCore midiInput_;
    PianoRollPlaybackCore midiPlayback_;
    VirtualMidiKeyboardCore virtualKeyboard_;
    AutomationManagerCore automationManager_;
    LastTouchedPluginParameterCore lastTouchedPluginParameterCore_;
    PluginAutomationGestureCore pluginAutomationGestureCore_;
    PluginAutomationRecorderCore pluginAutomationRecorderCore_;

    // TrackManager::Listener
    void trackAdded(Track* track) override;
    void trackRemoved(const TrackID& trackID) override;

    // RoutingGraph::Listener
    void connectionAdded(RoutingConnection* conn) override;
    void connectionRemoved(const RouteID& connId) override;
    void graphChanged() override;

    // Track::Listener
    void trackPropertyChanged(Track* track) override;

    // FaderRangeCore::Listener
    void faderRangeChanged() override;

    // TransportController::Listener — used to detect recording stop for autosave trigger
    void transportStateChanged() override;
    void tempoChanged(double newTempo) override;

    // ── Autosave / crash recovery (owned, initialized in initialize()) ────────
    std::unique_ptr<RecoverySessionCore>  recoverySession_;
    std::unique_ptr<CrashRecoveryCore>    crashRecovery_;
    std::unique_ptr<AutosaveManagerCore>  autosaveManager_;
    juce::ThreadPool                      vocalTuneThreadPool_ { 1 };
    std::unique_ptr<apex::vocaltune::IApexTuneEngineAdapter> vocalTuneEngineAdapter_;
    std::unique_ptr<apex::vocaltune::ApexTuneIntegrationCore> vocalTuneIntegration_;

    bool isRecoveredProject_ = false;
    bool wasRecordingLastTick_ = false;   ///< tracks previous recording state for stop-detection
    bool shutdownComplete_ = false;

    // Recovery dialog window — observed via SafePointer so JUCE's modal
    // manager can auto-delete it on dismiss without causing a double-free.
    juce::Component::SafePointer<juce::DialogWindow> recoveryDialogWindow_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(ApplicationCore)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ApplicationCore)
};

} // namespace DAW
