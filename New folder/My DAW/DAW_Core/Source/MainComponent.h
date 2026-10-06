#pragma once

#include <JuceHeader.h>
#include <unordered_map>
#include <unordered_set>
#include "AppCore/ApplicationCore.h"
#include "UICore/DAWMenuBar.h"
#include "UICore/TransportBar.h"
#include "UICore/SettingsPanel.h"
#include "UICore/TrackList.h"
#include "UICore/ArrangementView.h"
#include "UICore/MixerPanel.h"
#include "UICore/MonitorSectionUI.h"
#include "UICore/ControlRoomSettingsUI.h"
#include "DeviceCore/AudioDeviceSettingsUI.h"
#include "DeviceCore/DeviceSessionCore.h"
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
#include "SelectionCore/MultiSelectionCore.h"
#include "SelectionCore/SelectionBulkActionCore.h"
#include "ClickCore/ClickStateModel.h"
#include "UICore/QuitSafetyDialog.h"

namespace DAW { class PianoRollWindow; }

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
                      private DAW::ProjectManager::Listener,
                      private DAW::TrackManager::Listener
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
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    bool requestQuitFromWindowClose();
    void scrollBarMoved(juce::ScrollBar*, double) override;
    void componentMovedOrResized(juce::Component&, bool, bool) override;

    // ProjectManager::Listener
    void projectSaved() override;
    void projectLoaded() override;

    /** Wire a scan-active callback into the plugin scanner.
     *  Called from Main.cpp after both MainComponent and the suppressor exist.
     *  The callback receives true when a scan starts, false when it ends. */
    void setScanActiveCallback(std::function<void(bool)> cb)
    {
        appCore_.getPluginScanner().onScanActiveChanged = std::move(cb);
    }

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
    juce::Viewport                         mixerViewport_;
    DAW::BubblegumCableOverlayComponent     cableOverlay_;
    DAW::BubblegumOffscreenEndpointComponent offscreenEndpoint_;
    std::unique_ptr<DAW::TrackListWindow>  trackListWindow_;
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
    std::unique_ptr<DAW::FloatingPanelChrome>  sidePanelChrome_;
    std::unique_ptr<DAW::FloatingPanelChrome>  browserChrome_;
    std::unique_ptr<DAW::PluginScanStartupDialog> scanDialog_;
    std::unique_ptr<DAW::VirtualCursor>    virtualCursor_;
    std::unique_ptr<DAW::UndoHistoryPanel> historyPanel_;
    std::unique_ptr<DAW::ShortcutHelpFloatingWindow> shortcutHelpWindow_;
    std::unique_ptr<DAW::ForensicAuditWindow> forensicWindow_;
    DAW::SelectedTrackPeakBubbleComponent  peakBubble_;
    std::unique_ptr<DAW::QuitSafetyDialog> quitSafetyDialog_;
    bool quitSafetyDialogOpen_ = false;
    std::unique_ptr<DAW::ExportRenderCore> exportRenderCore_;
    juce::Component::SafePointer<juce::DialogWindow> createSequenceWindow_;
    std::vector<std::unique_ptr<juce::DocumentWindow>> clipRegionPluginWindows_;
    std::unordered_map<juce::String, bool> clipFxPanelOpenByClipId_;

    bool                                   historyVisible_ = false;
    bool                                   mixerDocked_ = false;
    bool                                   scanStarted_ = false;

    DAW::ClickStateModel* clickState_ = nullptr;

    // Shared multi-selection state (owned here, referenced by panels)
    DAW::MultiSelectionCore multiSelection_;

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
    DAW::ActionManager::ActionRegistration regViewTrackList_;
    DAW::ActionManager::ActionRegistration regViewMixer_;
    DAW::ActionManager::ActionRegistration regViewSettings_;
    DAW::ActionManager::ActionRegistration regViewFullScreen_;
    DAW::ActionManager::ActionRegistration regViewHistory_;

    // TrackManager::Listener
    void trackAdded  (DAW::Track*           ) override;
    void trackRemoved(const DAW::TrackID&   ) override;

    void registerActions();
    int getEnabledAudioInputChannelCount() const;
    bool ensureInputChannelsActive(const char* stage);
    void inputWatchdogTick();
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
    void setAudioChannels(int numInputChannels, int numOutputChannels);
    void shutdownAudio();
    juce::AudioDeviceManager& getOwnedDeviceManager() noexcept;
    const juce::AudioDeviceManager& getOwnedDeviceManager() const noexcept;
    bool& getAudioCallbackRegisteredFlag() noexcept;
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
    void showExportWavDialog();
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
    void moveTrackWithFolderAwareness(const DAW::TrackID& trackId, int targetIndex);
    bool shouldDetachTrackFromOpenFolder(const DAW::TrackID& trackId, int targetIndex) const;
    void executeTopologyCommand(std::function<void()> mutation, const juce::String& description);
    void beginContinuousTopologyGesture(const juce::String& description);
    void commitContinuousTopologyGesture(const juce::String& description);
    void refreshBubblegumFeedback();
    void refreshBubblegumOffscreenState();
    bool deleteTrackOrFolder(const DAW::TrackID& trackId);
    void updateTimelineViewportContentBounds();

    // RoutingGraph::Listener — real-time send graph updates
    void connectionAdded(DAW::RoutingConnection* conn) override;
    void connectionRemoved(const DAW::RouteID& connId) override;

    DAW::BubbleOrchestrator bubbleOrchestrator_;
    std::unordered_set<DAW::TrackID> collapsedFolderTrackIds_;
    bool continuousTopologyGestureActive_ = false;
    juce::ValueTree continuousTopologyBeforeState_;

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

