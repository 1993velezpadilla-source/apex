// ===========================================================================
// ArrangementViewCore.h
// Main arrangement view container.
// Composes: Ruler, Toolbar, Clips, Selection, Undo, Zoom.
// Routes all mouse events through the active tool.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include "ArrangementClipStateCore.h"
#include "ArrangementSelectionCore.h"
#include "ArrangementZoomCore.h"
#include "ArrangementUndoCore.h"
#include "EditorToolCore.h"
#include "EditorToolSelectorCore.h"
#include "ArrangementToolbarCore.h"
#include "ArrangementRulerCore.h"
#include "ClipRenderCore.h"
#include "RubberBandSelectCore.h"
#include "ArrangementDebugLogCore.h"
#include "ArrangementDebugPanelCore.h"
#include "ClipSplitIndicatorCore.h"
#include "PianoRollButtonCore.h"
#include "ClipPropertiesWindowCore.h"
#include "ClipContextMenuCore.h"
#include "ClipAutomationPanel.h"
#include "TimePitchBounceCore.h"
#include "AutomationLaneContainerComponent.h"
#include "ArrangementSnapCore.h"
#include "../../../Source/Automation/AutomationLaneStoreCore.h"
#include "../../../Source/AutomationCore/AutomationManagerCore.h"
#include "../../../Source/DiagnosticsCore/TimelinePaintMetrics.h"
#include "../../../Source/Automation/AutomationParameterKeyCore.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"             
#include "../../../Source/SoundEngineCore/ApexClipCrossfadeCore.h"
#include "../../../Source/CommandCore/CommandManager.h"
#include <vector>                                              
#include <memory>                                              
#include <map>
#include <set>
#include <iterator>
#include <optional>
#include <queue>
#include <unordered_set>

// Forward-declare DAW engine types so we don't pull in heavy headers.
// Call setAudioEngineBridge() after construction to enable audio-engine sync on split.
namespace DAW
{
    class Clip;
    class ClipManager;
    class AudioFileManager;
    class MidiClip;
    class Track;
    class TrackManager;
    class AutomationManagerCore;
    class AutomationLaneCore;
    class ClipRegionPluginCore;
}

