#pragma once

#include <JuceHeader.h>
#include <unordered_map>
#include <unordered_set>
#include "AppCore/ApplicationCore.h"
#include "UICore/DAWMenuBar.h"
#include "UICore/TransportBar.h"
#include "UICore/SettingsPanel.h"
#include "UICore/TrackList.h"
#include "UICore/QuickSendPopup.h"
#include "QuickTrackCore/QuickTrackBuilderCore.h"
#include "QuickTrackCore/QuickTrackBuilderPopup.h"
#include "QuickTrackCore/QuickWorkflowPopup.h"
#include "UICore/ArrangementView.h"
#include "UICore/MixerPanel.h"
#include "UICore/MixerKeyboardRoutingCore.h"
#include "UICore/MonitorSectionUI.h"
#include "UICore/ControlRoomSettingsUI.h"
#include "DeviceCore/AudioDeviceSettingsUI.h"
#include "DeviceCore/DeviceSessionCore.h"
#include "DeviceCore/PendingAudioPreparationGateCore.h"
#include "UICore/AudioDevicePanelUI.h"
#include "UICore/TrackListWindow.h"
#include "UICore/MixerWindow.h"
#include "UICore/PianoRollWindow.h"
#include "UICore/ClipPropertiesWindow.h"
#include "UICore/TimelineWindow.h"
#include "BubbleCore/BubbleTaskbar.h"
#include "BubbleCore/MixerBubble.h"
#include "BubbleCore/TimelineBubble.h"
#include "BubbleCore/MasterBubble.h"
#include "BubbleCore/MarkerBubble.h"
#include "BubbleCore/BubbleOrchestrator.h"
#include "BubbleCore/BubbleMergeOverlay.h"
#include "PluginHostCore/MixerPluginSidePanel.h"
#include "PluginHostCore/PluginBrowserPanel.h"
#include "PluginHostCore/PluginScanStartupDialog.h"
#include "FloatingWindowCore/FloatingPanelChrome.h"
#include "UICore/SlimeSidePanel.h"
#include "CursorCore/VirtualCursor.h"
#include "ActionCore/ActionManager.h"
#include "KeyBindingCore/KeyBindingManager.h"
#include "MarkerCore/MarkerManager.h"
#include "CommandCore/UndoHistoryPanel.h"
#include "UICore/ShortcutHelpFloatingWindow.h"
#include "UICore/ForensicAuditWindow.h"
#include "UICore/RecordOverwriteConfirmDialog.h"
#include "FloatingBubblegum/BubblegumOrbComponent.h"
#include "UICore/BubblegumV2PanelUI.h"
#include "UICore/BubblegumCableOverlayComponent.h"
#include "UICore/BubblegumOffscreenEndpointComponent.h"
#include "BubblegumTaskbarCore/BubblegumTaskbarComponent.h"
#include "InputTrimPanelCore/InputTrimPanelCore.h"
#include "UICore/SelectedTrackPeakBubbleComponent.h"
#include "RenderCore/ExportRenderCore.h"
#include "RenderCore/AudioResourceReleaseStateCore.h"
#include "SelectionCore/MultiSelectionCore.h"
#include "SelectionCore/SelectionBulkActionCore.h"
#include "ClickCore/ClickStateModel.h"
#include "UICore/QuitSafetyDialog.h"
#include "UICore/ProjectLoadingOverlay.h"
#include "UICore/StartupPanel.h"
#include "StepSequencerCore/StepSequencerWindow.h"
#include "StepSequencerCore/StepSequencerPlaybackCore.h"
#include "DrumSamplerCore/DrumSamplerEngine.h"
#include "PatternManagerCore/PatternManagerCore.h"
#include "DiagnosticsCore/CallbackAuditCore.h"
#include "Bubblegum/QuickSendModeCore.h"

namespace DAW { class PianoRollWindow; }

// ── Mixer viewport ───────────────────────────────────────────────────────────
// Refuses JUCE's built-in arrow-key scrolling so mixer arrow navigation stays
// owned by MainComponent (plain arrows = track selection; Ctrl+arrows belong
// to Arrange/Timeline zoom).
class MixerViewport : public juce::Viewport
{
public:
    bool keyPressed(const juce::KeyPress& key) override
    {
        // Never consume arrow keys for scrolling. Return false so the key
        // event walks up the parent chain to MainComponent's KeyListener.
        return false;
    }

};

