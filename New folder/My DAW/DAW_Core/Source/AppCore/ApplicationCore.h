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
#include "../ProjectCore/CrashRecoveryCore.h"
#include "../ProjectCore/RecoverySessionCore.h"
#include "../CommandCore/CommandManager.h"
#include "../AudioEngineCore/AudioEngine.h"
#include "../AudioEngineCore/AudioFileManager.h"
#include "../MasterCore/MasterBusEngine.h"
#include "../OutputRoutingCore/OutputRoutingEngine.h"
#include "../MonitorCore/ControlRoomEngine.h"
#include "../RecordingCore/RecordingEngine.h"
#include "../InputMonitorCore/LiveInputMonitorEngine.h"
#include "../ClickCore/ClickEngineCore.h"
#include "../ClickCore/ClickStateModel.h"
#include "../RecordingCore/RecordArmSafetyCore.h"
#include "../PluginHostCore/PluginScannerCore.h"
#include "../PluginHostCore/PluginChainCore.h"
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
        hardwareInputChannelCount_ = juce::jmax(0, inputChannels);
        recordingEngine_.setInputChannelCount(hardwareInputChannelCount_);
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

    /** Get or create the plugin chain for a track ID. */
    PluginChainCore* getPluginChain(const TrackID& trackId)
    {
        auto it = pluginChains_.find(trackId);
        if (it != pluginChains_.end()) return it->second.get();
        auto chain = std::make_unique<PluginChainCore>();
        chain->setPlayheadInfoCore(&pluginPlayheadInfoCore_);
        chain->setAutomationContext(trackId, &pluginAutomationGestureCore_, &lastTouchedPluginParameterCore_);
        if (currentSampleRate_ > 0.0 && currentBlockSize_ > 0)
            chain->prepare(currentSampleRate_, currentBlockSize_);

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
            audioEngine_.markMasterPdcDirty();
        };

        auto* ptr = chain.get();
        pluginChains_[trackId] = std::move(chain);
        if (trackManager_)
            if (auto* track = trackManager_->getTrack(trackId))
                track->setPluginChain(ptr);
        return ptr;
    }

    /** Clear all plugin chains (for new project). */
    void clearAllPluginChains()
    {
        // Null out the master bus pointer BEFORE destroying chains so the
        // audio thread never dereferences a freed PluginChainCore.
        masterBus_.setPluginChain(nullptr);
        pluginChains_.clear();

        if (trackManager_ && trackManager_->hasMasterTrack())
            masterBus_.setPluginChain(getPluginChain(trackManager_->getMasterTrack()->getID()));
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
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill,
                           const juce::AudioBuffer<float>* hardwareInputBuffer = nullptr,
                           int hardwareInputNumSamples = 0);
    void beginOfflineRender(int blockSize);
    bool renderOfflineBlock(juce::AudioBuffer<float>& output, int numSamples, int64_t timelineSample);
    void endOfflineRender();

    /** Reload audio buffers for all AudioClips from their source files. */
    void reloadAudioFiles();

    // ── Plugin chain serialization (called by ProjectManager during save/load) ──────

    juce::ValueTree getPluginChainsState() const;
    void restorePluginChainsState(const juce::ValueTree& chains);

    juce::ValueTree getClickStateTree() const;
    void restoreClickStateTree(const juce::ValueTree& clickState);

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
    std::map<TrackID, std::unique_ptr<PluginChainCore>> pluginChains_;
    FaderRangeCore faderRangeCore_;
    TrackPeakMeterManagerCore trackPeakMeterManager_;
    double currentSampleRate_{0.0};
    int currentBlockSize_{0};
    int hardwareInputChannelCount_{0};
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

    // Set to true while the export thread owns the AudioEngine.
    // getNextAudioBlock() checks this and outputs silence so the device
    // callback never races with renderOfflineBlock() on shared buffers.
    std::atomic<bool> isOfflineRendering_ { false };

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