namespace ArrangementEditor
{
    class ArrangementViewCore : public juce::Component,
                                private EditorToolState::Listener,
                                public juce::ChangeListener,
                                private DAW::CommandManager::Listener
    {
        class GhostOverlay : public juce::Component
        {
        public:
            explicit GhostOverlay(ArrangementViewCore& owner) : m_owner(owner)
            {
                setInterceptsMouseClicks(false, false);
                setAlwaysOnTop(true);
            }

            void paint(juce::Graphics& g) override { m_owner.drawGhost(g); }

        private:
            ArrangementViewCore& m_owner;
        };

        // Opaque mask for the fixed toolbar/ruler strip.  The arrangement is
        // itself the Viewport view, so scrolling translates all of its
        // children.  This mask is translated with the pinned controls and
        // explicitly occludes scrollable clip/automation pixels instead of
        // relying on z-order or transparent toolbar paint.
        class HeaderClipGuard : public juce::Component
        {
        public:
            HeaderClipGuard()
            {
                setInterceptsMouseClicks(false, false);
            }

            void paint(juce::Graphics& g) override
            {
                g.fillAll(juce::Colour(0xFF05070C));
            }
        };

        // ── Header button shield ──────────────────────────────────────────
        // The pinned buttons (lock / view-only / piano roll) are painted by
        // this view while the clips are child components scrolling beneath
        // them — without a shield the clip swallows the click. This
        // transparent component sits above everything in that corner and
        // forwards the events to the view / piano-roll button.
        class HeaderButtonShield : public juce::Component
        {
        public:
            explicit HeaderButtonShield(ArrangementViewCore& owner) : m_owner(owner)
            {
                setInterceptsMouseClicks(true, false);
            }

            void mouseDown(const juce::MouseEvent& e) override
            {
                const auto ownerPos = e.getEventRelativeTo(&m_owner);
                if (m_owner.m_pianoRollBtn.getBounds().contains(ownerPos.getPosition()))
                {
                    if (m_owner.m_pianoRollBtn.onClick)
                        m_owner.m_pianoRollBtn.onClick();
                    return;
                }
                m_owner.mouseDown(ownerPos);
            }

            void mouseMove(const juce::MouseEvent& e) override
            {
                m_owner.mouseMove(e.getEventRelativeTo(&m_owner));
            }

        private:
            ArrangementViewCore& m_owner;
        };

        // ── Automation Update Timer ───────────────────────────────────────
        class AutomationUpdateTimer : public juce::Timer
        {
        public:
            explicit AutomationUpdateTimer(ArrangementViewCore& owner) : m_owner(owner) {}

            uint64_t m_lastAudioGeneration = 0;

            void timerCallback() override
            {
                // Audio content changed elsewhere (e.g. Normalize toggled in
                // clip properties): rebuild the clip waveforms so the timeline
                // reflects the new audio even when automation is unchanged.
                if (m_owner.m_engineAudioFiles != nullptr)
                {
                    const uint64_t audioGeneration = m_owner.m_engineAudioFiles->getChangeGeneration();
                    if (m_lastAudioGeneration != audioGeneration)
                    {
                        m_lastAudioGeneration = audioGeneration;
                        m_owner.rebuildClipRenderers();
                    }
                }

                // ── Revision gating: skip invalidation + repaint when no
                //     automation data has changed since last tick. ───────
                DAW::AutomationManagerCore* mgr = m_owner.m_automationManager;
                if (mgr == nullptr)
                    return;  // no manager wired — zero repaint

                const uint64_t currentVersion = mgr->getSnapshotVersion();
                // First observation after manager reset / startup:
                // version may be 0 (constructor), 1 (first publish), or
                // any other value.  Force one correct refresh regardless.
                // After that, skip only when version is unchanged.
                if (m_hasObservedVersion && currentVersion == m_lastObservedVersion)
                    return;  // idle — no change, zero repaint

                m_lastObservedVersion  = currentVersion;
                m_hasObservedVersion   = true;

                const bool metricsActive = TimelinePaintMetrics::active.load(std::memory_order_relaxed);
                if (!metricsActive)
                {
                    // Still need to invalidate cache paths and repaint for correct display.
                    for (auto& [trackId, container] : m_owner.m_automationContainers)
                        container->invalidateAllCurvePaths();
                    m_owner.repaint();
                    return;
                }

                // F0 baseline: every tick invalidates + full repaint
                TimelinePaintMetrics::automationTimerTicks.fetch_add(1);

                for (auto& [trackId, container] : m_owner.m_automationContainers)
                {
                    container->invalidateAllCurvePaths();
                    TimelinePaintMetrics::automationCacheInvalidations.fetch_add(1);
                }
                TimelinePaintMetrics::automationContainersProcessed.fetch_add(
                    static_cast<uint64_t>(m_owner.m_automationContainers.size()));

                TimelinePaintMetrics::requestedFullRepaints.fetch_add(1);
                {
                    auto b = m_owner.getLocalBounds();
                    auto safeW = static_cast<uint64_t>(std::max(0, b.getWidth()));
                    auto safeH = static_cast<uint64_t>(std::max(0, b.getHeight()));
                    auto area = safeW * safeH;
                    TimelinePaintMetrics::requestedDirtyAreaPixels.fetch_add(static_cast<int64_t>(area));
                    TimelinePaintMetrics::requestedDirtyAreaPixelsRing.push(area);
                }
                m_owner.repaint();
            }

        private:
            ArrangementViewCore& m_owner;
            uint64_t m_lastObservedVersion = 0;
            bool     m_hasObservedVersion  = false;
        };

    public:
        ArrangementViewCore();
        ~ArrangementViewCore() override;

        // Access to subsystems
        ArrangementClipStateCore& getClipState() { return m_clipState; }
        ArrangementSelectionCore& getSelection() { return m_selection; }
        ArrangementZoomCore& getZoom() { return m_zoom; }
        EditorToolState& getToolState() { return m_toolState; }
        ArrangementToolbarCore& getToolbar() { return m_toolbar; }

        // ── Diagnostics / benchmark access ──────────────────────────────────
        /** Benchmark and diagnostics use ONLY — do not bypass production authority.
         *  Returns waveform cache readiness snapshot for audio clip renderers
         *  intersecting the visible viewport.
         *  Does not alter waveform generation, caching, paint behavior, or timing.
         *  Does not expose renderer pointers, containers, or locks. */
        /** Result of getVisibleWaveformReadinessForDiagnostics().
         *  All counts refer to the actual viewport-visible area only. */
        struct WaveformReadinessSnapshot
        {
            int visibleAudioRenderers = 0;
            int ready   = 0;
            int notReady = 0;
            int missingRenderer = 0;
            int totalVisibleRenderers = 0;  // all renderers in viewport (audio+MIDI+pattern)
            int viewportWidthPx  = 0;       // actual visible viewport width
            int viewportHeightPx = 0;       // actual visible viewport height
            bool allReady() const noexcept
            {
                return visibleAudioRenderers > 0
                    && ready == visibleAudioRenderers
                    && notReady == 0
                    && missingRenderer == 0;
            }
        };

        WaveformReadinessSnapshot getVisibleWaveformReadinessForDiagnostics() const;

        // ── Automation lane management ────────────────────────────────────
        /**
         * Set automation manager for displaying automation lanes.
         * Must be called before creating automation lanes.
         */
        void setAutomationManager(DAW::AutomationManagerCore* automationManager);

        /**
         * Get or create automation lane container for a track.
         */
        DAW::AutomationLaneContainerComponent* getOrCreateAutomationContainer(
            const DAW::TrackID& trackId);

        /**
         * Show automation lane for a specific parameter on a track.
         */
        void showAutomationLane(const DAW::TrackID& trackId, const juce::String& parameterId);

        /**
         * Hide automation lane for a specific parameter on a track.
         */
        void hideAutomationLane(const DAW::TrackID& trackId, const juce::String& parameterId);

        /**
         * Clear all automation lanes for a track.
         */
        void clearAutomationLanes(const DAW::TrackID& trackId);

        /**
         * Update layout of all automation containers.
         */
        void updateAutomationLayout();

        int getYForTrackId(const DAW::TrackID& trackId) const;
        int getYForTrack(const DAW::TrackID& trackId) const { return getYForTrackId(trackId); }
        juce::Rectangle<int> getTrackLaneBounds(const DAW::TrackID& trackId) const;
        juce::Rectangle<int> getClipBounds(const juce::Uuid& clipId) const;
        DAW::TrackID getTrackAtY(int y) const;
        std::vector<DAW::TrackID> getVisibleTrackIds() const;
        DAW::TrackID resolveSelectedTrack() const;
        DAW::TrackID resolveArmedTrack() const;
        void repaintAutomationLane(const DAW::TrackID& trackId, const juce::String& target);
        void repaintClip(const juce::Uuid& clipId);

        // ── ChangeListener for automation data updates ─────────────────────
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;

        /** Wire the real DAW audio engine so clips are playable.
         *  Must be called before any clips are added.
         *  All pointers must remain valid for the lifetime of this component. */
        void setAudioEngineBridge(DAW::ClipManager*      clipManager,
                                  DAW::AudioFileManager* audioFileManager,
                                  DAW::TrackManager*     trackManager     = nullptr,
                                  double                 engineSampleRate = 44100.0,
                                  DAW::ClipRegionPluginCore* clipRegionPlugins = nullptr,
                                  juce::AudioPluginFormatManager* pluginFormatManager = nullptr);

        void setRoutingGraph(DAW::RoutingGraph* routingGraph) noexcept { m_engineRoutingGraph = routingGraph; }

                 void addOrUpdateEngineClipModel(const ArrangementClipModel& model, const juce::String& engineClipId);
                 bool isSyncingArrangementToEngine() const noexcept { return m_syncingArrangementToEngine; }

                          /** Update engine sample rate (call from prepareToPlay). Recalculates all existing clips. */
                          void setEngineSampleRate(double sr);

                 private:
                          void recalculateEngineClipsForNewSampleRate(double oldSr, double newSr);
                 public:
                          void setPlayheadPosition(double timeSeconds);
        void setViewportScrollOffset(int scrollX, int scrollY);
        void setSnapMode(SnapMode mode);
        SnapMode getSnapMode() const noexcept { return m_snapMode; }
        int getSnapModeIndex() const noexcept;
        void setTempoBpm(double bpm);

        /** Set the set of collapsed FolderBus track IDs. Tracks whose folder
         *  ancestor is collapsed are hidden from the arrangement (lane content,
         *  clips, automation) without disturbing the lane-index math. */
        void setCollapsedFolderIds(const std::unordered_set<DAW::TrackID>& ids);

        /** True when the track (or any of its FolderBus ancestors) is in the
         *  collapsed set — i.e. its lane content should not be shown. */
        bool isTrackHiddenByCollapsedFolder(const DAW::Track* track) const;

        void paint(juce::Graphics& g) override;
        void paintOverChildren(juce::Graphics& g) override;
        void resized() override;
        void drawPhase1VolumeAutomation(juce::Graphics& g);
        void drawApexAutomationLane(juce::Graphics& g,
                                    apex::automation::ParameterID paramID,
                                    juce::Rectangle<float> laneBounds,
                                    juce::Colour colour,
                                    bool stepped);
        bool hasApexAutomationLane(apex::automation::ParameterID paramID) const;
        void drawCoreAutomationLane(juce::Graphics& g,
                                    const DAW::AutomationLaneCore& lane,
                                    juce::Rectangle<float> laneBounds,
                                    juce::Colour colour);

        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e,
                            const juce::MouseWheelDetails& wheel) override;