//==============================================================================
// MainComponent – full DAW layout
//   ┌─────────────────────────────────────────────────┐
//   │  MenuBar                                        │ 28px
//   ├─────────────────────────────────────────────────┤
//   │  TransportBar                      ⚙ Settings  │ 52px
//   ├────────────┬────────────────────────────────────┤
//   │            │                                    │
//   │ TrackList  │   ArrangementView                  │
//   │  (headers) │                                    │
//   ├────────────┴────────────────────────────────────┤
//   │  MixerPanel  (horizontal scroll)                │ 270px
//   └─────────────────────────────────────────────────┘
class MainComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private DAW::CommandManager::Listener,
                      private DAW::RoutingGraph::Listener,
                      private juce::ScrollBar::Listener,
                      private juce::ComponentListener,
                      private juce::KeyListener,
                      private DAW::ProjectManager::Listener,
                      private DAW::TrackManager::Listener,
                      private DAW::TransportController::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate);
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill);
    void releaseResources();

    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    /** KeyListener: Mixer selection / Arrange zoom policy runs before focused
        scrollbars consume arrow keys. */
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    bool keyStateChanged(bool, juce::Component*) override { return false; }
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    bool requestQuitFromWindowClose();
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void componentMovedOrResized(juce::Component&, bool, bool) override;
    void childrenChanged() override;
    void updateSlimeAttachedPosition();
    void updateWindowTitle();

    // ProjectManager::Listener
    void projectSaved() override;
    void projectLoaded() override;

    // TransportController::Listener — grows the timeline scroll extent when
    // the playhead moves past the current content end (scrubbing/looping).
    void transportStateChanged() override;
    void positionChanged(DAW::SamplePosition newPosition) override;

    /** Wire a scan-active callback into the plugin scanner.
     *  Called from Main.cpp after both MainComponent and the suppressor exist.
     *  The callback receives true when a scan starts, false when it ends. */
    void setScanActiveCallback(std::function<void(bool)> cb)
    {
        appCore_.getPluginScanner().onScanActiveChanged = std::move(cb);
    }

    /** Expose ApplicationCore for benchmark/diagnostics use ONLY.
     *  Used by TimelineBenchmarkController to load projects and query engine state.
     *  Do NOT use this accessor to bypass normal production authority outside
     *  diagnostics/benchmark code. */
    DAW::ApplicationCore& getAppCore() { return appCore_; }
    const DAW::ApplicationCore& getAppCore() const { return appCore_; }

    /** The APEX header (custom title bar). Used by the top-level window shell
     *  for caption hit-testing and window-control routing. */
    DAW::DAWMenuBar* getMenuBar() const noexcept { return menuBar_.get(); }

