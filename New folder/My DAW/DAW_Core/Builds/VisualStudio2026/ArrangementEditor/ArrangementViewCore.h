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
#include "../../Source/Automation/AutomationLaneStoreCore.h"
#include "../../Source/AutomationCore/AutomationManagerCore.h"
#include "../../Source/Automation/AutomationParameterKeyCore.h"
#include "../../Source/RoutingCore/RoutingGraph.h"             
#include "../../Source/CommandCore/CommandManager.h"
#include <vector>                                              
#include <memory>                                              
#include <map>
#include <optional>
#include <queue>

// Forward-declare DAW engine types so we don't pull in heavy headers.
// Call setAudioEngineBridge() after construction to enable audio-engine sync on split.
namespace DAW
{
    class Clip;
    class ClipManager;
    class AudioFileManager;
    class MidiClip;
    class TrackManager;
    class AutomationManagerCore;
    class AutomationLaneCore;
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

        // ── Automation Update Timer ───────────────────────────────────────
        class AutomationUpdateTimer : public juce::Timer
        {
        public:
            explicit AutomationUpdateTimer(ArrangementViewCore& owner) : m_owner(owner) {}

            void timerCallback() override
            {
                // Invalidate automation curves and repaint
                for (auto& [trackId, container] : m_owner.m_automationContainers)
                {
                    container->invalidateAllCurvePaths();
                }
                m_owner.repaint();
            }

        private:
            ArrangementViewCore& m_owner;
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
                                  double                 engineSampleRate = 44100.0);

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
        std::function<void(double)> onPlayheadMoved;

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
        RubberBandSelectCore     m_rubberBand;
        ClipSplitIndicatorCore   m_splitIndicator;
        PianoRollButtonCore      m_pianoRollBtn; // NEW: Piano Roll button
        GhostOverlay             m_ghostOverlay;

        std::unique_ptr<ClipPropertiesWindowCore> m_propertiesWindow;

        // Clip renderers (one per clip)
        std::vector<std::unique_ptr<ClipRenderCore>> m_clipRenderers;

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
        bool isAutomationComponentAt(juce::Point<int> pos) const;
        bool isAutomationInteractionBlockingClipAt(juce::Point<int> pos) const;
        void updateClipAutomationDim();
        ClipRenderCore* findClipRendererAt(juce::Point<int> pos);
        ClipRenderCore* findClipRendererFor(const juce::Uuid& clipId) const;
        ArrangementClipModel* findClipAt(juce::Point<int> pos);
        void syncClipToEngine(const ArrangementClipModel& clip);
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
        juce::String formatUuidShort(const juce::Uuid& id) const;
        juce::String formatRect(const juce::Rectangle<int>& r) const;
        bool areAllDragItemBoundsIdentical() const;
        bool areAllGhostPositionsIdentical() const;
        juce::String buildArrangementDebugText() const;
        void appendDebugLog(const juce::String& line);
        void toggleArrangementDebugPanel();

        // Optional DAW engine bridge — null when running standalone / without audio engine
        DAW::ClipManager*      m_engineClipManager  = nullptr;
        DAW::AudioFileManager* m_engineAudioFiles   = nullptr;
        DAW::TrackManager*     m_engineTrackManager = nullptr;
        DAW::RoutingGraph*     m_engineRoutingGraph = nullptr;
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
        void onClipOpenAutomationPanel(ArrangementClipModel* clip);

        // Automate This Clip window (one open at a time)
        std::unique_ptr<ClipAutomationWindow> m_automationPanelWindow;

        // Clipboard — holds a single copied/cut clip
        std::optional<ArrangementClipModel> m_clipboardClip;
        bool                                m_clipboardWasCut = false;

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
        bool m_middleMousePanActive = false;
        juce::Point<int> m_middleMousePanScreenPos;

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