        bool keyPressed(const juce::KeyPress& key) override;

        // CommandManager::Listener — refresh timeline immediately on undo/redo
        void undoHistoryChanged() override;

        std::function<void(DAW::Clip&)> onEngineClipSelected;
        std::function<void()> onEngineClipSelectionCleared;
        std::function<void(DAW::Clip&)> onEngineClipDoubleClicked;
        std::function<void(DAW::Clip&)> onEngineClipAutomationRequested;
        std::function<void(DAW::Clip&)> onEngineClipVocalTuneRequested;
        std::function<void(DAW::Clip&)> onEngineClipVocalTuneBypassToggled;
        std::function<bool(const DAW::Clip&)> onEngineClipIsVocalTuneBypassed;
        std::function<void(DAW::MidiClip&)> onEngineMidiClipDoubleClicked;
        std::function<void()> onOpenPianoRollRequested;
        std::function<void(double)> onPlayheadMoved;

        // Test-only control-plane seam. Returning true requests a failure at
        // the named Split transaction stage; production leaves it unset.
        std::function<bool(const juce::String&)> splitTransactionFailureInjector;

    public:
        // -----------------------------------------------------------------------
        // Core systems
        // -----------------------------------------------------------------------
        ArrangementClipStateCore m_clipState;
        ArrangementSelectionCore m_selection;
        ArrangementZoomCore      m_zoom;
        ArrangementUndoCore      m_undo;
        EditorToolState          m_toolState;