private:
    DAW::ApplicationCore appCore_;

    // Panels
    std::unique_ptr<DAW::DAWMenuBar>       menuBar_;
    std::unique_ptr<DAW::TransportBar>     transportBar_;
    std::unique_ptr<DAW::SettingsPanel>    settingsPanel_;
    std::unique_ptr<DAW::TrackList>        trackList_;
    std::unique_ptr<DAW::ArrangementView>  arrangement_;
    std::unique_ptr<DAW::MixerPanel>       mixerPanel_;
    std::unique_ptr<DAW::MonitorSectionUI>  monitorSection_;
    std::unique_ptr<DAW::AudioDeviceSettingsUI> audioDevicePanel_;
    std::unique_ptr<DAW::DeviceSessionCore> stagedDeviceSession_;
    std::unique_ptr<DAW::DevicePanelModelCore> stagedDevicePanelModel_;
    std::unique_ptr<DAW::AudioDevicePanelUI> stagedAudioDevicePanel_;
    std::unique_ptr<DAW::ControlRoomSettingsUI> controlRoomPanel_;
    std::unique_ptr<juce::Viewport>       timelineViewport_;
    MixerViewport                         mixerViewport_;
    DAW::BubblegumCableOverlayComponent     cableOverlay_;
    DAW::BubblegumOffscreenEndpointComponent offscreenEndpoint_;
    std::unique_ptr<DAW::TrackListWindow>  trackListWindow_;
    std::unique_ptr<DAW::QuickSendPopup>   quickSendPopup_;  // (removed from UI — see MainComponent.cpp)
    // APEX Quick Workflow — one shared Track / Route / Send presentation shell
    // over the canonical TrackManager, RoutingGraph, and color state.
    std::unique_ptr<DAW::QuickWorkflowCore> quickWorkflow_;
    void openQuickWorkflow(DAW::QuickWorkflowTab tab = DAW::QuickWorkflowTab::QuickTrack,
                           const juce::Rectangle<int>& anchorOverride = {},
                           const DAW::TrackID& sourceId = {});
    std::unique_ptr<DAW::MixerWindow>      mixerWindow_;
    std::unique_ptr<DAW::PianoRollWindow>  pianoRollWindow_;
    std::unique_ptr<DAW::ClipPropertiesWindow> clipPropertiesWindow_;
    std::unique_ptr<DAW::TimelineWindow>   timelineWindow_;

    std::unique_ptr<DAW::BubbleTaskbar>    bubbleTaskbar_;
    std::unique_ptr<DAW::BubblegumTaskbarComponent> bubblegumTaskbar_;
    std::unique_ptr<DAW::InputTrimPanelManager> inputTrimPanelManager_;
    juce::AudioDeviceManager               deviceManager;
    bool                                   audioCallbackRegistered_ = false;
    std::unique_ptr<DAW::MixerBubble>      mixerBubble_;
    std::unique_ptr<DAW::TimelineBubble>   timelineBubble_;
    std::unique_ptr<DAW::MasterBubble>     masterBubble_;
    std::unique_ptr<DAW::MarkerBubble>     markerBubble_;
    std::unique_ptr<DAW::BubbleMergeOverlay> mergeOverlay_;
    std::unique_ptr<DAW::BubblegumOrbComponent> bubblegumOrb_;
    std::unique_ptr<DAW::BubblegumV2PanelUI>     bubblegumPanel_;
    std::unique_ptr<DAW::MixerPluginSidePanel> pluginSidePanel_;
    std::unique_ptr<DAW::PluginBrowserPanel>   pluginBrowser_;
    std::unique_ptr<DAW::SlimeSidePanel>  slimeSidePanel_;
    std::unique_ptr<DAW::FloatingPanelChrome>  browserChrome_;
    std::unique_ptr<DAW::PluginScanStartupDialog> scanDialog_;
    std::unique_ptr<DAW::ProjectLoadingOverlay> loadingOverlay_;
    std::unique_ptr<DAW::StartupPanel> startupPanel_;
    bool startupPanelMayBeShown_ = true;
    std::unique_ptr<DAW::VirtualCursor>    virtualCursor_;
    std::unique_ptr<DAW::UndoHistoryPanel> historyPanel_;
    std::unique_ptr<DAW::ShortcutHelpFloatingWindow> shortcutHelpWindow_;
    std::unique_ptr<DAW::ForensicAuditWindow> forensicWindow_;
    DAW::SelectedTrackPeakBubbleComponent  peakBubble_;
    std::unique_ptr<DAW::QuitSafetyDialog> quitSafetyDialog_;
    bool quitSafetyDialogOpen_ = false;
    std::unique_ptr<DAW::ExportRenderCore> exportRenderCore_;
    juce::Component::SafePointer<juce::Component> exportProgressWindow_;
    uint64_t exportLifecycleGeneration_ = 0;
    uint64_t activeExportLifecycleGeneration_ = 0;
    DAW::AudioResourceReleaseStateCore audioResourceReleaseState_;
    struct PendingAudioDevicePreparation
    {
        DAW::AudioResourceReleaseStateCore::Generation generation = 0;
        int activeInputs = 0;
        int blockSize = 1;
        double sampleRate = 44100.0;
        bool valid = false;
        bool callbackDrainFailed = false;
    } pendingAudioDevicePreparation_;
    DAW::PendingAudioPreparationGateCore pendingAudioPreparationGate_;
    juce::CriticalSection audioDevicePreparationLock_;
    juce::Component::SafePointer<juce::DialogWindow> createSequenceWindow_;

    // Beat-making system (FL Studio clone)
    std::unique_ptr<DAW::StepSequencerWindow> stepSequencerWindow_;
    std::unique_ptr<DAW::DrumSamplerEngine> drumSamplerEngine_;
    std::unique_ptr<DAW::PatternManagerCore> patternManager_;
    DAW::StepSequencerPlaybackCore stepSeqPlayback_;
    std::vector<std::unique_ptr<juce::DocumentWindow>> clipRegionPluginWindows_;
    std::unordered_map<juce::String, bool> clipFxPanelOpenByClipId_;

    bool                                   historyVisible_ = false;
    bool                                   mixerDocked_ = false;
    bool                                   scanStarted_ = false;
    int                                    lastMixerZIndex_ = -1;  // slime follows mixer z-order changes
    // True once the floating mixer has been placed (default position applied)
    // or the user has moved/resized it. Prevents resized() from re-placing
    // the mixer over the app header on every layout pass.
    bool                                   mixerWindowPlaced_ = false;

    DAW::ClickStateModel* clickState_ = nullptr;

    // Shared multi-selection state (owned here, referenced by panels)
    DAW::MultiSelectionCore multiSelection_;

    // Global tooltip window — required for JUCE TooltipClient tooltips to display
    juce::TooltipWindow tooltipWindow_;

   #if JUCE_DEBUG
    // Self-contained FPS overlay — measures actual paint throughput (Debug only)
    struct FPSOverlay : public juce::Component, private juce::Timer
    {
        FPSOverlay() { setInterceptsMouseClicks(false, false); setAlwaysOnTop(true); startTimerHz(60); }
        void paint(juce::Graphics& g) override
        {
            ++frames_;
            g.setColour(juce::Colours::black.withAlpha(0.7f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.f);
            g.setColour(fps_ > 30 ? juce::Colours::lime : fps_ > 15 ? juce::Colours::yellow : juce::Colours::red);
            g.setFont(juce::Font(11.f, juce::Font::bold));
            g.drawText(juce::String((int)fps_) + " FPS", getLocalBounds().reduced(4, 0), juce::Justification::centred);
        }
        void timerCallback() override
        {
            double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            double dt = now - t_;
            if (dt >= 0.5) { fps_ = frames_ / dt; frames_ = 0; t_ = now; }
            repaint();
        }
        int frames_ = 0; double t_ = 0, fps_ = 0;
    } fpsOverlay_;
   #endif

    // Settings button (drawn in paint, hit-tested in mouseDown)
    juce::Rectangle<int> settingsBtn_;
    bool settingsVisible_ = false;

    // Layout constants
    static constexpr int kMenuH        = 28;
    static constexpr int kTransportH   = 52;
    static constexpr int kTrackListMin = 120;
    static constexpr int kTrackListMax = 400;
    static constexpr int kMixerHMin    = 120;
    static constexpr int kMixerHMax    = 500;

    int trackListW_ = 220;  // resizable via drag
    int mixerH_     = 276;  // resizable via drag

    // ActionManager RAII registrations — auto-unregister on destruction
    DAW::ActionManager::ActionRegistration regPlayStop_;
    DAW::ActionManager::ActionRegistration regRecord_;
    DAW::ActionManager::ActionRegistration regSettings_;
    DAW::ActionManager::ActionRegistration regMixer_;
    DAW::ActionManager::ActionRegistration regMarkerAdd_;
    DAW::ActionManager::ActionRegistration regMarkerNext_;
    DAW::ActionManager::ActionRegistration regMarkerPrev_;
    DAW::ActionManager::ActionRegistration regProjectNew_;
    DAW::ActionManager::ActionRegistration regProjectOpen_;
    DAW::ActionManager::ActionRegistration regProjectSave_;
    DAW::ActionManager::ActionRegistration regProjectSaveAs_;
    DAW::ActionManager::ActionRegistration regImportFiles_;
    DAW::ActionManager::ActionRegistration regTrackAddAudio_;
    DAW::ActionManager::ActionRegistration regTrackAddMidi_;
    DAW::ActionManager::ActionRegistration regTrackDuplicate_;
    DAW::ActionManager::ActionRegistration regTrackDeleteSelected_;
    DAW::ActionManager::ActionRegistration regTrackRenameSelected_;
    DAW::ActionManager::ActionRegistration regTrackArmSelected_;
    DAW::ActionManager::ActionRegistration regTrackMuteSelected_;
    DAW::ActionManager::ActionRegistration regTrackSoloSelected_;
    DAW::ActionManager::ActionRegistration regSelectAll_;
    DAW::ActionManager::ActionRegistration regViewTrackList_;
    DAW::ActionManager::ActionRegistration regViewMixer_;
    DAW::ActionManager::ActionRegistration regViewSettings_;
    DAW::ActionManager::ActionRegistration regViewFullScreen_;
    DAW::ActionManager::ActionRegistration regViewHistory_;
    DAW::ActionManager::ActionRegistration regViewStepSequencer_;

    // TrackManager::Listener
    void trackAdded  (DAW::Track*           ) override;
    void trackRemoved(const DAW::TrackID&   ) override;
    void trackOrderChanged() override;

    void registerActions();
    void openProjectFileWithOverlay(const juce::File& file, const juce::String& failureContext,
                                    std::function<void(bool)> onComplete = {});

    /** Opens an old project, runs the full repair chain, saves it back with a
     *  timestamped backup and shows exactly what was upgraded. */
    void upgradeProjectFile(const juce::File& file);
    int getEnabledAudioInputChannelCount() const;
    bool ensureInputChannelsActive(const char* stage);
    void inputWatchdogTick();
    void drainCallbackAuditRing();
    void setupBubblePhysics();
    void undoHistoryChanged() override;
    void startPluginScanIfNeeded();
    void absorbBubbleIntoMaster(const juce::String& sourceId, const juce::String& targetId);
    void addSlotToMaster(const juce::String& id);
    juce::Point<float> getBubbleCenterForId(const juce::String& id) const;
    juce::Component*   getBubbleComponentForId(const juce::String& id) const;
    juce::Colour       getBubbleColorForId(const juce::String& id) const;
    void undockMixer();
    void dockMixer();
    void initializeSafeStartupAudio();
    void prepareForAudioDeviceMutation();
    void setAudioChannels(int numInputChannels, int numOutputChannels);
    void cancelAndJoinExportBeforeAudioRelease();
    void finishExportOnMessageThread(uint64_t exportGeneration);
    void releaseStoppedDeviceResources();
    void applyAudioDevicePreparation(const PendingAudioDevicePreparation& preparation);
    void applyPendingAudioDevicePreparationIfReady();
    void shutdownAudio();
    juce::AudioDeviceManager& getOwnedDeviceManager() noexcept;
    const juce::AudioDeviceManager& getOwnedDeviceManager() const noexcept;
    bool& getAudioCallbackRegisteredFlag() noexcept;
    void processNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill,
                               const juce::AudioBuffer<float>* hardwareInputBuffer,
                               int hardwareInputNumSamples,
                               int validInputChannels);
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void selectSafeStartupAudioDeviceType();
    bool recoverFromFailedAsioPlaybackInit();
    void suspendAudioForDevicePanel();
    void resumeAudioAfterDevicePanel();
    bool repairInvalidCurrentAudioDevice();
    void armAsioCallbackVerification(const char* stage);
    void restartZombieAsioDevice();
    void showAudioDevicePanel();
    void showBubblegumPanel();
    void hideBubblegumPanel();
    void openPianoRollForClip(DAW::MidiClip& clip);
    void openVocalTuneForSelectedClip();
    void showExportAudioDialog();
    void startAudioExport(DAW::ExportSettings settings, const juce::File& displayDestination);
    void showRecordOverwriteConfirmDialog(const std::vector<DAW::RecordArmSafetyCore::OverwriteWarning>& warnings);
    void setFolderCollapsed(const DAW::TrackID& folderTrackId, bool collapsed);
    void syncFolderCollapseState();
    void handleFolderDropRequest(const DAW::TrackID& draggedId, const DAW::TrackID& targetId);
    void handleMixerFolderDropRequest(const DAW::TrackID& draggedId,
                                      const DAW::TrackID& targetId,
                                      DAW::DragMode mode);
    void showFolderDropModePopup(const DAW::TrackID& draggedId, const DAW::TrackID& targetId);
    bool createFolderBusFromDrop(const DAW::TrackID& draggedId, const DAW::TrackID& targetId);
    bool addTrackToExistingFolderFromDrop(const DAW::TrackID& draggedId, const DAW::TrackID& folderBusId);
    bool convertDestinationTrackToFolderBus(const DAW::TrackID& draggedId, const DAW::TrackID& targetId);
    void convertFolderBusToRegularTrack(const DAW::TrackID& folderBusId);
    void exclusiveMuteTrack(const DAW::TrackID& trackId);
    /** Reorder one track using the shared gap-based reorder authority. */
    void moveTrackWithFolderAwareness(const DAW::TrackID& trackId, int destinationGap);
    bool copyPluginBetweenTracks(const DAW::TrackID& srcTrack,
                                 int srcSlot,
                                 const DAW::TrackID& destTrack);
    /** Multi-track reorder: moves a whole selection as one block to a gap in
     *  the original normal-track order, preserving relative order and stable
     *  routing IDs, in a single undo step. */
    void moveTracksWithFolderAwareness(const std::vector<DAW::TrackID>& selectedIds,
                                       int destinationGap);
    /** After a reorder, if the track landed inside an open folder's child
     *  block (the reserved area where children are shown), it automatically
     *  becomes a child of that folder — REAPER/Logic-style. */
    void autoAdoptTrackIntoFolderBlock(const DAW::TrackID& trackId);
    bool adoptTrackIntoFolderBlockWithoutUndo(const DAW::TrackID& trackId);
    bool shouldDetachTrackFromOpenFolder(const DAW::TrackID& trackId, int targetIndex) const;
    void executeTopologyCommand(std::function<void()> mutation, const juce::String& description);
    void handleTrackSelection(const DAW::TrackID& id, const juce::ModifierKeys& mods);
    /** Synchronize Bubblegum's presentation source after a canonical UI
     *  selection change. Quick Send intentionally owns its own pinned source. */
    void syncBubblegumSourceSelection(const DAW::TrackID& trackId);
    void syncTrackSelectionVisuals();
    std::vector<DAW::TrackID> getSelectedTrackIdsInProjectOrder() const;
    bool deleteTracksAsOneTopologyTransaction(const std::vector<DAW::TrackID>& requestedIds);
    void beginContinuousTopologyGesture(const juce::String& description);
    void commitContinuousTopologyGesture(const juce::String& description);
    bool saveProjectToFile(const juce::File& file);
    bool augmentProjectStateForSave(juce::ValueTree& state, juce::String& error);
    void refreshBubblegumFeedback();
    void refreshPreFaderStates();
    /** Per-target pre/post-fader toggle: one send toggles directly, multiple
     *  sends open a menu listing every target with its own PRE/POST state. */
    void handlePreFaderTogglePressed(const DAW::TrackID& sourceId);
    void refreshBubblegumOffscreenState();
    // ── Quick Send mode ────────────────────────────────────────────────────
    /** Enter/retarget/exit Quick Send mode for the given source track. */
    void toggleQuickSendMode(const DAW::TrackID& sourceId);
    /** Exit Quick Send mode (aura off, popup hidden). */
    void exitQuickSendMode();
    /** Create (or re-enable) a send from the active Quick Send source to targetId. */
    void quickSendCreateSend(const DAW::TrackID& targetId);
    /** Delete the send from the active Quick Send source to the selected track. */
    void quickSendDeleteSend();
    /** Delete the send from the active Quick Send source to an explicit target track. */
    void quickSendDeleteSendTo(const DAW::TrackID& targetId);
    /** Push Quick Send state to the TrackList rows + popup. */
    void updateQuickSendUI();
    /** True when the mixer is currently shown (docked viewport or floating window). */
    bool isMixerOpen() const noexcept;
    /** True when keyboard focus belongs to the mixer stack or the Bubblegum panel. */
    bool hasMixerKeyboardFocus() const;
    /** Left/Right arrow navigation: step the mixer selection to the previous/next track. */
    void cycleMixerSelection(int direction);
    /** Apply the existing Timeline zoom operation, preserving its anchor policy. */
    void applyTimelineZoom(bool isVertical, bool zoomIn);
    /** Scroll the mixer viewport so the given track's strip is minimally revealed. */
    void scrollMixerViewportToTrack(const DAW::TrackID& trackId);
    /** Apply the six-normal-strip + Master floating Mixer default. */
    void setDefaultMixerWindowBounds();
    bool deleteTrackOrFolder(const DAW::TrackID& trackId);
    void updateTimelineViewportContentBounds();

    /** Last content-extent computed by updateTimelineViewportContentBounds();
     *  used to gate playhead-driven extent growth so the viewport bounds are
     *  not rebuilt on every transport tick. */
    double lastTimelineContentLengthSeconds_ = 30.0;

    // RoutingGraph::Listener — real-time send graph updates
    void connectionAdded(DAW::RoutingConnection* conn) override;
    void connectionRemoved(const DAW::RouteID& connId) override;

    DAW::BubbleOrchestrator bubbleOrchestrator_;
    std::unordered_set<DAW::TrackID> collapsedFolderTrackIds_;
    bool continuousTopologyGestureActive_ = false;
    juce::ValueTree continuousTopologyBeforeState_;
    bool topologyMutationInProgress_ = false;
    bubblegum::QuickSendModeCore quickSendMode_;

    struct OffscreenRefreshCache
    {
        bool active = false;
        bool mixerDocked = false;
        int viewportScrollX = std::numeric_limits<int>::min();
        juce::Rectangle<int> mainBounds;
        juce::Rectangle<int> mixerBounds;
        juce::Rectangle<int> viewportBounds;
        DAW::TrackID effectiveSource;
        uint64_t graphVersion = 0;
    } offscreenRefreshCache_;
    // ── Autosave / recovery UI state ─────────────────────────────────────────
    juce::String autosaveStatusText_;          ///< "Autosaved 1:42 PM" — shown in status bar
    bool         recoveryBannerVisible_ = false; ///< true until first manual Save As
    bool         recoveryPromptFired_   = false; ///< ensures deferred prompt fires only once
    bool         audioSuspendedForDevicePanel_ = false;
    bool         audioWasRunningBeforeDevicePanel_ = false;
    std::atomic<int> cachedEnabledAudioInputChannels_ { 0 };
    juce::AudioBuffer<float> liveCallbackInputBuffer_;
    // Set by audio callback when backend exceeds prepared input capacity.
    // Cleared by message thread after repreparation.
    std::atomic<bool> needsInputBufferReprepare_ { false };

    // ── Callback audit — conditional allocation-free timing instrumentation ──
    bool callbackAuditEnabled_ = false;
    uint32_t callbackAuditStreamGeneration_ = 0;
    int64_t callbackAuditPeriodTicks_ = 0;
    int64_t callbackAuditPrevStartTicks_ = 0;
    uint64_t callbackAuditSequence_ = 0;
    CallbackAuditRing<1024> callbackAuditRing_;
    CallbackAuditAccumulator callbackAuditAccumulator_;
    juce::String callbackAuditOutputPath_;
    int64_t callbackAuditLastDrainTicks_ = 0;
    double callbackAuditDrainIntervalSeconds_ = 5.0;

    // Adaptive ring drain (decoupled from the 5 s report cadence): keeps
    // ringOverflows meaningful at 32/64-sample buffers (Brain §34.21).
    struct CallbackAuditDrainTimer : public juce::Timer
    {
        explicit CallbackAuditDrainTimer(MainComponent& o) : owner(o) {}
        void timerCallback() override { owner.drainCallbackAuditRing(); }
        MainComponent& owner;
    } callbackAuditDrainTimer_ { *this };

    // ── Input watchdog — self-heals dead input channels mid-session ─────────
    struct InputWatchdogTimer : public juce::Timer
    {
        explicit InputWatchdogTimer(MainComponent& o) : owner(o) {}
        void timerCallback() override { owner.inputWatchdogTick(); }
        MainComponent& owner;
    } inputWatchdog_ { *this };
    int  inputWatchdogRepairAttempts_ = 0;
    bool inputWatchdogWasHealthy_     = false;
    static constexpr int kMaxInputWatchdogRepairAttempts = 5;

    // ── ASIO zombie detector — driver open+started but never calling back ───
    std::atomic<uint32_t> audioCallbackTickCounter_ { 0 };
    int asioZombieRestartAttempts_ = 0;
    static constexpr int kMaxAsioZombieRestartAttempts = 2;

    // Autosave failure toast throttle
    juce::Time   lastAutosaveFailToastTime_;
    static constexpr int kAutosaveFailToastIntervalSec = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