        // -----------------------------------------------------------------------
        // UI components
        // -----------------------------------------------------------------------
        ArrangementToolbarCore   m_toolbar;      // NEW: Visible tool buttons
        ArrangementRulerCore     m_ruler;
        HeaderClipGuard          m_headerClipGuard;
        // Transparent shield above the pinned-button cluster (created lazily in
        // updatePinnedViewportControls).
        std::unique_ptr<HeaderButtonShield> m_headerButtonShield;
        RubberBandSelectCore     m_rubberBand;
        ClipSplitIndicatorCore   m_splitIndicator;
        PianoRollButtonCore      m_pianoRollBtn; // NEW: Piano Roll button
        GhostOverlay             m_ghostOverlay;

        // ── Horizontal-lock button (locks clips to vertical-only movement) ──
        juce::Rectangle<int> m_lockBtnBounds;
        bool m_lockHorizontal = false;
        bool m_lockBtnHovered = false;
        // Drawn from paintOverChildren() so the opaque HeaderClipGuard child
        // cannot hide it.
        void drawLockButton(juce::Graphics& g);

        // ── Free View Mode button (view-only: scroll/zoom, no editing) ──
        juce::Rectangle<int> m_viewOnlyBtnBounds;
        bool m_viewOnlyMode = false;
        bool m_viewOnlyBtnHovered = false;
        // Applies/removes the view-only mouse guards on clips and lanes.
        void applyViewOnlyMode();

        // ── Empty-space gesture state: 1-finger drag = scroll, hold = marquee ──
        juce::Point<int> m_gestureDownPos;
        int  m_gestureStartViewX = 0;
        int  m_gestureStartViewY = 0;
        bool m_holdFired = false;      // held still long enough → marquee armed
        bool m_gestureMoved = false;   // moved past the threshold → it is a drag
        bool m_marqueePending = false; // empty-space press waiting for the hold
        bool m_touchScrollActive = false; // a touch pan gesture is in progress
        double m_lastPinchDistance = 0.0; // last two-finger separation (pinch)
        juce::Point<float> m_lastTwoFingerPos { -1.0f, -1.0f }; // two-finger pan anchor
        // Gesture classification: decided once per gesture (pan by default,
        // promoted to pinch on a clear separation change) so the two never
        // flip-flop mid-gesture.
        bool m_gestureIsPinch = false;
        double m_lastPinchSepX = 0.0;   // last horizontal finger separation
        double m_lastPinchSepY = 0.0;   // last vertical finger separation
        // Low-pass filtered touch position: touch digitizers jitter ±1 px even
        // on a nearly still finger, which made slow pans shake.
        juce::Point<float> m_smoothedTouchPos;
        bool m_smoothedTouchValid = false;
        bool m_skipSlop = false;   // finger landed on a running glide → respond at once

        // ── Long-press gesture (hold = right click, hold+drag = marquee) ──
        juce::Point<float> m_holdLocalPos;      // where the finger is holding
        double m_holdStartMs = 0.0;             // when the still finger started
        bool   m_holdActive = false;            // glow pulse visible
        bool   m_holdDragging = false;          // hold turned into a marquee
        juce::Time m_gestureTouchDownTime;      // exact new-gesture detection
        void onEmptySpaceHold();

        // ── Touch pan inertia (iOS-style momentum) ──
        class TouchPanInertiaTimer : public juce::Timer
        {
        public:
            explicit TouchPanInertiaTimer(ArrangementViewCore& owner) : m_owner(owner) {}
            void timerCallback() override
            {
                if (! m_owner.applyTouchPanInertia())
                    stopTimer();
            }
        private:
            ArrangementViewCore& m_owner;
        };
        std::unique_ptr<TouchPanInertiaTimer> m_inertiaTimer;
        juce::Point<int> m_lastTouchPanPos;
        double m_lastTouchPanTimeMs = 0.0;
        double m_touchPanVelocityX = 0.0;   // pixels per 16 ms frame
        double m_touchPanVelocityY = 0.0;
        bool applyTouchPanInertia();
        void endTouchPanGesture();

        std::unique_ptr<ClipPropertiesWindowCore> m_propertiesWindow;

        // Clip renderers (one per clip)
        std::vector<std::unique_ptr<ClipRenderCore>> m_clipRenderers;
        std::set<juce::Uuid> m_lastVisualSelection;
        std::vector<std::pair<juce::Uuid, juce::Rectangle<int>>> m_marqueeHitGeometry;

        // -----------------------------------------------------------------------
        // Automation support
        // -----------------------------------------------------------------------
        DAW::AutomationManagerCore* m_automationManager = nullptr;
        std::unique_ptr<DAW::AutomationManagerCore> m_fallbackAutomationManager; // used when no external manager is wired
        std::unique_ptr<DAW::AutomationUIHelper> m_automationHelper;
        std::map<DAW::TrackID, std::unique_ptr<DAW::AutomationLaneContainerComponent>> m_automationContainers;
        std::unique_ptr<AutomationUpdateTimer> m_automationUpdateTimer;                                                   


        // Tool handlers
        // -----------------------------------------------------------------------
        void handleSelectTool(const juce::MouseEvent& e);
        void handleSplitTool(const juce::MouseEvent& e);
        void performSplit(const ArrangementClipModel& clipSnapshot, double splitTime);
        void handleEraserTool(const juce::MouseEvent& e);
        void handleDrawTool(const juce::MouseEvent& e);
        void handleGlueTool(const juce::MouseEvent& e);
        void handleMuteTool(const juce::MouseEvent& e);

        // -----------------------------------------------------------------------
        // Tool state listener & cursor
        // -----------------------------------------------------------------------
        void activeToolChanged(EditorTool newTool) override;
        void setMouseCursorForActiveTool();

        // -----------------------------------------------------------------------
        // Helpers
        // -----------------------------------------------------------------------
        void rebuildClipRenderers();
        void rebuildClipRenderersInternal();  // Internal method called after recursion guard check
        void upsertClipRenderer(const juce::Uuid& clipId);

        /** Keep the pinned toolbar + ruler above every dynamically-created
         *  child (clip renderers, automation containers, overlays) so clicks
         *  meant for the toolbar strip — e.g. the Quick New Track button —
         *  can never be swallowed or re-routed by content scrolled underneath.
         *  Call after EVERY child-mutation path. */
        void ensureToolbarZOrder();
        bool isAutomationComponentAt(juce::Point<int> pos) const;
        bool isAutomationInteractionBlockingClipAt(juce::Point<int> pos) const;
        void updateClipAutomationDim();
        void updatePinnedViewportControls();
        juce::Rectangle<int> getViewportVisibleBounds() const;
        bool shouldShowClipRenderer(const ClipRenderCore& renderer) const;
        void updateClipRendererVisibility();
        ClipRenderCore* findClipRendererAt(juce::Point<int> pos);
        ClipRenderCore* findClipRendererFor(const juce::Uuid& clipId) const;
        ArrangementClipModel* findClipAt(juce::Point<int> pos);
        void syncClipToEngine(const ArrangementClipModel& clip);
        void refreshAutoCrossfadesForTrack(int trackIndex);
        void refreshAutoCrossfadesForAllTracks();
        double screenXToTime(int x) const;
        int timeToScreenX(double time) const;
        int arrangementContentTop() const;
        int computeClampedDragAnchorX() const;
        int computeSnappedDragAnchorX() const;
        double snapTimeToGrid(double time) const;
        int trackIndexAt(juce::Point<int> pos) const;
        int getArrangementTrackCount() const;
        int clampTrackIndex(int trackIndex) const;
        std::vector<juce::Uuid> getVisualSelectedClipIds() const;
        int getVisualSelectedClipCount() const;
        void purgeInvalidSelectedClips();
        void updateRubberBandSelection(const juce::Rectangle<int>& bounds);
        juce::String formatUuidShort(const juce::Uuid& id) const;
        juce::String formatRect(const juce::Rectangle<int>& r) const;
        bool areAllDragItemBoundsIdentical() const;
        bool areAllGhostPositionsIdentical() const;
        juce::String buildArrangementDebugText() const;
        void appendDebugLog(const juce::String& line);

    public:
        /** Coalesce project-load clip replacement into one renderer rebuild. */
        void beginEngineClipBatch(bool clearExisting);
        void endEngineClipBatch();

        int m_engineClipBatchDepth = 0;
        void toggleArrangementDebugPanel();

        // Optional DAW engine bridge — null when running standalone / without audio engine
        DAW::ClipManager*      m_engineClipManager  = nullptr;
        DAW::AudioFileManager* m_engineAudioFiles   = nullptr;
        DAW::TrackManager*     m_engineTrackManager = nullptr;
        DAW::RoutingGraph*     m_engineRoutingGraph = nullptr;
        DAW::ClipRegionPluginCore* m_clipRegionPlugins = nullptr;
        juce::AudioPluginFormatManager* m_pluginFormatManager = nullptr;
        double                 m_engineSampleRate   = 44100.0;
        bool                   m_engineSampleRateInitialized = false;  // true after prepareToPlay
        bool                   m_suppressEngineClipCreation = false;
        bool                   m_syncingArrangementToEngine = false;
        bool                   m_syncingEngineToArrangement = false;
        double                 m_playheadTimeSeconds = 0.0;

        // Maps ArrangementClipModel UUID → engine ClipID (for remove/split sync)
        std::map<juce::Uuid, juce::String> m_uuidToEngineId;

        // TimePitch bounce / freeze per clip
        TimePitchBounceCore m_bounceCore;
        std::map<juce::Uuid, ClipFreezeState> m_freezeStates;

        // Context menu helpers
        void showClipContextMenu(ArrangementClipModel* clip,
                                  juce::Point<int> screenPos,
                                  juce::Point<int> localPos);
        void showEmptyTrackContextMenu(juce::Point<int> screenPos,
                                       juce::Point<int> localPos);
        void onClipCut     (ArrangementClipModel* clip);
        void onClipCopy    (ArrangementClipModel* clip);
        void onClipDuplicate(ArrangementClipModel* clip);
        void onClipDelete  (ArrangementClipModel* clip);
        void onClipRename  (ArrangementClipModel* clip);
        void onClipMute    (ArrangementClipModel* clip);
        void onClipPaste   (double time, int trackIndex);
        void onClipBounce  (ArrangementClipModel* clip);
        void onClipFreeze  (ArrangementClipModel* clip);
        void onClipUnfreeze(ArrangementClipModel* clip);
        void onClipPreset  (ArrangementClipModel* clip, const std::string& name);
        void onClipResetTP (ArrangementClipModel* clip);
        // Creates/shows clip pitch & stretch automation lanes matching a
        // preset's time-pitch state (and clears them when the state is identity).
        void clearPresetAutomationLanes(ArrangementClipModel* clip);
        // Creates the clip pitch/stretch automation curve for a preset state
        // WITHOUT forcing its lane rows into the timeline. The rows are
        // revealed on demand by the Presets menu's "Show Preset Curve" toggle.
        void createPresetAutomationLanes(ArrangementClipModel* clip,
                                         const TimePitchState& state);
        // Preset-curve visibility (independent of normal automation lanes).
        bool isPresetCurveVisible(const ArrangementClipModel* clip);
        void setPresetCurveVisible(const ArrangementClipModel* clip, bool visible);
        void togglePresetCurveVisibility(const ArrangementClipModel* clip);
        // Pushes identity pitch/stretch to the engine clip so the preset curve
        // owns the effect only between its dots (no residual static shift).
        void applyPresetCurveEngineState(ArrangementClipModel* clip);
        void onClipOpenAutomationPanel(ArrangementClipModel* clip);

        // Multi-clip clipboard operations (keyboard path). These snapshot the
        // ENTIRE current selection so Copy/Cut/Duplicate never drop clips.
        void copySelectedClipsToClipboard(bool cut);
        void duplicateSelectedClips();

        // Automate This Clip window (one open at a time)
        std::unique_ptr<ClipAutomationWindow> m_automationPanelWindow;

        // Clipboard — holds every copied/cut clip (multi-clip capable).
        // Relative startTime/trackIndex offsets are preserved on paste.
        std::vector<ArrangementClipModel> m_clipboardClips;
        bool                              m_clipboardWasCut = false;

        // ── Clip drag / ghost ─────────────────────────────────────────────
        struct DragItem
        {
            juce::Uuid clipId;
            ArrangementClipModel clip;
            juce::Rectangle<int> bounds;
        };

        struct DragState
        {
            juce::Uuid  clipId;
            std::vector<juce::Uuid> clipIds;
            std::vector<DragItem> items;
            juce::Rectangle<int> anchorOriginalBounds;
            int         mouseOffsetX = 0;   // pixels from clip left edge to click point
            int         mouseOffsetY = 0;   // pixels from clip top edge to click point
            juce::Point<int> startMouse;    // mouse position at mouseDown (for threshold)
            juce::Point<int> currentMouse;  // current mouse position (local to view)
            bool        duplicate = false;
            bool        pending = false;    // armed after mouseDown, not yet dragging
            bool        active = false;     // true only after mouse moved past threshold
        };
        DragState m_drag;

        void startClipDrag   (ArrangementClipModel* clip, juce::Point<int> localPos, juce::ModifierKeys mods);
        void updateClipDrag  (juce::Point<int> localPos);
        void commitClipDrag  ();
        void cancelClipDrag  ();
        void drawGhost       (juce::Graphics& g);

        ArrangementDebugLogCore m_arrangementDebugLog;
        std::unique_ptr<ArrangementDebugPanelCore> m_arrangementDebugPanel;
        bool m_arrangementDebugPanelVisible = false;
        juce::Point<int> m_lastMousePos;
        juce::ModifierKeys m_lastMouseMods;
        juce::String m_lastMouseDownSource = "none";
        juce::Uuid m_lastMouseDownClipId;
        juce::String m_lastGhostLogSignature;
        SnapMode m_snapMode = SnapMode::Free;
        double m_tempoBpm = 120.0;
        int m_viewportScrollX = 0;
        int m_viewportScrollY = 0;
        bool m_inViewportScroll = false;
        bool m_middleMousePanActive = false;
        juce::Point<int> m_middleMousePanScreenPos;

        // ── Collapsed FolderBus state (hidden lane content) ───────────────
        std::unordered_set<DAW::TrackID> m_collapsedFolderIds;

        // ── Recursion guard for zoom/rebuild ──────────────────────────────
        bool m_rebuildInProgress = false;
        bool m_deferredRebuildRequested = false;
        class RebuildDeferTimer : public juce::Timer
        {
        public:
            explicit RebuildDeferTimer(ArrangementViewCore& owner) : m_owner(owner) {}
            void timerCallback() override
            {
                stopTimer();
                if (m_owner.m_deferredRebuildRequested)
                {
                    m_owner.m_deferredRebuildRequested = false;
                    m_owner.rebuildClipRenderersInternal();
                }
            }
        private:
            ArrangementViewCore& m_owner;
        };
        std::unique_ptr<RebuildDeferTimer> m_rebuildDeferTimer;

        // ── Waveform loading queue for batch imports ──────────────────────
        class WaveformLoadTimer : public juce::Timer
        {
        public:
            explicit WaveformLoadTimer(ArrangementViewCore& owner) : m_owner(owner) {}
            void timerCallback() override;
        private:
            ArrangementViewCore& m_owner;
        };
        std::unique_ptr<WaveformLoadTimer> m_waveformLoadTimer;
        std::queue<juce::Uuid> m_waveformLoadQueue;
        static constexpr int kWaveformLoadBatchSize = 2;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
