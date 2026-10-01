// ===========================================================================
// ArrangementViewCore.cpp
// ===========================================================================
#include "ArrangementViewCore.h"
#include "ClipSplitCore.h"
#include "ClipGlueCore.h"
#include "ClipMuteCore.h"
#include "ClipEraserCore.h"
#include "ClipTimePitchBridgeCore.h"
#include "TimePitchPresetsCore.h"
#include "TimePitchUndoActions.h"
#include "../../../Source/PluginHostCore/ClipRegionPluginCore.h"
#include <set>
#include "../Source/ClipCore/Clip.h"
#include "../Source/MidiCore/MidiClip.h"
#include "../Source/AudioEngineCore/AudioFileManager.h"
#include "../Source/TrackCore/Track.h"
#include "../Source/UICore/CursorThemeCore.h"
#include <algorithm>
#include <functional>
#include <limits>

namespace ArrangementEditor
{
    namespace Col
    {
        // APEX signal-core tokens (see ThemeCore::ApexTokens)
        constexpr uint32_t bg = 0xFF05070C;          // deepestA — creative space
        constexpr uint32_t trackBg = 0xFF0A0D15;     // panelA
        constexpr uint32_t trackLine = 0xFF1A2233;   // borderSoftA
        constexpr uint32_t gridBeat = 0xFF1A2233;    // beat grid (drawn at low alpha)
        constexpr uint32_t gridBar = 0xFF22283A;     // bar grid
        constexpr uint32_t folderBand = 0xFF070A10;  // folder grouping band
    }

    namespace
    {
        static int getAutomationLaneVisualHeight(int trackHeight) noexcept
        {
            return juce::jmax(24, trackHeight - 4);
        }

        static float valueToAutomationValueForDisplay(const juce::String& parameterId, float normalised) noexcept
        {
            normalised = juce::jlimit(0.0f, 1.0f, normalised);

            if (parameterId == DAW::AutomationLaneCore::trackPanParameterId)
                return normalised * 2.0f - 1.0f;

            if (parameterId.startsWith("clip.") && parameterId.endsWith(DAW::AutomationLaneCore::clipPitchSuffix))
                return normalised * 72.0f - 36.0f;

            if (parameterId.startsWith("clip.") && parameterId.endsWith(DAW::AutomationLaneCore::clipStretchSuffix))
                return 0.1f + normalised * 3.9f;

            if (parameterId.startsWith("plugin.") || parameterId.startsWith("instrument.") || parameterId == DAW::AutomationLaneCore::trackTapeStopParameterId)
                return normalised;

            return normalised * 2.0f;
        }

        static bool isApexAutomationKeyForTrack(const juce::String& trackId, const juce::String& parameterId)
        {
            return parameterId.startsWith("track." + trackId + ".")
                || parameterId.startsWith("plugin." + trackId + ".");
        }

        static apex::automation::ParameterID apexIdForVisibleTarget(const juce::String& trackId, const juce::String& parameterId)
        {
            auto& registry = apex::automation::AutomationParameterKeyRegistry::getInstance();

            if (isApexAutomationKeyForTrack(trackId, parameterId))
                return registry.findID(parameterId);

            if (parameterId == DAW::AutomationLaneCore::trackVolumeParameterId)
                return registry.findID(apex::automation::AutomationParameterKeyRegistry::trackVolumeKey(trackId));

            if (parameterId == DAW::AutomationLaneCore::trackPanParameterId)
                return registry.findID(apex::automation::AutomationParameterKeyRegistry::trackPanKey(trackId));

            if (parameterId == "track.mute")
                return registry.findID(apex::automation::AutomationParameterKeyRegistry::trackMuteKey(trackId));

            if (parameterId == "track.solo")
                return registry.findID(apex::automation::AutomationParameterKeyRegistry::trackSoloKey(trackId));

            return apex::automation::kInvalidParameterID;
        }

        static void addOrReplaceApexPoint(const juce::String& parameterKey, double timeSeconds, float normalisedValue, double bpm)
        {
            if (parameterKey.isEmpty())
                return;

            auto& registry = apex::automation::AutomationParameterKeyRegistry::getInstance();
            const auto id = registry.getOrCreateID(parameterKey);
            auto& lane = apex::automation::AutomationLaneStore::getInstance().getOrCreateLane(id);
            auto snap = lane.getSnapshot();
            apex::automation::AutomationLane::PointVector next = snap ? *snap : apex::automation::AutomationLane::PointVector{};
            // Convert seconds to PPQ beats with the real project tempo. The
            // previous hardcoded 2.0 beats/second (120 BPM) placed points at
            // wrong PPQ positions at any other tempo, so APEX playback of
            // these lanes fired at the wrong time.
            const double ppq = juce::jmax(0.0, timeSeconds) * (juce::jmax(1.0, bpm) / 60.0);
            const float value = juce::jlimit(0.0f, 1.0f, normalisedValue);

            for (auto& point : next)
            {
                if (std::abs(point.timePPQ - ppq) < 1.0e-6)
                {
                    point.normalizedValue = value;
                    lane.replacePoints(std::move(next));
                    return;
                }
            }

            next.push_back({ ppq, value, apex::automation::CurveType::Linear, 0.0f });
            lane.replacePoints(std::move(next));
        }

        static float normaliseAutomationValueForDisplay(const juce::String& parameterId, float value) noexcept
        {
            if (parameterId == DAW::AutomationLaneCore::trackPanParameterId)
                return juce::jlimit(0.0f, 1.0f, (value + 1.0f) * 0.5f);

            if (parameterId == DAW::AutomationLaneCore::trackTapeStopParameterId)
                return juce::jlimit(0.0f, 1.0f, value);

            if (parameterId.startsWith("clip.") && parameterId.endsWith(DAW::AutomationLaneCore::clipPitchSuffix))
                return juce::jlimit(0.0f, 1.0f, (value + 36.0f) / 72.0f);

            if (parameterId.startsWith("clip.") && parameterId.endsWith(DAW::AutomationLaneCore::clipStretchSuffix))
                return juce::jlimit(0.0f, 1.0f, (value - 0.1f) / 3.9f);

            if (parameterId.startsWith("plugin.") || parameterId.startsWith("instrument."))
                return juce::jlimit(0.0f, 1.0f, value);

            return juce::jlimit(0.0f, 1.0f, value * 0.5f);
        }

        static juce::Colour getAutomationLaneColour(const juce::String& parameterId,
                                                    juce::Colour fallback) noexcept
        {
            if (parameterId == DAW::AutomationLaneCore::trackVolumeParameterId)
                return juce::Colour(0xFFFFD84D);
            if (parameterId == DAW::AutomationLaneCore::trackPanParameterId)
                return juce::Colour(0xFF62D8FF);
            if (parameterId == "track.mute")
                return juce::Colour(0xFFFF7AB8);
            if (parameterId == "track.solo")
                return juce::Colour(0xFF7DFF9B);
            if (parameterId == DAW::AutomationLaneCore::trackTapeStopParameterId)
                return juce::Colour(0xFFC08BFF);
            if (parameterId.startsWith("plugin."))
                return juce::Colour(0xFF4DFFD8);
            if (parameterId.startsWith("instrument."))
                return juce::Colour(0xFFFF9CF2);
            if (parameterId.startsWith("clip."))
                return juce::Colour(0xFFFFB15A);

            return fallback.brighter(0.25f);
        }
    }

    ArrangementViewCore::ArrangementViewCore()
        : m_toolbar(m_toolState)
        , m_ruler(m_zoom)
        , m_pianoRollBtn(PianoRollButtonCore::Style::ToolbarButton)
        , m_ghostOverlay(*this)
    {
        // Initialize timers for deferred operations
        m_rebuildDeferTimer = std::make_unique<RebuildDeferTimer>(*this);
        m_waveformLoadTimer = std::make_unique<WaveformLoadTimer>(*this);

        addAndMakeVisible(m_headerClipGuard);
        addAndMakeVisible(m_toolbar);
        addAndMakeVisible(m_ruler);
        addAndMakeVisible(m_rubberBand);
        addAndMakeVisible(m_splitIndicator);
        addAndMakeVisible(m_pianoRollBtn);
        addChildComponent(m_ghostOverlay);

        m_arrangementDebugPanel = std::make_unique<ArrangementDebugPanelCore>();
        m_arrangementDebugPanel->onRequestText = [this]() { return buildArrangementDebugText(); };
        m_arrangementDebugPanel->onCopy = [this]() { m_arrangementDebugLog.copyToClipboard(); };
        m_arrangementDebugPanel->onClear = [this]() { m_arrangementDebugLog.clear(); };
        addChildComponent(*m_arrangementDebugPanel);

        // Register as tool state listener so cursor + repaint stay in sync
        m_toolState.addListener(this);

        // Listen to the global undo/redo stack so the timeline refreshes
        // immediately after Ctrl+Z / Ctrl+Y — no frame of latency.
        DAW::CommandManager::getInstance().addListener(this);

        DBG("[ArrangementView] Created with shared ToolState");

        // Create the clip properties floating window
        m_propertiesWindow = std::make_unique<ClipPropertiesWindowCore>();
        m_propertiesWindow->setEngineSampleRate (m_engineSampleRate);

        m_ruler.onPlayheadMoved = [this](double timeSeconds)
        {
            setPlayheadPosition(timeSeconds);
            if (onPlayheadMoved)
                onPlayheadMoved(timeSeconds);
        };

        m_toolbar.setSelectedSnapModeIndex(getSnapModeIndex());
        m_toolbar.onSnapModeSelected = [this](int index)
        {
            switch (index)
            {
            case 1:  setSnapMode(SnapMode::ThirtySecond); break;
            case 2:  setSnapMode(SnapMode::Sixteenth); break;
            case 3:  setSnapMode(SnapMode::Eighth); break;
            case 4:  setSnapMode(SnapMode::Quarter); break;
            case 5:  setSnapMode(SnapMode::Half); break;
            case 6:  setSnapMode(SnapMode::Bar); break;
            case 7:  setSnapMode(SnapMode::Bar2); break;
            case 8:  setSnapMode(SnapMode::Bar4); break;
            default: setSnapMode(SnapMode::Free); break;
            }
        };

        // Visual-only changes (gain, fade, mute) — repaint only, no peak regen
        m_propertiesWindow->onModelChanged = [this](ArrangementClipModel& m)
        {
            m_clipState.updateClip(m);

            // Bump visual version so renderer knows to repaint
            m.bumpWaveformVisualVersion();

            // Find the renderer for this specific clip and force immediate repaint
            // The renderer already has a valid pointer to m (the vector element),
            // so the updated gain/fade/pitch values will be read in paint().
            auto* renderer = findClipRendererFor(m.id);
            if (renderer)
            {
                // Force immediate synchronous repaint to show changes in real-time
                renderer->forceUpdate();
            }

            // Also trigger a full view repaint to ensure parent updates
            repaint();
        };

        // Processed waveform invalidation (pitch, stretch, mode changes)
        m_propertiesWindow->onProcessedWaveformInvalidated = [this](ArrangementClipModel& m)
        {
            DBG("[ArrangementView] onProcessedWaveformInvalidated callback fired for clip: " << m.clipName);
            DBG("[ArrangementView]   pitch=" << m.timePitch.pitchSemitones << " stretch=" << m.timePitch.stretchRatio << " mode=" << (int)m.timePitch.mode);

            m_clipState.updateClip(m);

            // Bump processed version to invalidate cache
            m.bumpProcessedWaveformVersion();

            // CRITICAL: Sync ArrangementClipModel → DAW::AudioClip so audio engine sees pitch/stretch/mode changes
            syncClipToEngine(m);

            // Rebuild renderers to recalculate visualLength() and request new peaks
            rebuildClipRenderers();

            // Mark renderer as rendering if needed and force immediate repaint
            auto* renderer = findClipRendererFor(m.id);
            if (renderer)
            {
                renderer->setRenderingStretch(m.needsProcessedWaveform());
                // Force immediate repaint to show updated waveform
                renderer->repaint();
                repaint();
                if (auto* parent = getParentComponent())
                    parent->repaint();
                DBG("[ArrangementView]   Forced immediate repaint for processed waveform");
            }
        };

        // Wire Piano Roll button
        m_pianoRollBtn.onClick = [this]() {
            DBG("Piano Roll button clicked");
            if (onOpenPianoRollRequested)
                onOpenPianoRollRequested();
        };

        // Wire clip state callbacks
        m_clipState.onClipAdded = [this](const juce::Uuid&) {
            rebuildClipRenderers();
        };

        m_clipState.onClipRemoved = [this](const juce::Uuid& id) {
            if (m_propertiesWindow)
                m_propertiesWindow->closeIfShowingClip(id);
            m_selection.deselectClip(id);
            rebuildClipRenderers();
        };

        m_clipState.onClipChanged = [this](const juce::Uuid&) {
            repaint();
        };

        // Wire selection callbacks
        m_selection.onSelectionChanged = [this]() {
            const auto current = m_selection.getSelectedIds();
            if (current == m_lastVisualSelection)
                return;

            std::set<juce::Uuid> changed;
            std::set_symmetric_difference(m_lastVisualSelection.begin(),
                                          m_lastVisualSelection.end(),
                                          current.begin(), current.end(),
                                          std::inserter(changed, changed.end()));
            for (const auto& id : changed)
                if (auto* renderer = findClipRendererFor(id))
                    renderer->setSelected(current.count(id) > 0);

            m_lastVisualSelection = current;

            std::set<juce::Uuid> coreIds(m_selection.getSelectedIds().begin(), m_selection.getSelectedIds().end());
            const auto visualIds = getVisualSelectedClipIds();
            std::set<juce::Uuid> visualSet(visualIds.begin(), visualIds.end());
            if (coreIds != visualSet)
            {
                appendDebugLog("[WARNING selectionMismatch] core=" + juce::String(coreIds.size())
                    + " visual=" + juce::String(visualSet.size()));
            }

            repaint();
        };

        // Wire zoom callbacks
        m_zoom.onZoomChanged = [this]() {
            rebuildClipRenderers();
            repaint();
        };

        // Wire rubber band
        m_rubberBand.onSelectionChanged = [this](juce::Rectangle<int> bounds)
        {
            updateRubberBandSelection(bounds);
        };
        m_rubberBand.onSelectionComplete = [this](juce::Rectangle<int> bounds) {
            updateRubberBandSelection(bounds);
            m_marqueeHitGeometry.clear();
            appendDebugLog("[rubberBandSelection] bounds=" + formatRect(bounds)
                + " selected=" + juce::String(m_selection.getSelectionCount()));
        };

        setSize(1200, 800);
        setWantsKeyboardFocus(true);
        setMouseCursorForActiveTool();
        appendDebugLog("[ArrangementDebug] initialized");
    }

    ArrangementViewCore::~ArrangementViewCore()
    {
        DBG("[CRASH TRACE] action=arrangementview_destroyed");
        if (m_rebuildDeferTimer)
            m_rebuildDeferTimer->stopTimer();
        if (m_waveformLoadTimer)
            m_waveformLoadTimer->stopTimer();
        cancelClipDrag();
        m_rubberBand.setVisible(false);
        m_propertiesWindow.reset();
        m_bounceCore.onBounceComplete = nullptr;
        m_pianoRollBtn.onClick = nullptr;
        m_rubberBand.onSelectionComplete = nullptr;
        m_selection.onSelectionChanged = nullptr;
        m_clipState.onClipAdded = nullptr;
        m_clipState.onClipRemoved = nullptr;
        m_clipState.onClipChanged = nullptr;
        m_clipState.onStateChanged = nullptr;
        m_zoom.onZoomChanged = nullptr;
        m_toolState.removeListener(this);
        DAW::CommandManager::getInstance().removeListener(this);
    }

    void ArrangementViewCore::undoHistoryChanged()
    {
        // Force an immediate, synchronous refresh of the timeline so undo/redo
        // is visible in real-time instead of waiting for the next paint tick.
        rebuildClipRenderers();
        for (auto& [trackId, container] : m_automationContainers)
            if (container != nullptr)
                container->invalidateAllCurvePaths();
        repaint();
    }

    // -----------------------------------------------------------------------
    // Audio engine bridge
    // -----------------------------------------------------------------------

    void ArrangementViewCore::setAudioEngineBridge(DAW::ClipManager*      clipManager,
                                                   DAW::AudioFileManager* audioFileManager,
                                                   DAW::TrackManager*     trackManager,
                                                   double                 engineSampleRate,
                                                   DAW::ClipRegionPluginCore* clipRegionPlugins,
                                                   juce::AudioPluginFormatManager* pluginFormatManager)
    {
        m_engineClipManager   = clipManager;
        m_engineAudioFiles    = audioFileManager;
        m_engineTrackManager  = trackManager;
        m_engineSampleRate    = juce::jmax(1.0, engineSampleRate);
        m_clipRegionPlugins   = clipRegionPlugins;
        m_pluginFormatManager = pluginFormatManager;

        if (m_engineClipManager == nullptr || m_engineAudioFiles == nullptr)
            return;

        // ---- onClipAdded: create a matching engine clip and cache the ID ----
        m_clipState.onClipAdded = [this](const juce::Uuid& uuid)
        {
            if (m_suppressEngineClipCreation)
                return;

            rebuildClipRenderers();

            if (m_engineClipManager == nullptr || m_engineAudioFiles == nullptr)
                return;

            auto* aclip = m_clipState.findClip(uuid);
            if (!aclip || aclip->sourcePath.empty())
                return;

            const juce::File srcFile (aclip->sourcePath);
            juce::String clipNameStr = aclip->clipName.empty()
                                           ? juce::String("Clip")
                                           : juce::String(aclip->clipName);
            juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);
            auto* ec = m_engineClipManager->createAudioClip(clipNameStr, srcFile);
            if (!ec) return;

            // Load audio
            if (!m_engineAudioFiles->hasAudio(ec->getID()))
            {
                auto res = m_engineAudioFiles->loadForClip(ec->getID(), srcFile);
                if (res.success)
                {
                    auto tl = (DAW::SamplePosition)std::llround(
                        (double)res.numSamples * m_engineSampleRate / (res.sampleRate > 0.0 ? res.sampleRate : 44100.0));
                    ec->setLength(juce::jmax((DAW::SamplePosition)1, tl));

                    if (aclip->sourceEndSample <= aclip->sourceStartSample)
                    {
                        ec->setSourceStartSample(0);
                        ec->setSourceEndSample((int64_t)res.numSamples);
                    }

                    // Cache the file's true sample count on the model so the
                    // clip renderer can cap right-edge trim at the real audio
                    // end (no accidental stretch when extending past source).
                    if (auto* mutClip = m_clipState.findClip(uuid))
                    {
                        mutClip->sourceTotalSamples = (int64_t)res.numSamples;
                        mutClip->sourceSampleRate = res.sampleRate;
                    }
                }
            }

            const double srcRate = m_engineAudioFiles->getSourceSampleRate(ec->getID());
            // Arrangement time is expressed in seconds.  Timeline positions
            // and lengths belong to the engine sample-rate domain; only the
            // source offset belongs to the decoded file's sample-rate domain.
            ec->setStartPosition((DAW::SamplePosition)std::llround(aclip->startTime * m_engineSampleRate));
            ec->setLength(juce::jmax((DAW::SamplePosition)1,
                          (DAW::SamplePosition)std::llround(aclip->length * m_engineSampleRate)));
            ec->setSourceOffset(juce::jmax((DAW::SamplePosition)0,
                                (DAW::SamplePosition)std::llround(aclip->sourceOffset * srcRate)));
            if (aclip->sourceEndSample > aclip->sourceStartSample)
            {
                ec->setSourceStartSample(aclip->sourceStartSample);
                ec->setSourceEndSample(aclip->sourceEndSample);
            }
            ec->setMuted(aclip->muted);

            // Arrangement indices include the master strip; engine indices do
            // not.  Keep the conversion identical to syncClipToEngine().
            if (m_engineTrackManager != nullptr && aclip->trackIndex < m_engineTrackManager->getNumTracks())
            {
                const int masterOffset = m_engineTrackManager->hasMasterTrack() ? 1 : 0;
                const int engineTrackIndex = aclip->trackIndex - masterOffset;
                if (engineTrackIndex >= 0
                    && engineTrackIndex < m_engineTrackManager->getNumTracks())
                if (auto* t = m_engineTrackManager->getTrack(engineTrackIndex))
                    ec->setTrackID(t->getID());
            }

            m_uuidToEngineId[uuid] = ec->getID();

            // Push the complete model state after the ID mapping exists.  In
            // particular, do not replace the user's pitch/time mode with the
            // legacy Resample/1.0 defaults during import or project restore.
            {
                juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);
                syncClipToEngine(*aclip);
            }
            refreshAutoCrossfadesForAllTracks();

            DBG("[EngineSync] onClipAdded uuid=" << uuid.toString()
                << " engineId=" << ec->getID()
                << " src=" << aclip->sourcePath
                << " stretchRatio=" << aclip->timePitch.stretchRatio
                << " mode=" << static_cast<int>(aclip->timePitch.mode));
        };

        // ---- onClipRemoved: delete matching engine clip ----
        m_clipState.onClipRemoved = [this](const juce::Uuid& uuid)
        {
            if (m_propertiesWindow)
                m_propertiesWindow->closeIfShowingClip(uuid);
            m_selection.deselectClip(uuid);
            rebuildClipRenderers();

            if (m_engineClipManager == nullptr) return;

            auto it = m_uuidToEngineId.find(uuid);
            if (it != m_uuidToEngineId.end())
            {
                const auto engineId = it->second;
                DBG("[EngineSync] onClipRemoved uuid=" << uuid.toString()
                    << " engineId=" << engineId);

                // deleteClip() can synchronously notify listeners that re-enter
                // clipRemoved/onClipRemoved and mutate this map. Erase by key first
                // and never keep/use an iterator across that callback path.
                m_uuidToEngineId.erase(uuid);
                m_engineClipManager->deleteClip(engineId);
            }

            refreshAutoCrossfadesForAllTracks();
        };

        DBG("[EngineSync] Bridge wired. sampleRate=" << m_engineSampleRate);

        // ---- onClipChanged: sync pitch/stretch/gain into the engine AudioClip ----
        m_clipState.onClipChanged = [this](const juce::Uuid& uuid)
        {
            repaint();

            if (m_syncingEngineToArrangement)
                return;

            if (m_engineClipManager == nullptr) return;
            const auto it = m_uuidToEngineId.find(uuid);
            if (it == m_uuidToEngineId.end()) return;

            const auto* aclip = m_clipState.findClip(uuid);
            if (!aclip) return;

            auto* ec = dynamic_cast<DAW::AudioClip*>(m_engineClipManager->getClip(it->second));
            if (!ec) return;

            // The model is authoritative for clip time/pitch state.  The
            // bridge writes pitch and stretch independently, so PitchOnly does
            // not become a playback-rate edit and Stretch does not invent a
            // pitch change.
            {
                juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);
                syncClipToEngine(*aclip);
            }

            DBG("[EngineSync] onClipChanged uuid=" << uuid.toString()
                << " pitch=" << aclip->timePitch.pitchSemitones
                << " stretch=" << aclip->timePitch.stretchRatio
                << " mode=" << static_cast<int>(aclip->timePitch.mode));
        };
    }

    // ── Playhead dirty-rect helpers for F1 bounded repaint ────────────
    namespace {
        // Body playhead area: 1px line + 5px glow (x-2..x+2) + 1px AA margin.
        // Covers the timeline body below the ruler only.
        constexpr int kPlayheadBodyHalfWidth = 3;

        // Ruler playhead area: 1px line + 10px triangle (x-5..x+5) + 1px AA margin.
        // Covers the entire ruler height.
        constexpr int kPlayheadRulerHalfWidth = 6;

        /** Returns a dirty rectangle for the body playhead at |playheadX|
         *  (core-local X coordinate). The rect is clamped to the body area
         *  (top … bottom, componentWidth × componentHeight).
         *  Returns an empty rect when the playhead is entirely outside bounds. */
        juce::Rectangle<int> playheadBodyDirtyRect(
            int playheadX, int top, int bottom,
            int componentWidth, int componentHeight) noexcept
        {
            const auto body = juce::Rectangle<int>(
                0, top, componentWidth, componentHeight - top);
            const auto glow = juce::Rectangle<int>(
                playheadX - kPlayheadBodyHalfWidth, top,
                kPlayheadBodyHalfWidth * 2 + 1,
                juce::jmax(0, bottom - top));
            return glow.getIntersection(body);
        }

        /** Returns a dirty rectangle for the ruler playhead at |playheadX|
         *  (ruler-local X coordinate). Clamped to the ruler's local bounds.
         *  Returns an empty rect when the playhead is entirely outside bounds. */
        juce::Rectangle<int> playheadRulerDirtyRect(
            int playheadX, int rulerWidth, int rulerHeight) noexcept
        {
            const auto bounds = juce::Rectangle<int>(0, 0, rulerWidth, rulerHeight);
            const auto glow = juce::Rectangle<int>(
                playheadX - kPlayheadRulerHalfWidth, 0,
                kPlayheadRulerHalfWidth * 2 + 1, rulerHeight);
            return glow.getIntersection(bounds);
        }

        /** Issues one or two partial-repaint rects for old and new playhead
         *  positions.  When the two rects are the same or overlapping, JUCE
         *  coalesces them into a single repaint region. */
        void repaintPlayheadPositions(
            juce::Component& comp,
            int oldX, int newX,
            int top, int bottom,
            int compWidth, int compHeight) noexcept
        {
            if (oldX == newX)
            {
                auto r = playheadBodyDirtyRect(newX, top, bottom, compWidth, compHeight);
                if (!r.isEmpty()) comp.repaint(r);
                return;
            }
            auto oldR = playheadBodyDirtyRect(oldX, top, bottom, compWidth, compHeight);
            if (!oldR.isEmpty()) comp.repaint(oldR);
            auto newR = playheadBodyDirtyRect(newX, top, bottom, compWidth, compHeight);
            if (!newR.isEmpty()) comp.repaint(newR);
        }

        /** Records partial-repaint dirty-area metrics for old and new positions. */
        void recordPlayheadDirtyAreaMetrics(
            int oldX, int newX,
            int top, int bottom,
            int compWidth, int compHeight) noexcept
        {
            auto recordOne = [&](int x) {
                auto r = playheadBodyDirtyRect(x, top, bottom, compWidth, compHeight);
                if (r.isEmpty()) return;
                TimelinePaintMetrics::requestedPartialRepaints.fetch_add(1);
                auto area = static_cast<uint64_t>(r.getWidth())
                          * static_cast<uint64_t>(r.getHeight());
                TimelinePaintMetrics::requestedDirtyAreaPixels.fetch_add(
                    static_cast<int64_t>(area));
                TimelinePaintMetrics::requestedDirtyAreaPixelsRing.push(area);
            };
            if (oldX == newX) {
                recordOne(newX);
            } else {
                recordOne(oldX);
                recordOne(newX);
            }
        }
    } // anonymous namespace

    void ArrangementViewCore::setPlayheadPosition(double timeSeconds)
    {
        timeSeconds = juce::jmax(0.0, timeSeconds);
        if (std::abs(m_playheadTimeSeconds - timeSeconds) < 0.0001)
            return;

        const double oldTime = m_playheadTimeSeconds;
        const int oldX = timeToScreenX(oldTime);
        m_playheadTimeSeconds = timeSeconds;
        const int newX = timeToScreenX(timeSeconds);

        // Delegate to ruler FIRST — ruler handles its own partial repaint
        m_ruler.setPlayheadPosition(timeSeconds);

        // Body playhead area: below the ruler
        const int top = m_ruler.getBottom();
        const int bottom = getHeight();
        const int compW = getWidth();
        const int compH = getHeight();

        if (TimelinePaintMetrics::active.load(std::memory_order_relaxed))
        {
            recordPlayheadDirtyAreaMetrics(
                oldX, newX, top, bottom, compW, compH);
        }

        repaintPlayheadPositions(
            *this, oldX, newX, top, bottom, compW, compH);
    }

    void ArrangementViewCore::addOrUpdateEngineClipModel(const ArrangementClipModel& model, const juce::String& engineClipId)
    {
        if (engineClipId.isEmpty())
            return;

        juce::ScopedValueSetter<bool> syncGuard(m_syncingEngineToArrangement, true);

        for (const auto& mapped : m_uuidToEngineId)
        {
            if (mapped.second == engineClipId)
            {
                auto updated = model;
                updated.id = mapped.first;
                m_suppressEngineClipCreation = true;
                m_clipState.updateClip(updated);
                m_suppressEngineClipCreation = false;
                if (m_engineClipBatchDepth == 0)
                {
                    upsertClipRenderer(updated.id);
                    repaint();
                    refreshAutoCrossfadesForAllTracks();
                }
                return;
            }
        }

        m_suppressEngineClipCreation = true;
        m_clipState.addClip(model);
        m_suppressEngineClipCreation = false;
        m_uuidToEngineId[model.id] = engineClipId;
        if (m_engineClipBatchDepth == 0)
        {
            upsertClipRenderer(model.id);
            repaint();
        }
        if (m_engineClipBatchDepth == 0)
            refreshAutoCrossfadesForAllTracks();
    }

    void ArrangementViewCore::beginEngineClipBatch(bool clearExisting)
    {
        if (m_engineClipBatchDepth++ == 0)
        {
            m_clipState.beginBatch();
            if (clearExisting)
            {
                m_clipState.clear();
                m_uuidToEngineId.clear();
                m_selection.deselectAll();
            }
        }
    }

    void ArrangementViewCore::endEngineClipBatch()
    {
        if (m_engineClipBatchDepth <= 0)
            return;
        if (--m_engineClipBatchDepth == 0)
        {
            m_clipState.endBatch();
            refreshAutoCrossfadesForAllTracks();
            rebuildClipRenderers();
            repaint();
        }
    }

    void ArrangementViewCore::paint(juce::Graphics& g)
    {
        // Keep the ruler in sync with the real viewport position. The scroll
        // callback path is not wired, so the ruler's scroll offset went stale
        // and its bar numbers drifted away from the clip positions while
        // scrolling.
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            const int viewX = viewport->getViewPositionX();
            const int viewY = viewport->getViewPositionY();
            if (viewX != m_viewportScrollX || viewY != m_viewportScrollY)
                setViewportScrollOffset(viewX, viewY);
        }
        const bool metricsActive = TimelinePaintMetrics::active.load(std::memory_order_relaxed);

        if (metricsActive)
            TimelinePaintMetrics::paintCount.fetch_add(1);
        TimelinePaintMetrics::ScopedTimer paintTimer(TimelinePaintMetrics::paintDurationNs);

        // Record the area actually delivered by the Graphics clip region.
        // This is the union of coalesced repaint regions after JUCE's
        // repaint coalescing and native-window clipping.
        if (metricsActive)
        {
            auto clipArea = g.getClipBounds();
            auto safeW = static_cast<uint64_t>(std::max(0, clipArea.getWidth()));
            auto safeH = static_cast<uint64_t>(std::max(0, clipArea.getHeight()));
            auto area = safeW * safeH;
            TimelinePaintMetrics::actualPaintClipAreaPixels.push(area);
            if (clipArea != getLocalBounds())
                TimelinePaintMetrics::actualPartialPaintInvocations.fetch_add(1);
            else
                TimelinePaintMetrics::actualFullPaintInvocations.fetch_add(1);
        }

        g.fillAll(fromU32(Col::bg));

        // Draw track lanes
        int trackH = (int)m_zoom.getTrackHeightPx();
        const int trackCount = getArrangementTrackCount();
        const int contentTop = arrangementContentTop();
        const auto visibleBounds = getViewportVisibleBounds();
        const int firstVisibleTrack = juce::jlimit(0, trackCount,
            (int)std::floor((double)(visibleBounds.getY() - contentTop) / (double)juce::jmax(1, trackH)));
        const int lastVisibleTrack = juce::jlimit(firstVisibleTrack, trackCount,
            (int)std::ceil((double)(visibleBounds.getBottom() - contentTop) / (double)juce::jmax(1, trackH)));
        int y = contentTop + firstVisibleTrack * trackH;
        const int visibleTrackCount = juce::jmax(0, lastVisibleTrack - firstVisibleTrack);

        // These counters are only incremented when metrics are active,
        // but trackIteration is a proxy for "how much work was done"
        // and is cheap regardless. Guarding it avoids ~1-2ns fetch_add
        // per paint when benchmarking is inactive.
        if (metricsActive)
        {
            TimelinePaintMetrics::tracksIterated.fetch_add(static_cast<uint64_t>(visibleTrackCount));
            TimelinePaintMetrics::tracksCulled.fetch_add(
                static_cast<uint64_t>(juce::jmax(0, trackCount - visibleTrackCount)));
        }

        for (int i = firstVisibleTrack; i < lastVisibleTrack; ++i)
        {
            g.setColour(fromU32(i % 2 == 0 ? Col::trackBg : Col::bg));
            g.fillRect(0, y, getWidth(), trackH);

                g.setColour(fromU32(Col::trackLine));
                    g.drawHorizontalLine(y + trackH - 1, 0.f, (float)getWidth());

                    y += trackH;
                }

        // ── Beat/bar grid across lanes ──────────────────────────────────────
        // Restrained cool tones: bars read above beats, grid never competes
        // with clips. LOD policy shared with ArrangementRulerCore via
        // GridLodPolicy.
        {
            const double beatDuration = 60.0 / juce::jmax(1.0, m_tempoBpm);
            const double barDuration  = beatDuration * 4.0;
            const float  gridTop      = (float)juce::jmax(contentTop, visibleBounds.getY());
            const float  gridBottom   = (float)juce::jmin(getHeight(), visibleBounds.getBottom());

            // The grid is drawn in CONTENT coordinates — this component IS the
            // scrolled content, so it must NOT subtract the scroll offset.
            // Subtracting it shifted every bar line away from the clips and
            // the ruler as soon as the view scrolled.
            auto timeToLaneX = [this](double t) { return (int) m_zoom.timeToX(t); };
            const double viewStart = m_zoom.xToTime((double) m_viewportScrollX);
            const double viewEnd   = m_zoom.xToTime((double) (m_viewportScrollX + getWidth()));
            const int    gridVisibleX0 = (int) visibleBounds.getX();
            const int    gridVisibleX1 = (int) visibleBounds.getRight();

            const double pxPerBeat = beatDuration * m_zoom.getPixelsPerSecond();
            const double pxPerBar  = barDuration  * m_zoom.getPixelsPerSecond();
            const int    barStart  = (int) (viewStart / barDuration);
            const int    barEnd    = (int) (viewEnd   / barDuration) + 1;
            const int    barStep   = GridLodPolicy::barStep(pxPerBar);
            const bool   noBeats   = GridLodPolicy::suppressBeats(pxPerBeat);

            for (int bar = barStart; bar <= barEnd; ++bar)
            {
                // Skip bars at extreme zoom-out to reduce GPU draw calls
                if (barStep > 1 && (bar % barStep) != 0)
                    continue;

                const double barTime = bar * barDuration;
                const int x = timeToLaneX(barTime);
                if (x >= gridVisibleX0 && x <= gridVisibleX1)
                {
                    g.setColour(fromU32(Col::gridBar).withAlpha(0.55f));
                    g.drawVerticalLine(x, gridTop, gridBottom);
                    if (metricsActive)
                        TimelinePaintMetrics::gridBarsGenerated.fetch_add(1);
                }

                // Beat lines — suppressed when beats are too dense
                if (noBeats)
                    continue;

                g.setColour(fromU32(Col::gridBeat).withAlpha(0.22f));
                for (int beat = 1; beat < 4; ++beat)
                {
                    const int beatX = timeToLaneX(barTime + beat * beatDuration);
                    if (beatX >= gridVisibleX0 && beatX <= gridVisibleX1)
                    {
                        g.drawVerticalLine(beatX, gridTop, gridBottom);
                        if (metricsActive)
                            TimelinePaintMetrics::gridBeatsGenerated.fetch_add(1);
                    }
                }
            }
        }

        // ── Folder Track hierarchy — branch/continuity guides ───────────────
        // Visual only: subtle grouping band + inherited parent accent on the
        // lane's left edge. FolderBusCore/routing semantics are untouched.
        if (m_engineTrackManager != nullptr)
        {
            // Lane 0 is the MASTER lane when a master track exists; engine
            // tracks sit at lane = engineIndex + masterOffset (mirrors the
            // automation-lane mapping used elsewhere in this file).
            const int masterOffset = m_engineTrackManager->hasMasterTrack() ? 1 : 0;
            int laneY = contentTop + firstVisibleTrack * trackH;
            for (int i = firstVisibleTrack; i < lastVisibleTrack; ++i, laneY += trackH)
            {
                if (i < masterOffset)
                {
                    // MASTER lane identity — subtle magenta edge, coherent
                    // with the Phase 4 master header treatment.
                    g.setColour(juce::Colour(0xFFE91572).withAlpha(0.10f));
                    g.fillRect(0, laneY, 4, trackH);
                    g.setColour(juce::Colour(0xFFE91572).withAlpha(0.55f));
                    g.fillRect(0, laneY, 2, trackH);
                    continue;
                }

                auto* track = m_engineTrackManager->getTrack(i - masterOffset);
                if (track == nullptr || track->isMaster())
                    continue;

                // Collapsed folder: children lanes render as empty bands —
                // no identity, no grouping guides (content is hidden).
                if (isTrackHiddenByCollapsedFolder(track))
                    continue;

                // Resolve depth + nearest ancestor accent (bounded walk)
                int depth = 0;
                juce::Colour parentAccent = track->getColor();
                {
                    DAW::Track* cursor = track;
                    juce::String parentId = cursor->getParentTrackID();
                    while (parentId.isNotEmpty() && depth < 16)
                    {
                        auto* parent = m_engineTrackManager->getTrack(parentId);
                        if (parent == nullptr) break;
                        parentAccent = parent->getColor();
                        ++depth;
                        cursor = parent;
                        parentId = cursor->getParentTrackID();
                    }
                }

                const bool isFolder = (track->getRole() == DAW::TrackRole::FolderBus);

                if (isFolder)
                {
                    // Folder lane: subtle shared accent wash at the left edge —
                    // reads as "group", never as a boxed container.
                    g.setColour(track->getColor().withAlpha(0.10f));
                    g.fillRect(0, laneY, 4, trackH);
                    g.setColour(track->getColor().withAlpha(0.55f));
                    g.fillRect(0, laneY, 2, trackH);
                }
                else if (depth > 0)
                {
                    // Child lane: darker grouping band + branch guide in the
                    // parent's accent, stepping right with nesting depth.
                    const int bandW = juce::jmin(4 + (depth - 1) * 6, 22);
                    g.setColour(fromU32(Col::folderBand).withAlpha(0.60f));
                    g.fillRect(0, laneY, bandW, trackH);

                    for (int d = 0; d < depth; ++d)
                    {
                        g.setColour(parentAccent.withAlpha(d == depth - 1 ? 0.50f : 0.28f));
                        g.fillRect(2 + d * 6, laneY, 1, trackH);
                    }
                }
            }
        }

        // ── Lock button (horizontal clip lock) ──
        // Drawn in paintOverChildren(): the opaque HeaderClipGuard child
        // covers everything painted here, which is why this button was
        // invisible. See drawLockButton().
    }

    void ArrangementViewCore::drawLockButton(juce::Graphics& g)
    {
        if (m_lockBtnBounds.isEmpty())
            return;

        const auto rb = m_lockBtnBounds.toFloat();
        g.setColour(fromU32(m_lockHorizontal ? 0xFFFF1678 : (m_lockBtnHovered ? 0xFF1A2233 : 0xFF111522)));
        g.fillRoundedRectangle(rb, 4.f);
        g.setColour(fromU32(m_lockHorizontal ? 0xFFFF1678 : 0xFF22283A));
        g.drawRoundedRectangle(rb.reduced(0.5f), 4.f, 1.f);

        // Vector lock glyph — emoji fonts are not guaranteed to exist on
        // every Windows install, which left this button looking empty.
        const auto glyphCol = fromU32(m_lockHorizontal ? 0xFFFFFFFF : 0xFFA6ADBC);
        const float cx = rb.getCentreX();
        const float bodyW = 12.0f;
        const float bodyH = 9.0f;
        const juce::Rectangle<float> body(cx - bodyW * 0.5f,
                                          rb.getCentreY() - 0.5f,
                                          bodyW, bodyH);
        g.setColour(glyphCol);
        g.fillRoundedRectangle(body, 1.5f);

        juce::Path shackle;
        shackle.addCentredArc(cx, body.getY() + 0.5f, 3.8f, 4.2f, 0.0f,
                              juce::MathConstants<float>::pi,
                              juce::MathConstants<float>::twoPi, true);
        g.strokePath(shackle, juce::PathStrokeType(1.6f,
                       juce::PathStrokeType::curved,
                       juce::PathStrokeType::rounded));

        // ── Free View Mode button (eye glyph) ──
        if (! m_viewOnlyBtnBounds.isEmpty())
        {
            const auto vb = m_viewOnlyBtnBounds.toFloat();
            g.setColour(fromU32(m_viewOnlyMode ? 0xFF5BC8FF : (m_viewOnlyBtnHovered ? 0xFF1A2233 : 0xFF111522)));
            g.fillRoundedRectangle(vb, 4.f);
            g.setColour(fromU32(m_viewOnlyMode ? 0xFF5BC8FF : 0xFF22283A));
            g.drawRoundedRectangle(vb.reduced(0.5f), 4.f, 1.f);

            const auto eyeCol = fromU32(m_viewOnlyMode ? 0xFF0A0A0C : 0xFFA6ADBC);
            const float cx = vb.getCentreX();
            const float cy = vb.getCentreY();
            juce::Path eye;
            eye.startNewSubPath(cx - 8.0f, cy);
            eye.quadraticTo(cx, cy - 6.0f, cx + 8.0f, cy);
            eye.quadraticTo(cx, cy + 6.0f, cx - 8.0f, cy);
            g.setColour(eyeCol);
            g.strokePath(eye, juce::PathStrokeType(1.5f));
            g.fillEllipse(cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);

            if (m_viewOnlyMode)
            {
                // Universal "view only" mark: a slash through the eye, so the
                // state is unmistakable even if the colour alone is misread.
                g.drawLine(cx - 7.0f, cy + 6.0f, cx + 7.0f, cy - 6.0f, 1.8f);
            }
        }
    }

    void ArrangementViewCore::onEmptySpaceHold()
    {
        // Held still on empty space → arm the marquee (Logic-style
        // hold-to-select). A drag before the hold scrolls instead.
        if (m_viewOnlyMode || m_gestureMoved || ! m_marqueePending)
            return;

        m_holdFired = true;
        m_marqueePending = false;

        m_selection.deselectAll();

        m_marqueeHitGeometry.clear();
        m_marqueeHitGeometry.reserve(m_clipRenderers.size());
        for (const auto& renderer : m_clipRenderers)
            if (renderer != nullptr)
                m_marqueeHitGeometry.emplace_back(renderer->getClipId(), renderer->getBounds());

        m_rubberBand.startDrag(m_gestureDownPos);
        if (onEngineClipSelectionCleared)
            onEngineClipSelectionCleared();
    }

    bool ArrangementViewCore::applyTouchPanInertia()
    {
        auto* viewport = findParentComponentOfClass<juce::Viewport>();
        if (viewport == nullptr)
            return false;

        // Scroll/gesture LOD: every renderer paints only its background+header
        // while this runs, which is what makes the gesture smooth.
        ClipRenderCore::s_lightweightPaint.store(true, std::memory_order_relaxed);

        // ── Live two-finger gesture, sampled at 60 Hz ──
        // Driving the pan/pinch from this timer instead of the irregular mouse
        // event stream is what makes it feel like an Apple device: every frame
        // advances by the same amount, no bursts, no stalls.
        int touchCount = 0;
        juce::Point<float> firstTouch, secondTouch;
        // Capture the VALUE, not a pointer: getMouseSources() returns the array
        // by value, so a pointer into it dangles after the loop and reading the
        // down time from freed memory produced random stops and reversals
        // mid-swipe.
        juce::Time firstTouchDownTime;
        for (auto& source : juce::Desktop::getInstance().getMouseSources())
        {
            if (! source.isDragging()
                || source.getType() != juce::MouseInputSource::InputSourceType::touch)
                continue;
            if (touchCount == 0)
            {
                firstTouch         = source.getScreenPosition();
                firstTouchDownTime = source.getLastMouseDownTime();
            }
            else if (touchCount == 1)
            {
                secondTouch = source.getScreenPosition();
            }
            ++touchCount;
        }

        if (touchCount >= 2)
        {
            const auto firstLocal  = getLocalPoint(nullptr, firstTouch.toInt()).toFloat();
            const auto secondLocal = getLocalPoint(nullptr, secondTouch.toInt()).toFloat();

            // Axis-specific pinch: horizontal separation drives the time zoom,
            // vertical separation drives the track height — each independent,
            // in and out.
            const double sepX = std::abs((double) firstLocal.x - (double) secondLocal.x);
            const double sepY = std::abs((double) firstLocal.y - (double) secondLocal.y);

            if (m_lastPinchSepX > 1.0 && std::abs(sepX - m_lastPinchSepX) > 6.0)
            {
                const double ratio = juce::jlimit(0.85, 1.18, sepX / m_lastPinchSepX);
                m_zoom.setPixelsPerSecond(juce::jlimit(10.0, 4000.0,
                    m_zoom.getPixelsPerSecond() * ratio));
                repaint();
            }

            if (m_lastPinchSepY > 1.0 && std::abs(sepY - m_lastPinchSepY) > 6.0)
            {
                const double ratio = juce::jlimit(0.85, 1.18, sepY / m_lastPinchSepY);
                m_zoom.setTrackHeightPx(juce::jlimit(24.0, 400.0,
                    m_zoom.getTrackHeightPx() * ratio));
                repaint();
            }

            m_lastPinchSepX = sepX;
            m_lastPinchSepY = sepY;
            return true;
        }

        m_lastPinchSepX = 0.0;
        m_lastPinchSepY = 0.0;

        // Fingers lifted: close the gesture HERE. mouseUp is not reliable for
        // touch (a press that started on a clip never reaches the view), and a
        // stale flag meant the next gesture skipped its re-anchor and jumped —
        // the "maxed out" scroll.
        //
        // ONLY when touchCount == 0: running this with one finger still down
        // reset the anchor every tick and the pan never moved at all.
        if (touchCount == 0 && m_touchScrollActive)
        {
            m_touchScrollActive  = false;
            m_smoothedTouchValid = false;

            // Hold released without dragging = right click at that point.
            if (m_holdActive && ! m_holdDragging)
            {
                const auto localPos = m_holdLocalPos.toInt();
                if (auto* clip = findClipAt(localPos))
                    showClipContextMenu(clip, localPointToGlobal(localPos), localPos);
            }
            m_holdActive   = false;
            m_holdDragging = false;

            // Fling thresholds, Android-style: below the minimum velocity there
            // is no fling at all, and the release velocity is clamped so a fast
            // flick cannot fly off. (Velocity is px per 60 Hz frame.)
            constexpr double kMinFlingVelocity = 1.0;    // ≈ 60 px/s
            constexpr double kMaxFlingVelocity = 120.0;  // ≈ 7200 px/s
            if (std::abs(m_touchPanVelocityX) < kMinFlingVelocity
                && std::abs(m_touchPanVelocityY) < kMinFlingVelocity)
            {
                m_touchPanVelocityX = 0.0;
                m_touchPanVelocityY = 0.0;
            }
            m_touchPanVelocityX = juce::jlimit(-kMaxFlingVelocity, kMaxFlingVelocity, m_touchPanVelocityX);
            m_touchPanVelocityY = juce::jlimit(-kMaxFlingVelocity, kMaxFlingVelocity, m_touchPanVelocityY);
        }

        if (touchCount == 1)
        {
            // One finger pans the timeline — anchored, with a 6 px slop so a
            // small nudge does not register, and the release velocity tracked
            // for the iOS momentum.
            // Low-pass the touch position before using it: digitizers report
            // ±1 px of noise on a nearly still finger, which is what made slow
            // pans shake. The filter trades ~1 frame of lag for stability.
            const auto localRaw = getLocalPoint(nullptr, firstTouch.toInt()).toFloat();
            if (! m_smoothedTouchValid)
            {
                m_smoothedTouchPos   = localRaw;
                m_smoothedTouchValid = true;
            }
            else
            {
                m_smoothedTouchPos = m_smoothedTouchPos * 0.6f + localRaw * 0.4f;
            }
            const auto local = m_smoothedTouchPos;

            // A new swipe is detected by the touch's OWN down time — exact, no
            // distance threshold. (The old 250 px bound missed a re-touch close
            // to the previous gesture, which reused the stale anchor and made
            // the view jump near the tap.)
            if (m_touchScrollActive
                && firstTouchDownTime != m_gestureTouchDownTime)
            {
                m_touchScrollActive = false;
            }

            // Was the view still gliding when this finger landed? Then the
            // touch means "keep scrolling" and must respond immediately.
            const bool momentumWasRunning =
                (std::abs(m_touchPanVelocityX) > 1.0 || std::abs(m_touchPanVelocityY) > 1.0);

            if (! m_touchScrollActive)
            {
                m_touchScrollActive = true;
                m_skipSlop          = momentumWasRunning;
                m_gestureDownPos    = local.toInt();
                m_gestureStartViewX = viewport->getViewPositionX();
                m_gestureStartViewY = viewport->getViewPositionY();
                m_lastTouchPanPos    = local.toInt();
                m_lastTouchPanTimeMs = juce::Time::getMillisecondCounterHiRes();
                m_touchPanVelocityX  = 0.0;
                m_touchPanVelocityY  = 0.0;

                m_gestureTouchDownTime = firstTouchDownTime;

                // Long-press detection starts now.
                m_holdLocalPos = local;
                m_holdStartMs  = m_lastTouchPanTimeMs;
                m_holdActive   = false;
                m_holdDragging = false;
                return true;
            }

            const auto pos = local.toInt();
            const bool still = std::abs(pos.x - m_gestureDownPos.x) < 10
                            && std::abs(pos.y - m_gestureDownPos.y) < 10;

            // Long press: a still finger for 500 ms lights the glow pulse under
            // it. Release at that point = right click; move = marquee.
            if (still && ! m_skipSlop)
            {
                if (! m_holdActive
                    && juce::Time::getMillisecondCounterHiRes() - m_holdStartMs >= 500.0)
                {
                    m_holdActive   = true;
                    m_holdLocalPos = local;
                }

                if (m_holdActive)
                    repaint(juce::Rectangle<int>((int) m_holdLocalPos.x - 40,
                                                 (int) m_holdLocalPos.y - 40, 80, 80));
                return true;
            }

            m_skipSlop = false;

            // Hold + drag = marquee (the hold armed it).
            if (m_holdActive && ! m_holdDragging)
            {
                m_holdDragging = true;
                m_selection.deselectAll();
                m_marqueeHitGeometry.clear();
                m_marqueeHitGeometry.reserve(m_clipRenderers.size());
                for (const auto& renderer : m_clipRenderers)
                    if (renderer != nullptr)
                        m_marqueeHitGeometry.emplace_back(renderer->getClipId(), renderer->getBounds());
                m_rubberBand.startDrag(m_holdLocalPos.toInt());
                if (onEngineClipSelectionCleared)
                    onEngineClipSelectionCleared();
            }

            if (m_holdDragging)
            {
                if (m_rubberBand.isVisible())
                    m_rubberBand.updateDrag(pos);
                return true;
            }

            viewport->setViewPosition(m_gestureStartViewX - (pos.x - m_gestureDownPos.x),
                                      m_gestureStartViewY - (pos.y - m_gestureDownPos.y));

            const auto nowMs = juce::Time::getMillisecondCounterHiRes();
            const auto dtMs  = nowMs - m_lastTouchPanTimeMs;
            if (dtMs > 0.0 && dtMs < 120.0)
            {
                const auto delta = pos - m_lastTouchPanPos;
                m_touchPanVelocityX = 0.7 * m_touchPanVelocityX + 0.3 * (double) delta.x;
                m_touchPanVelocityY = 0.7 * m_touchPanVelocityY + 0.3 * (double) delta.y;
            }
            m_lastTouchPanPos    = pos;
            m_lastTouchPanTimeMs = nowMs;
            return true;
        }

        m_gestureIsPinch = false;

        // Exponential decay per frame. 0.967 per 16 ms frame is the iOS
        // UIScrollView "normal" deceleration (0.998 per ms) — the natural,
        // long glide. 0.93 was stopping noticeably short.
        m_touchPanVelocityX *= 0.967;
        m_touchPanVelocityY *= 0.967;

        if (std::abs(m_touchPanVelocityX) < 0.35 && std::abs(m_touchPanVelocityY) < 0.35)
        {
            // Gesture (and its momentum) finished: restore full detail and
            // re-lay-out the clips once, not every frame.
            ClipRenderCore::s_lightweightPaint.store(false, std::memory_order_relaxed);
            rebuildClipRenderers();
            updateAutomationLayout();
            repaint();
            return false;
        }

        const int maxX = juce::jmax(0, getWidth()  - viewport->getViewWidth());
        const int maxY = juce::jmax(0, getHeight() - viewport->getViewHeight());
        viewport->setViewPosition(
            juce::jlimit(0, maxX, viewport->getViewPositionX() - juce::roundToInt(m_touchPanVelocityX)),
            juce::jlimit(0, maxY, viewport->getViewPositionY() - juce::roundToInt(m_touchPanVelocityY)));
        return true;
    }

    void ArrangementViewCore::endTouchPanGesture()
    {
        m_touchScrollActive = false;

        // Momentum only after a REAL swipe. A tiny nudge used to fling on a
        // spurious velocity spike, which is what made small movements jitter.
        const double dx = (double) (m_lastTouchPanPos.x - m_gestureDownPos.x);
        const double dy = (double) (m_lastTouchPanPos.y - m_gestureDownPos.y);
        const bool realSwipe = std::sqrt(dx * dx + dy * dy) > 24.0;

        if (realSwipe
            && (std::abs(m_touchPanVelocityX) > 1.0 || std::abs(m_touchPanVelocityY) > 1.0))
        {
            if (! m_inertiaTimer)
                m_inertiaTimer = std::make_unique<TouchPanInertiaTimer>(*this);
            m_inertiaTimer->startTimerHz(60);
        }
    }

    void ArrangementViewCore::applyViewOnlyMode()
    {
        // Publish to the global flag FIRST: every clip renderer reads it live,
        // so the guard applies even to renderers not in the local list.
        ClipRenderCore::s_viewOnlyMode.store(m_viewOnlyMode, std::memory_order_relaxed);
        // Free View Mode: the timeline becomes view-only — scroll and zoom keep
        // working, but clips and automation lanes stop receiving mouse events
        // entirely, so nothing can be moved or edited by accident.
        for (auto& renderer : m_clipRenderers)
            if (renderer != nullptr)
                renderer->setInterceptsMouseClicks(! m_viewOnlyMode, false);

        for (auto& [trackId, container] : m_automationContainers)
            if (container != nullptr)
                container->setInterceptsMouseClicks(! m_viewOnlyMode, false);

        if (m_viewOnlyMode)
        {
            // Entering Free View clears the selection: nothing stays selected
            // (so nothing can look movable) while the timeline is view-only,
            // and no half-state can survive the mode.
            m_selection.deselectAll();
            for (auto& renderer : m_clipRenderers)
                if (renderer != nullptr)
                    renderer->setSelected(false);

            if (onEngineClipSelectionCleared)
                onEngineClipSelectionCleared();

            if (m_rubberBand.isVisible())
                m_rubberBand.endDrag();

            repaint();
        }
    }

    void ArrangementViewCore::drawPhase1VolumeAutomation(juce::Graphics& g)
    {
        if (m_engineTrackManager == nullptr)
            return;

        uint64_t phase1LanesDrawn = 0;

        const int trackH = juce::jmax(1, (int)m_zoom.getTrackHeightPx());
        const int top = arrangementContentTop();

        for (int i = 0; i < m_engineTrackManager->getNumTracks(); ++i)
        {
            auto* track = m_engineTrackManager->getTrack(i);
            if (track == nullptr || !track->isAutomationVisible() || track->isAutomationMuted())
                continue;

            const auto activeParameterId = track->getActiveAutomationParameterId().isNotEmpty()
                ? track->getActiveAutomationParameterId()
                : juce::String(DAW::AutomationLaneCore::trackVolumeParameterId);

            bool hasVisibleCoreLane = false;
            if (m_automationManager != nullptr)
            {
                if (const auto* activeLane = m_automationManager->findLane(track->getID(), activeParameterId))
                    hasVisibleCoreLane = activeLane->isEnabled() && !activeLane->points.empty();
            }

            if (hasVisibleCoreLane)
                continue;

            const auto apexId = apexIdForVisibleTarget(track->getID(), activeParameterId);
            if (!hasApexAutomationLane(apexId))
                continue;

            const int masterOffset = (m_engineTrackManager->hasMasterTrack() ? 1 : 0);
            const float yTop = (float)(top + (i + masterOffset) * trackH);
            const float laneHeight = (float)getAutomationLaneVisualHeight(trackH);
            const auto laneBounds = juce::Rectangle<float>(0.0f,
                                                           yTop + (float)trackH - laneHeight - 2.0f,
                                                           (float)getWidth(),
                                                           laneHeight);
            const auto colour = getAutomationLaneColour(activeParameterId, track->getColor());
            const auto laneFillBounds = laneBounds.reduced(2.0f, 0.0f);

            // Draw a stronger foreground lane plate so clips read clearly behind automation
            // whenever automation is visible on the track.
            g.setColour(juce::Colour(0xFF0E111B).withAlpha(0.88f));
            g.fillRoundedRectangle(laneFillBounds, 4.0f);

            g.setColour(colour.withAlpha(0.16f));
            g.fillRoundedRectangle(laneFillBounds, 4.0f);

            g.setColour(colour.withAlpha(0.42f));
            g.drawRoundedRectangle(laneFillBounds, 4.0f, 1.0f);

            g.setColour(juce::Colours::white.withAlpha(0.42f));
            g.setFont(juce::Font(10.0f, juce::Font::plain));
            g.drawText(activeParameterId, laneBounds.reduced(8.0f, 2.0f).toNearestInt(), juce::Justification::topRight, true);

            drawApexAutomationLane(g,
                                   apexId,
                                   laneBounds,
                                   colour,
                                   activeParameterId.endsWith(".bypass") || activeParameterId == "track.mute" || activeParameterId == "track.solo");
            ++phase1LanesDrawn;
        }

        if (TimelinePaintMetrics::active.load(std::memory_order_relaxed))
            TimelinePaintMetrics::automationPhase1LanesDrawn.fetch_add(phase1LanesDrawn);
    }

    void ArrangementViewCore::drawCoreAutomationLane(juce::Graphics& g,
                                                     const DAW::AutomationLaneCore& lane,
                                                     juce::Rectangle<float> laneBounds,
                                                     juce::Colour colour)
    {
        if (lane.points.empty())
            return;

        const double sampleRate = juce::jmax(1.0, m_engineSampleRate);
        juce::Path path;
        bool started = false;
        float firstVisibleY = laneBounds.getCentreY();

        for (size_t i = 0; i < lane.points.size(); ++i)
        {
            const auto& point = lane.points[i];
            const float x = (float)m_zoom.timeToX((double)point.timeSamples / sampleRate);
            const float y = laneBounds.getBottom()
                          - normaliseAutomationValueForDisplay(lane.parameterId, point.value) * laneBounds.getHeight();

            if (x < -20.0f || x > (float)getWidth() + 20.0f)
                continue;

            if (!started)
            {
                path.startNewSubPath(x, y);
                firstVisibleY = y;
                started = true;
            }
            else
            {
                path.lineTo(x, y);
            }

            g.setColour(colour.withAlpha(0.95f));
            g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        }

        if (!started)
            return;

        if (lane.points.size() == 1)
        {
            g.setColour(colour.withAlpha(0.50f));
            g.drawHorizontalLine(juce::roundToInt(firstVisibleY), 0.0f, (float)getWidth());
        }
        else
        {
            g.setColour(colour.withAlpha(0.22f));
            g.strokePath(path, juce::PathStrokeType(5.0f));
            g.setColour(colour.withAlpha(0.95f));
            g.strokePath(path, juce::PathStrokeType(2.0f));
        }
    }

    void ArrangementViewCore::drawApexAutomationLane(juce::Graphics& g,
                                                     apex::automation::ParameterID paramID,
                                                     juce::Rectangle<float> laneBounds,
                                                     juce::Colour colour,
                                                     bool stepped)
    {
        if (paramID == apex::automation::kInvalidParameterID)
            return;

        auto lane = apex::automation::AutomationLaneStore::getInstance().findLane(paramID);
        if (lane == nullptr)
            return;

        auto snap = lane->getSnapshot();
        if (snap == nullptr || snap->empty())
            return;

        // Convert APEX PPQ beats back to seconds for display using the real
        // project tempo, matching the write-side conversion in
        // addOrReplaceApexPoint / syncApexLaneFromCoreLane.
        const double ppqPerSecond = juce::jmax(1.0, m_tempoBpm) / 60.0;
        juce::Path path;
        bool started = false;
        std::vector<juce::Point<float>> visiblePoints;
        visiblePoints.reserve(snap->size());

        for (size_t i = 0; i < snap->size(); ++i)
        {
            const auto& bp = snap->at(i);
            const float x = (float)m_zoom.timeToX(bp.timePPQ / ppqPerSecond);
            if (x < -20.0f || x > (float)getWidth() + 20.0f)
                continue;

            const float y = laneBounds.getBottom() - juce::jlimit(0.0f, 1.0f, bp.normalizedValue) * laneBounds.getHeight();
            if (!started)
            {
                path.startNewSubPath(x, y);
                started = true;
            }
            else if (stepped && i > 0)
            {
                const auto& prev = snap->at(i - 1);
                const float prevY = laneBounds.getBottom() - juce::jlimit(0.0f, 1.0f, prev.normalizedValue) * laneBounds.getHeight();
                path.lineTo(x, prevY);
                path.lineTo(x, y);
            }
            else
            {
                path.lineTo(x, y);
            }

            visiblePoints.emplace_back(x, y);
        }

        if (started)
        {
            g.setColour(colour.withAlpha(0.22f));
            g.strokePath(path, juce::PathStrokeType(5.0f));
            g.setColour(colour.withAlpha(0.95f));
            g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(juce::Colours::black.withAlpha(0.65f));
            for (auto p : visiblePoints)
                g.fillEllipse(p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);

            g.setColour(colour.brighter(0.35f));
            for (auto p : visiblePoints)
                g.drawEllipse(p.x - 3.5f, p.y - 3.5f, 7.0f, 7.0f, 1.5f);
        }
    }

    bool ArrangementViewCore::hasApexAutomationLane(apex::automation::ParameterID paramID) const
    {
        if (paramID == apex::automation::kInvalidParameterID)
            return false;

        auto lane = apex::automation::AutomationLaneStore::getInstance().findLane(paramID);
        if (lane == nullptr)
            return false;

        auto snap = lane->getSnapshot();
        return snap != nullptr && !snap->empty();
    }

    void ArrangementViewCore::paintOverChildren(juce::Graphics& g)
    {
        const bool metricsActive = TimelinePaintMetrics::active.load(std::memory_order_relaxed);
        if (metricsActive)
            TimelinePaintMetrics::paintOverChildrenCount.fetch_add(1);
        TimelinePaintMetrics::ScopedTimer paintChildrenTimer(TimelinePaintMetrics::paintChildrenDurationNs);

        // Automation lanes drawn here so they always appear above clip components
        drawPhase1VolumeAutomation(g);

        if (m_drag.active)
            drawGhost(g);

        const int x = timeToScreenX(m_playheadTimeSeconds);
        if (x >= 0 && x <= getWidth())
        {
            const auto accent = juce::Colour(0xFFFF1678);
            const int top = m_ruler.getBottom();

            g.setColour(accent.withAlpha(0.24f));
            g.fillRect(x - 2, top, 5, juce::jmax(0, getHeight() - top));
            g.setColour(accent);
            g.drawVerticalLine(x, (float)top, (float)getHeight());
        }

        // Long-press glow: a pulsing ring under the finger while it is held.
        // Release here = right click; move = marquee.
        if (m_holdActive && ! m_holdDragging)
        {
            const double phase = (juce::Time::getMillisecondCounterHiRes() - m_holdStartMs) / 1000.0;
            const float  pulse = 0.5f + 0.5f * (float) std::sin(phase * 6.2831853 * 1.6);
            const float  radius = 16.0f + 8.0f * pulse;
            const juce::Colour glow(0xFFFFB347);
            g.setColour(glow.withAlpha(0.22f + 0.35f * pulse));
            g.drawEllipse(m_holdLocalPos.x - radius, m_holdLocalPos.y - radius,
                          radius * 2.0f, radius * 2.0f, 2.5f);
            g.setColour(glow.withAlpha(0.16f + 0.12f * pulse));
            g.fillEllipse(m_holdLocalPos.x - 11.0f, m_holdLocalPos.y - 11.0f, 22.0f, 22.0f);
        }

        // The opaque HeaderClipGuard child covers the header, so the lock
        // button must be drawn here (above all children) to be visible.
        drawLockButton(g);

        if (m_arrangementDebugPanelVisible && m_arrangementDebugPanel != nullptr)
            m_arrangementDebugPanel->toFront(false);
    }

    void ArrangementViewCore::resized()
    {
        updatePinnedViewportControls();

        rebuildClipRenderers();
        updateAutomationLayout();
    }

    void ArrangementViewCore::updatePinnedViewportControls()
    {
        const auto viewportWidth = [this]() -> int
        {
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
                return juce::jmax(1, viewport->getViewWidth());
            return getWidth();
        }();

        int w = getWidth();
        int toolbarH = 48;
        int rulerH = 32;

        // This is an explicit occlusion boundary for all scrollable content.
        // It moves with the pinned controls in content coordinates, therefore
        // the Viewport presents it at a fixed screen-space top edge.
        m_headerClipGuard.setBounds(m_viewportScrollX, m_viewportScrollY,
                                    viewportWidth, toolbarH + rulerH);
        m_toolbar.setBounds(m_viewportScrollX, m_viewportScrollY, juce::jmax(120, viewportWidth - 140), toolbarH);

        // Lock button (horizontal lock) — sits left of piano roll button
        m_lockBtnBounds = juce::Rectangle<int>(m_viewportScrollX + viewportWidth - 108, m_viewportScrollY + 10, 28, 28);

        // Free View Mode button — sits left of the lock button
        m_viewOnlyBtnBounds = juce::Rectangle<int>(m_viewportScrollX + viewportWidth - 144, m_viewportScrollY + 10, 28, 28);

        // Shield over the pinned-button cluster: clips scroll underneath it,
        // so without this they swallow the clicks meant for the buttons.
        if (! m_headerButtonShield)
        {
            m_headerButtonShield = std::make_unique<HeaderButtonShield>(*this);
            addAndMakeVisible(*m_headerButtonShield);
        }
        m_headerButtonShield->setBounds(m_viewportScrollX + viewportWidth - 168,
                                        m_viewportScrollY + 6, 164, 36);
        m_headerButtonShield->toFront(false);
        m_pianoRollBtn.toFront(false);

        // Piano Roll button — compact, to the right of lock button
        m_pianoRollBtn.setBounds(m_viewportScrollX + viewportWidth - 56, m_viewportScrollY + 10, 48, 28);
        m_ruler.setBounds(m_viewportScrollX, m_viewportScrollY + toolbarH, viewportWidth, rulerH);
        m_ghostOverlay.setBounds(getLocalBounds());

        if (m_arrangementDebugPanel != nullptr)
            m_arrangementDebugPanel->setBounds(juce::Rectangle<int>(juce::jmax(20, w - 460), toolbarH + rulerH + 8, 440, juce::jmax(220, getHeight() - (toolbarH + rulerH + 16))));
    }

    void ArrangementViewCore::setViewportScrollOffset(int scrollX, int scrollY)
    {
        scrollX = juce::jmax(0, scrollX);
        scrollY = juce::jmax(0, scrollY);

        if (m_viewportScrollX == scrollX && m_viewportScrollY == scrollY)
            return;

        m_viewportScrollX = scrollX;
        m_viewportScrollY = scrollY;
        m_ruler.setScrollOffsetX(scrollX);

        TimelinePaintMetrics::inc(TimelinePaintMetrics::arrangementScrollEvents);
        const juce::ScopedValueSetter<bool> scrollGuard(m_inViewportScroll, true);

        // Scrolling changes only the viewport-relative controls and which
        // existing clip components are eligible to paint.  It is not a
        // structural resize: clip models, bounds, automation layout, and
        // renderer ownership remain unchanged.
        updatePinnedViewportControls();
        updateClipRendererVisibility();

        // Repaint only the viewport-visible content region.  JUCE coalesces
        // this with the Viewport's exposed-area invalidation, while keeping
        // the culling/LOD decisions above authoritative.  Unchanged clip
        // renderers are not individually invalidated by a pure translation.
        const auto visibleRepaint = getViewportVisibleBounds().getIntersection(getLocalBounds());
        if (!visibleRepaint.isEmpty())
        {
            TimelinePaintMetrics::inc(TimelinePaintMetrics::arrangementScrollPartialRepaints);
            if (TimelinePaintMetrics::active.load(std::memory_order_relaxed))
            {
                const auto area = static_cast<int64_t>(
                    static_cast<uint64_t>(juce::jmax(0, visibleRepaint.getWidth()))
                    * static_cast<uint64_t>(juce::jmax(0, visibleRepaint.getHeight())));
                TimelinePaintMetrics::inc(TimelinePaintMetrics::arrangementScrollDirtyAreaPixels, area);
            }
            repaint(visibleRepaint);
        }
        else
        {
            // Defensive fallback for an invalid/un-sized viewport.  Normal
            // production scrolls have a non-empty intersection and therefore
            // never take this broad invalidation path.
            TimelinePaintMetrics::inc(TimelinePaintMetrics::arrangementScrollFullRepaints);
            repaint();
        }
    }

    void ArrangementViewCore::setSnapMode(SnapMode mode)
    {
        m_snapMode = mode;
        m_toolbar.setSelectedSnapModeIndex(getSnapModeIndex());
        repaint();
    }

    int ArrangementViewCore::getSnapModeIndex() const noexcept
    {
        switch (m_snapMode)
        {
        case SnapMode::ThirtySecond: return 1;
        case SnapMode::Sixteenth:    return 2;
        case SnapMode::Eighth:       return 3;
        case SnapMode::Quarter:      return 4;
        case SnapMode::Half:         return 5;
        case SnapMode::Bar:          return 6;
        case SnapMode::Bar2:         return 7;
        case SnapMode::Bar4:         return 8;
        default:                                          return 0;
        }
    }

    void ArrangementViewCore::setEngineSampleRate(double sr)
    {
        double oldSr = m_engineSampleRate;
        double newSr = juce::jmax(1.0, sr);

        // Only recalculate if the rate actually changed
        if (std::abs(newSr - oldSr) > 0.1)
        {
            DBG("[EngineSync] Sample rate changed: " << oldSr << " -> " << newSr);
            m_engineSampleRate = newSr;
            m_engineSampleRateInitialized = true;

            // If we have engine clips, recalculate their positions/lengths
            // for the new sample rate
            if (m_engineClipManager && oldSr > 0.0)
            {
                const double ratioFactor = newSr / oldSr;
                int clipCount = 0;

                // Iterate all clips and scale their sample-domain positions
                for (const auto& [uuid, engineId] : m_uuidToEngineId)
                {
                    auto* ec = dynamic_cast<DAW::AudioClip*>(m_engineClipManager->getClip(engineId));
                    if (!ec) continue;

                    // Rescale the engine sample positions by the rate ratio
                    auto oldStartPos = ec->getStartPosition();
                    auto oldLength = ec->getLength();
                    auto oldSourceOffset = ec->getSourceOffset();

                    ec->setStartPosition((DAW::SamplePosition)std::llround((double)oldStartPos * ratioFactor));
                    ec->setLength(juce::jmax((DAW::SamplePosition)1,
                                  (DAW::SamplePosition)std::llround((double)oldLength * ratioFactor)));
                    ec->setSourceOffset(juce::jmax((DAW::SamplePosition)0,
                                        (DAW::SamplePosition)std::llround((double)oldSourceOffset * ratioFactor)));
                    clipCount++;
                }

                if (clipCount > 0)
                    DBG("[EngineSync] Recalculated " << clipCount << " clips for new sample rate (factor=" << ratioFactor << ")");
            }
        }
        else
        {
            m_engineSampleRate = newSr;
            m_engineSampleRateInitialized = true;
        }

        // Push the authoritative rate to every consumer that converts
        // seconds<->samples in this view (automation panels, clip renderers).
        if (m_propertiesWindow)
            m_propertiesWindow->setEngineSampleRate (m_engineSampleRate);
        for (auto& renderer : m_clipRenderers)
            if (renderer != nullptr)
                renderer->setEngineSampleRate (m_engineSampleRate);

        // Crossfade ranges are timeline-sample coordinates, so republish the
        // derived plan after any engine-rate conversion.
        refreshAutoCrossfadesForAllTracks();
    }

    void ArrangementViewCore::recalculateEngineClipsForNewSampleRate(double oldSr, double newSr)
    {
        const double ratioFactor = newSr / oldSr;
        int clipCount = 0;

        for (const auto& [uuid, engineId] : m_uuidToEngineId)
        {
            auto* ec = dynamic_cast<DAW::AudioClip*>(m_engineClipManager->getClip(engineId));
            if (!ec) continue;

            auto oldStartPos = ec->getStartPosition();
            auto oldLength = ec->getLength();
            auto oldSourceOffset = ec->getSourceOffset();

            ec->setStartPosition((DAW::SamplePosition)std::llround((double)oldStartPos * ratioFactor));
            ec->setLength(juce::jmax((DAW::SamplePosition)1,
                          (DAW::SamplePosition)std::llround((double)oldLength * ratioFactor)));
            ec->setSourceOffset(juce::jmax((DAW::SamplePosition)0,
                                (DAW::SamplePosition)std::llround((double)oldSourceOffset * ratioFactor)));
            clipCount++;
        }

        if (clipCount > 0)
            DBG("[EngineSync] Recalculated " << clipCount << " clips for sample rate change (factor=" << ratioFactor << ")");
    }

    void ArrangementViewCore::setTempoBpm(double bpm)
    {
        m_tempoBpm = juce::jmax(1.0, bpm);
        m_ruler.setTempo(m_tempoBpm);
    }

    void ArrangementViewCore::rebuildClipRenderers()
    {
        // Guard against recursive rebuild calls (e.g., from zoom callbacks)
        if (m_rebuildInProgress)
        {
            m_deferredRebuildRequested = true;
            return;
        }

        juce::ScopedValueSetter<bool> guard(m_rebuildInProgress, true);
        rebuildClipRenderersInternal();

        // If another rebuild was requested while we were running, defer it
        if (m_deferredRebuildRequested)
        {
            m_deferredRebuildRequested = false;
            m_rebuildDeferTimer->startTimer(1);  // Defer 1ms to avoid immediate recursion
        }
    }

    void ArrangementViewCore::rebuildClipRenderersInternal()
    {
        std::set<juce::Uuid> liveClipIds;
        for (const auto& clip : m_clipState.allClips())
            liveClipIds.insert(clip.id);

        for (auto it = m_clipRenderers.begin(); it != m_clipRenderers.end();)
        {
            // Use the stable UUID stored in the renderer — never dereference
            // m_model here because removeClip() may have already erased the
            // backing ArrangementClipModel from m_clipState, leaving m_model dangling.
            const auto& rid = (*it)->getClipId();
            if (rid.isNull() || liveClipIds.find(rid) == liveClipIds.end())
            {
                removeChildComponent(it->get());
                it = m_clipRenderers.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto& clip : m_clipState.allClips())
            upsertClipRenderer(clip.id);

        updateClipRendererVisibility();

        // Automation containers + pinned toolbar/ruler z-order (the toolbar
        // must stay above clip lanes so the Quick New Track button can never
        // be covered by scrolled content).
        ensureToolbarZOrder();

        updateClipAutomationDim();

        // Queue waveform loads for any clips that need them
        for (auto& clip : m_clipState.allClips())
            if (auto* renderer = findClipRendererFor(clip.id))
                if (renderer->isVisible()
                    && renderer->shouldRenderWaveform()
                    && renderer->getClip() != nullptr
                    && !renderer->getClip()->sourcePath.empty()
                    && renderer->needsWaveformUpdate())
                    m_waveformLoadQueue.push(clip.id);

        // Start waveform loading if queue is not empty
        if (!m_waveformLoadQueue.empty() && m_waveformLoadTimer)
            m_waveformLoadTimer->startTimer(16);  // ~60 FPS
    }

    void ArrangementViewCore::upsertClipRenderer(const juce::Uuid& clipId)
    {
        auto* clip = m_clipState.findClip(clipId);
        if (clip == nullptr)
            return;

        int toolbarH = 48;
        int rulerH = 32;
        int trackH = (int)m_zoom.getTrackHeightPx();

        int x = timeToScreenX(clip->startTime);
        int w = (int)(clip->visualLength() * m_zoom.getPixelsPerSecond());
        w = juce::jmax(4, w);
        int y = toolbarH + rulerH + clip->trackIndex * trackH;
        int h = trackH - 2;

        // Collapsed folder: keep the renderer alive but park it offscreen so
        // expanding the folder restores every clip instantly. laneIndex 0 is
        // the MASTER lane; regular tracks sit at laneIndex = index + offset.
        if (m_engineTrackManager != nullptr)
        {
            const int masterOffset = m_engineTrackManager->hasMasterTrack() ? 1 : 0;
            if (clip->trackIndex >= masterOffset)
                if (auto* track = m_engineTrackManager->getTrack(clip->trackIndex - masterOffset))
                    if (isTrackHiddenByCollapsedFolder(track))
                    {
                        y = -100000;
                        h = 1;
                    }
        }

        DAW::AudioFileManager::CachedAudioHandle cachedAudio;
        bool hasEngineAudioOwner = false;
        if (m_engineAudioFiles != nullptr)
        {
            const auto mapped = m_uuidToEngineId.find(clipId);
            if (mapped != m_uuidToEngineId.end())
            {
                hasEngineAudioOwner = true;
                cachedAudio = m_engineAudioFiles->getCachedAudioSnapshot(mapped->second);
            }
        }

        const juce::Rectangle<int> targetBounds(x, y, w, h);

        if (auto* renderer = findClipRendererFor(clipId))
        {
            renderer->updateClipData(*clip);
            renderer->setCachedAudioHandle(cachedAudio, !hasEngineAudioOwner);
            if (renderer->getBounds() != targetBounds)
                renderer->setBounds(targetBounds);
            renderer->setSelected(m_selection.isSelected(clip->id));
            renderer->setVisible(shouldShowClipRenderer(*renderer));
            if (renderer->isVisible() && renderer->shouldRenderWaveform()
                && renderer->getClip() != nullptr
                && !renderer->getClip()->sourcePath.empty()
                && renderer->needsWaveformUpdate())
                renderer->refresh();
            renderer->repaint();
            return;
        }

        auto renderer = std::make_unique<ClipRenderCore>(
            const_cast<ArrangementClipModel&>(*clip), m_zoom,
            std::move(cachedAudio), !hasEngineAudioOwner);
        renderer->setEngineSampleRate (m_engineSampleRate);
        renderer->setBounds(targetBounds);
        renderer->setSelected(m_selection.isSelected(clip->id));

        // Wire interactive handle callbacks (fade corners, edge resize, volume btn)
        const juce::Uuid clipUuid = clip->id;
        auto* rendererPtr = renderer.get();

        // Snapshot of the clip when an edit begins — used to build the undo command.
        auto preEdit = std::make_shared<std::optional<ArrangementClipModel>>();

        rendererPtr->onEditBegin = [this, clipUuid, preEdit]()
        {
            if (auto* c = m_clipState.findClip(clipUuid))
                *preEdit = *c;
        };

        rendererPtr->onEditLive = [this, clipUuid]()
        {
            // Realtime feedback during drag: refresh model timestamps + engine sync
            // so the playback engine reflects the new length/fade/trim immediately.
            if (auto* c = m_clipState.findClip(clipUuid))
            {
                // The renderer edits its OWN ArrangementClipModel copy (m_clipData)
                // during a fade/trim drag. Adopt that edited copy into the
                // authoritative state model BEFORE syncing. Without this, the stale
                // state would be pushed to the engine and then re-copied back over
                // the renderer by upsertClipRenderer() below — reverting every drag
                // delta so edge resizes and fade handles appear to "do nothing".
                if (auto* renderer = findClipRendererFor(clipUuid))
                    *c = *renderer->getClip();

                c->bumpWaveformVisualVersion();

                // CRITICAL: guard the whole live edit. Both m_clipState.updateClip()
                // (via onClipChanged) and syncClipToEngine() call the engine AudioClip
                // setters (setLength / setSourceOffset / setTimeStretch / ...), and each
                // setter fires Clip::clipPropertyChanged back into
                // ArrangementView::mirrorEngineClipIntoArrangement(). Without the guard
                // that callback re-mirrors STALE engine state over the freshly-dragged
                // model mid-sync — e.g. setLength fires before pushToAudioClip() writes
                // the new sourceEndSample, leaving the model with a short length but the
                // full source window. The waveform then squeezes the whole file into the
                // shrunken box, which looks exactly like a time-stretch, and left-edge
                // trim gets its startTime/sourceOffset clobbered. Suppressing the
                // re-entrant mirror keeps trim behaving as a pure trim.
                {
                    juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);
                    m_clipState.updateClip(*c);
                    syncClipToEngine(*c);
                }

                refreshAutoCrossfadesForTrack(c->trackIndex);

                // Re-position the renderer so trim/resize visually shrinks the
                // clip box in realtime (matches Pro Tools / Logic / Live behavior).
                upsertClipRenderer(clipUuid);
            }
        };

        rendererPtr->onVolumeButtonClicked = [this, clipUuid]()
        {
            // Pro-DAW-style quick-access volume popup (CallOutBox).
            // The full properties window is reserved for the (...) menu /
            // double-click — this button just exposes the gain knob inline.
            auto* anchorRenderer = findClipRendererFor(clipUuid);
            if (anchorRenderer == nullptr) return;

            class QuickGainPopup : public juce::Component, public juce::Slider::Listener
            {
            public:
                QuickGainPopup(ArrangementViewCore& v, const juce::Uuid& id) : view(v), uuid(id)
                {
                    setSize(120, 130);
                    knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
                    knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
                    knob.setRange(-60.0, 12.0, 0.0);
                    knob.textFromValueFunction = [](double v) {
                        return juce::String(v, 1) + " dB";
                    };
                    if (auto* c = view.m_clipState.findClip(uuid))
                        knob.setValue(juce::Decibels::gainToDecibels(c->gain, -60.f),
                                       juce::dontSendNotification);
                    knob.addListener(this);
                    // Double-click resets to 0 dB (unity), matching pro DAW behavior.
                    knob.setDoubleClickReturnValue(true, 0.0);
                    addAndMakeVisible(knob);

                    title.setText("Volume", juce::dontSendNotification);
                    title.setJustificationType(juce::Justification::centred);
                    title.setFont(juce::Font(11.f, juce::Font::bold));
                    addAndMakeVisible(title);

                    // Snapshot for undo on close
                    if (auto* c = view.m_clipState.findClip(uuid))
                        beforeGain = c->gain;
                }
                ~QuickGainPopup() override
                {
                    if (auto* c = view.m_clipState.findClip(uuid))
                    {
                        const float afterGain = c->gain;
                        if (std::abs(afterGain - beforeGain) > 1.0e-5f)
                        {
                            const float b = beforeGain, a = afterGain;
                            auto& view_ref = view;
                            auto id = uuid;
                            auto apply = [&view_ref, id](float g)
                            {
                                if (auto* cc = view_ref.m_clipState.findClip(id))
                                {
                                    cc->gain = g;
                                    cc->bumpWaveformVisualVersion();
                                    view_ref.m_clipState.updateClip(*cc);
                                    view_ref.syncClipToEngine(*cc);
                                }
                            };
                            view.m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                                "Edit Clip Gain",
                                [apply, a]() { apply(a); },
                                [apply, b]() { apply(b); },
                                /*alreadyApplied*/ true));
                        }
                    }
                }
                void resized() override
                {
                    auto r = getLocalBounds().reduced(4);
                    title.setBounds(r.removeFromTop(16));
                    knob.setBounds(r);
                }
                void sliderValueChanged(juce::Slider* s) override
                {
                    if (s != &knob) return;
                    if (auto* c = view.m_clipState.findClip(uuid))
                    {
                        c->gain = juce::Decibels::decibelsToGain((float)knob.getValue(), -60.f);
                        c->bumpWaveformVisualVersion();
                        view.m_clipState.updateClip(*c);
                        view.syncClipToEngine(*c);
                    }
                }
            private:
                ArrangementViewCore& view;
                juce::Uuid uuid;
                juce::Slider knob;
                juce::Label title;
                float beforeGain = 1.0f;
            };

            auto popup = std::make_unique<QuickGainPopup>(*this, clipUuid);
            const auto screenArea = anchorRenderer->getScreenBounds();
            juce::CallOutBox::launchAsynchronously(std::move(popup), screenArea, nullptr);
        };

        rendererPtr->onMuteButtonClicked = [this, clipUuid]()
        {
            auto* c = m_clipState.findClip(clipUuid);
            if (c == nullptr) return;

            const bool before = c->muted;
            const bool after  = !before;

            auto apply = [this, clipUuid](bool m)
            {
                if (auto* cc = m_clipState.findClip(clipUuid))
                {
                    cc->muted = m;
                    cc->bumpWaveformVisualVersion();
                    m_clipState.updateClip(*cc);
                    syncClipToEngine(*cc);
                }
            };
            apply(after);

            m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                after ? "Mute Clip" : "Unmute Clip",
                [apply, after]()  { apply(after);  },
                [apply, before]() { apply(before); },
                /*alreadyApplied*/ true));
        };

        rendererPtr->onPianoRollClick = [this, clipUuid]()
        {
            if (m_engineClipManager != nullptr)
            {
                const auto mapped = m_uuidToEngineId.find(clipUuid);
                if (mapped != m_uuidToEngineId.end())
                {
                    if (auto* engineClip = m_engineClipManager->getClip(mapped->second))
                    {
                        if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(engineClip))
                        {
                            if (onEngineMidiClipDoubleClicked)
                                onEngineMidiClipDoubleClicked(*midiClip);
                        }
                    }
                }
            }
        };

        rendererPtr->onPresetCurveToggle = [this, clipUuid]()
        {
            // The pitch/stretch/mode badges on the clip header double as the
            // preset-curve visibility toggle.
            if (auto* clip = m_clipState.findClip(clipUuid))
                togglePresetCurveVisibility(clip);
        };

        rendererPtr->onClick = [this, clipUuid]()
        {
            // Click = select. The renderer consumes the press for unselected
            // clips (anti-accidental move), so the selection MUST happen here.
            // onClick was never wired, which left every clip unclickable until
            // the renderers stopped intercepting (i.e. the exact inverse of
            // Free View Mode).
            m_selection.selectClip(clipUuid);
            for (auto& r : m_clipRenderers)
                if (r != nullptr)
                    r->setSelected(m_selection.isSelected(r->getClipId()));
            repaint();
        };

        rendererPtr->onEditCommit = [this, clipUuid, preEdit]()
        {
            if (!preEdit->has_value()) return;
            auto* c = m_clipState.findClip(clipUuid);
            if (c == nullptr) return;

            const ArrangementClipModel before = preEdit->value();
            const ArrangementClipModel after  = *c;
            preEdit->reset();

            // Skip recording if nothing actually changed
            if (before.length        == after.length &&
                before.startTime     == after.startTime &&
                before.sourceOffset  == after.sourceOffset &&
                before.sourceStartSample == after.sourceStartSample &&
                before.sourceEndSample   == after.sourceEndSample &&
                before.timePitch.stretchRatio == after.timePitch.stretchRatio &&
                before.fadeInLength  == after.fadeInLength &&
                before.fadeOutLength == after.fadeOutLength &&
                before.fadeInCurve   == after.fadeInCurve &&
                before.fadeOutCurve  == after.fadeOutCurve)
            {
                return;
            }

            // Automatic overlap crossfades are derived prepared state, not
            // manual fade edits.  The old FadePatch path mutated neighbouring
            // fade lengths here, which caused the same overlap to be applied
            // once as a normal fade and again as the equal-power relationship.
            // Keep the persisted/user-configured fade fields untouched and
            // republish only the geometry-derived crossfade plan.
            const ArrangementClipModel afterFinal = after;
            refreshAutoCrossfadesForTrack(afterFinal.trackIndex);

            auto applyState = [this, clipUuid](const ArrangementClipModel& s)
            {
                if (auto* cc = m_clipState.findClip(clipUuid))
                {
                    cc->startTime         = s.startTime;
                    cc->length            = s.length;
                    cc->sourceOffset      = s.sourceOffset;
                    cc->sourceStartSample = s.sourceStartSample;
                    cc->sourceEndSample   = s.sourceEndSample;
                    cc->timePitch.stretchRatio = s.timePitch.stretchRatio;
                    cc->fadeInLength      = s.fadeInLength;
                    cc->fadeOutLength     = s.fadeOutLength;
                    cc->fadeInCurve       = s.fadeInCurve;
                    cc->fadeOutCurve      = s.fadeOutCurve;
                    cc->bumpWaveformVisualVersion();
                    // Guard the engine push so the re-entrant
                    // mirrorEngineClipIntoArrangement() (fired by the engine
                    // setters) cannot clobber the just-applied trim/stretch
                    // state with a stale source window — same hazard as the
                    // live-edit path.
                    juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);
                    m_clipState.updateClip(*cc);
                    syncClipToEngine(*cc);
                }
            };

            const auto refreshAfter = [this, trackIndex = afterFinal.trackIndex]()
            {
                refreshAutoCrossfadesForTrack(trackIndex);
            };
            const auto refreshBefore = [this, trackIndex = before.trackIndex]()
            {
                refreshAutoCrossfadesForTrack(trackIndex);
            };

            m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                "Edit Clip",
                [applyState, refreshAfter, afterFinal]() { applyState(afterFinal); refreshAfter(); },
                [applyState, refreshBefore, before]()     { applyState(before);     refreshBefore(); },
                /*alreadyApplied*/ true));
        };

        auto* addedRenderer = renderer.get();
        addAndMakeVisible(*addedRenderer);
        addedRenderer->setVisible(shouldShowClipRenderer(*addedRenderer));
        if (addedRenderer->isVisible() && addedRenderer->shouldRenderWaveform()
            && addedRenderer->getClip() != nullptr
            && !addedRenderer->getClip()->sourcePath.empty()
            && addedRenderer->needsWaveformUpdate())
            addedRenderer->refresh();
        m_clipRenderers.push_back(std::move(renderer));

        // The pinned toolbar + ruler must stay above every clip lane so a
        // vertically-scrolled clip can never draw over them (or swallow the
        // clicks meant for the toolbar, e.g. the add-track button).
        ensureToolbarZOrder();
    }

    // ── Pinned-strip z-order enforcement (click-through fix) ───────────────
    void ArrangementViewCore::ensureToolbarZOrder()
    {
        // Always keep automation containers above clips
        for (auto& [trackId, container] : m_automationContainers)
            if (container != nullptr && container->isVisible())
                container->toFront(false);

        // Keep the occlusion mask below the actual controls, but above every
        // scrollable clip/automation component.
        m_headerClipGuard.toFront(false);
        // The pinned toolbar + ruler must stay above every clip lane so a
        // vertically-scrolled clip can never draw over them (or swallow the
        // clicks meant for the toolbar, e.g. the add-track button). The
        // interactive overlays stay above the toolbar for drag feedback.
        m_toolbar.toFront(false);
        m_ruler.toFront(false);
        m_pianoRollBtn.toFront(false);
        m_rubberBand.toFront(false);
        m_splitIndicator.toFront(false);
        m_ghostOverlay.toFront(false);
    }

    bool ArrangementViewCore::isAutomationComponentAt(juce::Point<int> pos) const
    {
        for (const auto& [trackId, container] : m_automationContainers)
        {
            if (container == nullptr || !container->isVisible())
                continue;

            const auto local = pos - container->getPosition();
            if (!container->getLocalBounds().contains(local))
                continue;

            for (const auto& parameterId : container->getAllParameterIds())
            {
                if (auto* lane = container->getLane(parameterId))
                {
                    const auto laneLocal = local - lane->getPosition();
                    if (lane->isVisible() && lane->hitTest(laneLocal.x, laneLocal.y))
                        return true;
                }
            }
        }

        return false;
    }

    bool ArrangementViewCore::isAutomationInteractionBlockingClipAt(juce::Point<int> pos) const
    {
        if (m_engineTrackManager == nullptr)
            return false;

        const int laneIndex = (pos.y - arrangementContentTop()) / juce::jmax(1, (int)m_zoom.getTrackHeightPx());
        if (laneIndex < 0)
            return false;

        DAW::Track* track = nullptr;
        if (m_engineTrackManager->hasMasterTrack())
        {
            if (laneIndex == 0)
                track = m_engineTrackManager->getMasterTrack();
            else
                track = m_engineTrackManager->getTrack(laneIndex - 1);
        }
        else
        {
            track = m_engineTrackManager->getTrack(laneIndex);
        }

        if (track == nullptr || !track->isAutomationVisible() || track->isAutomationMuted())
            return false;

        const auto trackId = track->getID();
        const auto activeParameterId = track->getActiveAutomationParameterId().isNotEmpty()
            ? track->getActiveAutomationParameterId()
            : juce::String(DAW::AutomationLaneCore::trackVolumeParameterId);

        if (auto it = m_automationContainers.find(trackId); it != m_automationContainers.end())
            if (it->second != nullptr && it->second->isVisible())
                if (auto* lane = it->second->getLane(activeParameterId))
                    if (lane->isVisible())
                        return true;

        const auto apexId = apexIdForVisibleTarget(trackId, activeParameterId);
        return hasApexAutomationLane(apexId);
    }

    void ArrangementViewCore::updateClipAutomationDim()
    {
        for (auto& renderer : m_clipRenderers)
        {
            auto* clip = renderer->getClip();
            if (clip == nullptr) { renderer->setAutomationActive(false); continue; }

            bool dimmed = false;
            if (m_engineTrackManager != nullptr)
            {
                // Map clip trackIndex to engine track
                const int masterOffset = m_engineTrackManager->hasMasterTrack() ? 1 : 0;
                const int engineIdx    = clip->trackIndex - masterOffset;
                DAW::Track* track = nullptr;
                if (masterOffset > 0 && clip->trackIndex == 0)
                    track = m_engineTrackManager->getMasterTrack();
                else if (engineIdx >= 0 && engineIdx < m_engineTrackManager->getNumTracks())
                    track = m_engineTrackManager->getTrack(engineIdx);

                if (track != nullptr && track->isAutomationVisible() && !track->isAutomationMuted())
                {
                    // Check there is actually a visible container with lanes for this track
                    auto it = m_automationContainers.find(track->getID());
                    if (it != m_automationContainers.end() && it->second != nullptr
                        && it->second->isVisible() && it->second->getNumLanes() > 0)
                        dimmed = true;
                    else
                        dimmed = isAutomationInteractionBlockingClipAt(
                            juce::Point<int>(0, renderer->getY() + renderer->getHeight() / 2));
                }
            }
            renderer->setAutomationActive(dimmed);
        }
    }

    // -----------------------------------------------------------------------
    // Tool state listener
    // -----------------------------------------------------------------------

    void ArrangementViewCore::activeToolChanged(EditorTool newTool)
    {
        DBG("[ArrangementView] Active tool changed to: " << toolName(newTool));
        setMouseCursorForActiveTool();
        repaint();
    }

    void ArrangementViewCore::setMouseCursorForActiveTool()
    {
        switch (m_toolState.getActiveTool())
        {
        case EditorTool::Select:
            setMouseCursor(DAW::CursorThemeCore::getDefaultArrow());
            break;
        case EditorTool::Split:
            setMouseCursor(DAW::CursorThemeCore::getCrosshair());
            break;
        case EditorTool::RazorEdit:
            setMouseCursor(DAW::CursorThemeCore::getDefaultArrow());
            break;
        case EditorTool::Eraser:
            setMouseCursor(DAW::CursorThemeCore::getHand());
            break;
        case EditorTool::Draw:
            setMouseCursor(DAW::CursorThemeCore::getCrosshair());
            break;
        case EditorTool::Glue:
            setMouseCursor(DAW::CursorThemeCore::getHand());
            break;
        case EditorTool::Mute:
            setMouseCursor(DAW::CursorThemeCore::getHand());
            break;
        case EditorTool::Zoom:
            setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::DraggingHandCursor));
            break;
        case EditorTool::TimeScrub:
            setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::LeftRightResizeCursor));
            break;
        }
    }

    void ArrangementViewCore::mouseDown(const juce::MouseEvent& e)
    {
        DBG("[ArrangementView] MouseDown with active tool: " << toolName(m_toolState.getActiveTool()));

        m_lastMousePos = e.getPosition();
        m_lastMouseMods = e.mods;
        if (auto* clip = findClipAt(e.getPosition()))
        {
            m_lastMouseDownSource = "ClipRenderCore";
            m_lastMouseDownClipId = clip->id;
        }
        else
        {
            m_lastMouseDownSource = "ArrangementViewCore";
            m_lastMouseDownClipId = juce::Uuid();
        }

        appendDebugLog("[mouseDown] pos=" + e.getPosition().toString()
            + " mods ctrl=" + juce::String(e.mods.isCtrlDown() ? 1 : 0)
            + " shift=" + juce::String(e.mods.isShiftDown() ? 1 : 0)
            + " alt=" + juce::String(e.mods.isAltDown() ? 1 : 0)
            + " left=" + juce::String(e.mods.isLeftButtonDown() ? 1 : 0)
            + " right=" + juce::String(e.mods.isRightButtonDown() ? 1 : 0)
            + " clipHit=" + juce::String(findClipAt(e.getPosition()) != nullptr ? "yes" : "no")
            + " clipId=" + formatUuidShort(m_lastMouseDownClipId));
        appendDebugLog("[preSelect] coreSelectionCount=" + juce::String(m_selection.getSelectionCount())
            + " visualSelectedCount=" + juce::String(getVisualSelectedClipCount()));

        if (e.mods.isMiddleButtonDown())
        {
            m_middleMousePanActive = true;
            m_middleMousePanScreenPos = e.getScreenPosition();
            setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::DraggingHandCursor));
            return;
        }

        // ── Pinned-strip guard (Quick New Track click-through fix) ────────
        // The toolbar strip is pinned INSIDE the scrollable content at
        // [scrollY, scrollY + 48]. Pointer-down events in the strip belong
        // exclusively to the strip layer (real toolbar children, piano-roll
        // button, lock button, or dead gaps). They must NEVER be interpreted
        // as lane interactions with content scrolled underneath — no track
        // selection, rubber band, clip edit, or automation point from a
        // strip-area press.
        {
            const int stripTop = m_viewportScrollY;
            const int stripBottom = m_viewportScrollY + 48; // toolbar height
            if (e.getPosition().y >= stripTop && e.getPosition().y < stripBottom)
            {
                // The lock button is painted and hit-tested here (it is not
                // a child component); everything else in the strip is owned
                // by the toolbar layer or is an inert gap.
                if (m_viewOnlyBtnBounds.contains(e.getPosition()))
        {
            m_viewOnlyMode = ! m_viewOnlyMode;
            applyViewOnlyMode();
            repaint();
            return;
        }

        // Free View Mode: view only — no selection, no drags, no marquee.
        if (m_viewOnlyMode)
            return;

        // Empty-space gesture setup. Industry model (Logic/Cubasis/GarageBand):
        // a MOUSE drag starts the marquee immediately; a TOUCH drag scrolls the
        // timeline. No hold delay — no DAW does that, it just feels laggy.
        m_gestureDownPos = e.getPosition();
        m_holdFired = false;
        m_gestureMoved = false;
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            m_gestureStartViewX = viewport->getViewPositionX();
            m_gestureStartViewY = viewport->getViewPositionY();
        }

        if (m_lockBtnBounds.contains(e.getPosition()))
                {
                    m_lockHorizontal = !m_lockHorizontal;
                    repaint();
                    return;
                }
                appendDebugLog("[mouseDown] pinned-strip guard: consumed by strip layer");
                return;
            }
        }

        if (m_automationManager != nullptr && m_engineTrackManager != nullptr && m_toolState.getActiveTool() == EditorTool::Draw)
        {
            // Lane container components handle all interaction when visible — let them own it
            if (isAutomationComponentAt(e.getPosition()) || isAutomationInteractionBlockingClipAt(e.getPosition()))
                return;

            const int trackH = juce::jmax(1, (int)m_zoom.getTrackHeightPx());
            const int trackIndex = (e.y - arrangementContentTop()) / trackH;
            const int masterOffset = m_engineTrackManager->hasMasterTrack() ? 1 : 0;
            const int engineTrackIndex = trackIndex - masterOffset;

            if (engineTrackIndex >= 0 && engineTrackIndex < m_engineTrackManager->getNumTracks())
            {
                if (auto* track = m_engineTrackManager->getTrack(engineTrackIndex))
                {
                    if (track->isAutomationVisible() && !track->isAutomationMuted())
                    {
                        const auto activeParameterId = track->getActiveAutomationParameterId().isNotEmpty()
                            ? track->getActiveAutomationParameterId()
                            : juce::String(DAW::AutomationLaneCore::trackVolumeParameterId);
                        const float yTop = (float)(arrangementContentTop() + trackIndex * trackH);
                        const auto laneBounds = juce::Rectangle<float>(0.0f, yTop, (float)getWidth(), (float)trackH).reduced(0.0f, 7.0f);

                        if (laneBounds.contains(e.position))
                        {
                            const float normalised = 1.0f - ((e.position.y - laneBounds.getY()) / juce::jmax(1.0f, laneBounds.getHeight()));
                            const double timeSeconds = juce::jmax(0.0, screenXToTime(e.x));

                            if (isApexAutomationKeyForTrack(track->getID(), activeParameterId))
                            {
                                addOrReplaceApexPoint(activeParameterId, timeSeconds, normalised, m_tempoBpm);
                            }
                            else
                            {
                                m_automationManager->addOrReplacePoint(track->getID(), activeParameterId,
                                    (int64_t)std::llround(timeSeconds * juce::jmax(1.0, m_engineSampleRate)),
                                    valueToAutomationValueForDisplay(activeParameterId, normalised));
                            }

                            repaint();
                            return;
                        }
                    }
                }
            }
        }

        grabKeyboardFocus();

        if (m_toolState.getActiveTool() != EditorTool::Split
            && m_toolState.getActiveTool() != EditorTool::Draw
            && (isAutomationComponentAt(e.getPosition()) || isAutomationInteractionBlockingClipAt(e.getPosition())))
            return;

        switch (m_toolState.getActiveTool())
        {
        case EditorTool::Select:
            handleSelectTool(e);
            break;

        case EditorTool::Split:
            handleSplitTool(e);
            break;

        case EditorTool::RazorEdit:
            m_toolState.setActiveTool(EditorTool::Select);
            handleSelectTool(e);
            break;

        case EditorTool::Eraser:
            handleEraserTool(e);
            break;

        case EditorTool::Draw:
            handleDrawTool(e);
            break;

        case EditorTool::Glue:
            handleGlueTool(e);
            break;

        case EditorTool::Mute:
            handleMuteTool(e);
            break;

        default:
            break;
        }
    }

    void ArrangementViewCore::mouseDrag(const juce::MouseEvent& e)
    {
        // Touch navigation (pan / pinch) works in EVERY mode — Free View's
        // whole purpose is free navigation, so this must come before the
        // view-only guard below.
        if (e.source.getType() == juce::MouseInputSource::InputSourceType::touch)
        {
            if (! m_inertiaTimer)
                m_inertiaTimer = std::make_unique<TouchPanInertiaTimer>(*this);
            if (! m_inertiaTimer->isTimerRunning())
                m_inertiaTimer->startTimerHz(60);
            return;
        }

        // Free View Mode: no clip drags and no marquee (mouse editing only).
        if (m_viewOnlyMode)
            return;

        m_lastMousePos = e.getPosition();
        m_lastMouseMods = e.mods;

        if (m_middleMousePanActive)
        {
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            {
                const auto currentScreenPos = e.getScreenPosition();
                const auto delta = currentScreenPos - m_middleMousePanScreenPos;
                m_middleMousePanScreenPos = currentScreenPos;

                const int maxX = juce::jmax(0, getWidth() - viewport->getViewWidth());
                const int maxY = juce::jmax(0, getHeight() - viewport->getViewHeight());
                viewport->setViewPosition(juce::jlimit(0, maxX, viewport->getViewPositionX() - delta.x),
                                          juce::jlimit(0, maxY, viewport->getViewPositionY() - delta.y));
            }

            return;
        }

        if (m_toolState.getActiveTool() != EditorTool::Split
            && !m_drag.active
            && !m_drag.pending
            && (isAutomationComponentAt(e.getPosition()) || isAutomationInteractionBlockingClipAt(e.getPosition())))
            return;

        if (m_toolState.getActiveTool() == EditorTool::Select)
        {
            if (m_drag.active || m_drag.pending)
            {
                updateClipDrag(e.getPosition());
                return;
            }

            // Input-source split (industry standard): a TOUCH drag scrolls the
            // timeline with the finger; a MOUSE drag runs the marquee below.
            const bool touchDrag =
                e.source.getType() == juce::MouseInputSource::InputSourceType::touch;

            if (touchDrag)
            {
                // ALL touch navigation is sampled by the 60 Hz gesture timer
                // (see applyTouchPanInertia): one finger pans the timeline,
                // two fingers pinch each axis. The raw event stream is too
                // irregular to drive a smooth gesture.
                if (! m_inertiaTimer)
                    m_inertiaTimer = std::make_unique<TouchPanInertiaTimer>(*this);
                if (! m_inertiaTimer->isTimerRunning())
                    m_inertiaTimer->startTimerHz(60);
                return;
            }

            // Rubber band select.  Keep updating while the pointer crosses
            // clips; ClipRenderCore forwards body drags here and the old
            // findClipAt() gate froze the marquee over any clip.
            if (m_rubberBand.isVisible())
                m_rubberBand.updateDrag(e.getPosition());
        }
    }

    void ArrangementViewCore::mouseUp(const juce::MouseEvent& e)
    {
        DBG("[CRASH TRACE] action=mouseUp file=ArrangementViewCore track=" << trackIndexAt(e.getPosition())
            << " clip=" << formatUuidShort(m_lastMouseDownClipId));
        m_lastMousePos = e.getPosition();
        m_lastMouseMods = e.mods;

        // Touch pan finished → start the iOS-style momentum (slow stop).
        if (m_touchScrollActive)
            endTouchPanGesture();

        if (m_middleMousePanActive)
        {
            m_middleMousePanActive = false;
            setMouseCursorForActiveTool();
            return;
        }

        // Pinned-strip guard (mirror of mouseDown): a release inside the
        // strip layer never finalizes lane interactions (rubber band,
        // clip drags, context menus) unless a real drag began on the lanes
        // below and is still active.
        {
            const int stripTop = m_viewportScrollY;
            const int stripBottom = m_viewportScrollY + 48;
            if (e.getPosition().y >= stripTop && e.getPosition().y < stripBottom
                && !m_drag.active && !m_drag.pending)
                return;
        }

        if (m_toolState.getActiveTool() != EditorTool::Split
            && !m_drag.active
            && (isAutomationComponentAt(e.getPosition()) || isAutomationInteractionBlockingClipAt(e.getPosition())))
            return;

        if (m_rubberBand.isVisible())
            m_rubberBand.endDrag();

        // Commit clip drag
        if (m_drag.active)
        {
            if (!e.mods.isRightButtonDown())
                commitClipDrag();
            else
                cancelClipDrag();
        }
        else if (m_drag.pending)
        {
            m_drag.pending = false;
            m_drag.clipIds.clear();
            m_drag.items.clear();
            m_drag.clipId = {};
        }

        // Right-click → clip context menu or empty track menu
        if (e.mods.isRightButtonDown())
        {
            auto* clip = findClipAt(e.getPosition());
            if (clip)
                showClipContextMenu(clip, e.getScreenPosition(), e.getPosition());
            else
                showEmptyTrackContextMenu(e.getScreenPosition(), e.getPosition());
        }
    }

    void ArrangementViewCore::mouseExit(const juce::MouseEvent& e)
    {
        DBG("[CRASH TRACE] action=mouseExit file=ArrangementViewCore track=" << trackIndexAt(e.getPosition())
            << " clip=" << formatUuidShort(m_lastMouseDownClipId));
        if (m_middleMousePanActive)
        {
            m_middleMousePanActive = false;
            setMouseCursorForActiveTool();
        }
        if (m_drag.active)
            cancelClipDrag();
        if (m_rubberBand.isVisible())
            m_rubberBand.endDrag();
    }

    void ArrangementViewCore::mouseMove(const juce::MouseEvent& e)
    {
        m_lastMousePos = e.getPosition();
        m_lastMouseMods = e.mods;

        if (m_middleMousePanActive)
            return;

        // Lock button hover
        const bool wasHovered = m_lockBtnHovered;
        m_lockBtnHovered = m_lockBtnBounds.contains(e.getPosition());
        if (wasHovered != m_lockBtnHovered)
            repaint();

        const bool wasViewOnlyHovered = m_viewOnlyBtnHovered;
        m_viewOnlyBtnHovered = m_viewOnlyBtnBounds.contains(e.getPosition());
        if (wasViewOnlyHovered != m_viewOnlyBtnHovered)
            repaint();

        setMouseCursorForActiveTool();

        if (m_toolState.getActiveTool() == EditorTool::Split)
        {
            auto* clip = findClipAt(e.getPosition());
            if (clip)
            {
                double time = screenXToTime(e.x);
                m_splitIndicator.setPosition(time);
                m_splitIndicator.setVisible(true);

                int x = timeToScreenX(time);
                m_splitIndicator.setBounds(x - 1, e.y - 40, 2, 80);
            }
            else
            {
                m_splitIndicator.setVisible(false);
            }
        }
    }

    void ArrangementViewCore::mouseDoubleClick(const juce::MouseEvent& e)
    {
        // Free View Mode: nothing opens or edits while viewing.
        if (m_viewOnlyMode)
            return;

        DBG("[ArrangementView] mouseDoubleClick pos=" << e.getPosition().toString());

        if (isAutomationComponentAt(e.getPosition()) || isAutomationInteractionBlockingClipAt(e.getPosition()))
            return;

        auto* clip = findClipAt(e.getPosition());
        DBG("[ArrangementView] findClipAt result=" << (clip ? clip->clipName : "null"));

        if (!clip)
            return;

        // MIDI clips may open the piano roll/editor on double-click.
        if (m_engineClipManager != nullptr)
        {
            const auto mapped = m_uuidToEngineId.find(clip->id);
            if (mapped != m_uuidToEngineId.end())
            {
                if (auto* engineClip = m_engineClipManager->getClip(mapped->second))
                {
                    if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(engineClip))
                    {
                        if (onEngineMidiClipDoubleClicked)
                        {
                            onEngineMidiClipDoubleClicked(*midiClip);
                            return;
                        }
                    }
                    else if (auto* audioClip = dynamic_cast<DAW::AudioClip*>(engineClip))
                    {
                        if (onEngineClipDoubleClicked)
                        {
                            onEngineClipDoubleClicked(*audioClip);
                            return;
                        }
                    }
                }
            }
        }

        // Default: open clip properties window
        if (m_propertiesWindow)
        {
            DBG("[ArrangementView] opening properties for clip: " << clip->clipName);
            m_propertiesWindow->openForClip(clip);
        }
    }

    void ArrangementViewCore::mouseWheelMove(const juce::MouseEvent& e,
                                              const juce::MouseWheelDetails& wheel)
    {
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            if (e.mods.isCtrlDown())
            {
                // Ctrl+scroll = horizontal zoom, anchored to mouse position
                const double oldPps = m_zoom.getPixelsPerSecond();
                const double anchorTime = m_zoom.xToTime((double)e.x);
                const int mouseXInViewport = e.x - viewport->getViewPositionX();
                const double zoomFactor = std::pow(1.2, (double)wheel.deltaY * 2.0);
                const double newPps = juce::jlimit(10.0, 4000.0, oldPps * zoomFactor);

                m_zoom.setPixelsPerSecond(newPps);

                const int maxX = juce::jmax(0, getWidth() - viewport->getViewWidth());
                const int anchoredViewX = juce::roundToInt(anchorTime * newPps) - mouseXInViewport;
                viewport->setViewPosition(juce::jlimit(0, maxX, anchoredViewX), viewport->getViewPositionY());
                return;
            }

            if (e.mods.isAltDown())
            {
                // Alt+scroll = vertical zoom (track/lane height), anchored to mouse Y
                const double oldH = (double)m_zoom.getTrackHeightPx();
                const double anchorTrackY = (double)(e.y);  // content-local Y
                const int mouseYInViewport = e.y - viewport->getViewPositionY();
                const double zoomFactor = std::pow(1.2, (double)wheel.deltaY * 2.0);
                const double newH = juce::jlimit(24.0, 240.0, oldH * zoomFactor);

                m_zoom.setTrackHeightPx(newH);

                // Keep the track under the cursor visually stable
                const double scale = newH / juce::jmax(1.0, oldH);
                const int maxY = juce::jmax(0, getHeight() - viewport->getViewHeight());
                const int anchoredViewY = juce::roundToInt(anchorTrackY * scale) - mouseYInViewport;
                viewport->setViewPosition(viewport->getViewPositionX(), juce::jlimit(0, maxY, anchoredViewY));
                return;
            }

            // Plain scroll
            const int dx = juce::roundToInt((wheel.deltaX != 0.0f ? -wheel.deltaX : 0.0f) * 120.0f);
            const int dy = juce::roundToInt((wheel.deltaY != 0.0f ? -wheel.deltaY : 0.0f) * 120.0f);
            const int maxX = juce::jmax(0, getWidth() - viewport->getViewWidth());
            const int maxY = juce::jmax(0, getHeight() - viewport->getViewHeight());
            viewport->setViewPosition(juce::jlimit(0, maxX, viewport->getViewPositionX() + dx),
                                      juce::jlimit(0, maxY, viewport->getViewPositionY() + dy));
        }
    }

    bool ArrangementViewCore::keyPressed(const juce::KeyPress& key)
    {
        if (key == juce::KeyPress('d', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0))
        {
            toggleArrangementDebugPanel();
            return true;
        }

        // Undo/redo
        if ((key.getKeyCode() == 'y' || key.getKeyCode() == 'Y') && key.getModifiers().isCtrlDown())
        {
            DAW::ActionManager::getInstance().dispatch(DAW::ActionID::EditRedo);
            return true;
        }

        if ((key.getKeyCode() == 'z' || key.getKeyCode() == 'Z') && key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown())
        {
            DAW::ActionManager::getInstance().dispatch(DAW::ActionID::EditRedo);
            return true;
        }

        if ((key.getKeyCode() == 'z' || key.getKeyCode() == 'Z') && key.getModifiers().isCtrlDown())
        {
            DAW::ActionManager::getInstance().dispatch(DAW::ActionID::EditUndo);
            return true;
        }

        char c = (char)std::toupper(key.getTextCharacter());

        // Tool shortcuts are single-key only; modified shortcuts belong to the command layer.
        if (!key.getModifiers().isAnyModifierKeyDown())
        {
            if (c == 'S') { m_toolState.setActiveTool(EditorTool::Select); return true; }
            if (c == 'B') { m_toolState.setActiveTool(EditorTool::Split); return true; }
            if (c == 'R') { m_toolState.setActiveTool(EditorTool::Select); return true; }
            if (c == 'E') { m_toolState.setActiveTool(EditorTool::Eraser); return true; }
            if (c == 'D') { m_toolState.setActiveTool(EditorTool::Draw); return true; }
            if (c == 'G') { m_toolState.setActiveTool(EditorTool::Glue); return true; }
            if (c == 'M') { m_toolState.setActiveTool(EditorTool::Mute); return true; }
            if (c == 'Z') { m_toolState.setActiveTool(EditorTool::Zoom); return true; }
            if (c == 'T') { m_toolState.setActiveTool(EditorTool::TimeScrub); return true; }
        }

        // Copy / Cut / Paste / Duplicate
        if (key == juce::KeyPress('c', juce::ModifierKeys::ctrlModifier, 0))
        {
            copySelectedClipsToClipboard(false);
            return true;
        }
        if (key == juce::KeyPress('x', juce::ModifierKeys::ctrlModifier, 0))
        {
            copySelectedClipsToClipboard(true);
            return true;
        }
        if (key == juce::KeyPress('v', juce::ModifierKeys::ctrlModifier, 0))
        {
            // Paste the whole clipboard group right after the group's original
            // span, preserving relative positions and tracks.
            if (!m_clipboardClips.empty())
            {
                double groupEnd = 0.0;
                int    anchorTrack = std::numeric_limits<int>::max();
                for (const auto& c : m_clipboardClips)
                {
                    groupEnd = juce::jmax(groupEnd, c.endTime());
                    anchorTrack = juce::jmin(anchorTrack, c.trackIndex);
                }
                onClipPaste(groupEnd, anchorTrack);
            }
            return true;
        }
        if (key == juce::KeyPress('d', juce::ModifierKeys::ctrlModifier, 0))
        {
            duplicateSelectedClips();
            return true;
        }

        // Delete selected clips
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            purgeInvalidSelectedClips();
            auto selected = m_selection.getSelectedIds();

            // Snapshot every clip so the whole multi-delete is one undo step.
            std::vector<ArrangementClipModel> snapshots;
            for (auto& id : selected)
                if (auto* c = m_clipState.findClip(id))
                    snapshots.push_back(*c);

            if (!snapshots.empty())
            {
                m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                    snapshots.size() > 1 ? "Delete Clips" : "Delete Clip",
                    [this, snapshots]()
                    {
                        for (const auto& s : snapshots)
                            m_clipState.removeClip(s.id);
                        m_selection.deselectAll();
                    },
                    [this, snapshots]()
                    {
                        m_selection.deselectAll();
                        for (const auto& s : snapshots)
                        {
                            m_clipState.addClip(s);
                            m_selection.selectClip(s.id);
                        }
                    }));
            }
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // Tool handlers
    // -----------------------------------------------------------------------

    void ArrangementViewCore::handleSelectTool(const juce::MouseEvent& e)
    {
        auto* clip = findClipAt(e.getPosition());

        if (clip)
        {
            if (e.mods.isRightButtonDown())
                return; // handled in mouseUp

            const bool isDuplicate = e.mods.isAltDown();

            if (isDuplicate)
            {
                // Alt+drag duplicates the current selection; do not toggle selection here.
                if (!m_selection.isSelected(clip->id))
                    m_selection.selectClip(clip->id);
            }
            else if (e.mods.isCtrlDown())
            {
                m_selection.toggleClip(clip->id);
            }
            else if (!m_selection.isSelected(clip->id))
            {
                m_selection.deselectAll();
                m_selection.selectClip(clip->id);
            }

            // Begin drag — only on left button
            if (!e.mods.isRightButtonDown())
                startClipDrag(clip, e.getPosition(), e.mods);

            juce::StringArray selected;
            for (const auto& id : m_selection.getSelectedIds())
                selected.add(formatUuidShort(id));
            appendDebugLog("[postSelect] coreSelectionCount=" + juce::String(m_selection.getSelectionCount())
                + " selectedIds=" + selected.joinIntoString(","));

            if (m_engineClipManager != nullptr && onEngineClipSelected)
            {
                const auto mapped = m_uuidToEngineId.find(clip->id);
                if (mapped != m_uuidToEngineId.end())
                    if (auto* engineClip = m_engineClipManager->getClip(mapped->second))
                        onEngineClipSelected(*engineClip);
            }
        }
        else
        {
            // Marquee for MOUSE input only: one finger on touch pans the
            // timeline (see the gesture timer).
            if (e.source.getType() != juce::MouseInputSource::InputSourceType::touch)
            {
                if (!e.mods.isCtrlDown())
                    m_selection.deselectAll();

                m_marqueeHitGeometry.clear();
                m_marqueeHitGeometry.reserve(m_clipRenderers.size());
                for (const auto& renderer : m_clipRenderers)
                    if (renderer != nullptr)
                        m_marqueeHitGeometry.emplace_back(renderer->getClipId(), renderer->getBounds());

                m_rubberBand.startDrag(e.getPosition());
                if (onEngineClipSelectionCleared)
                    onEngineClipSelectionCleared();
            }
        }
    }

    void ArrangementViewCore::handleSplitTool(const juce::MouseEvent& e)
    {
        auto* clip = findClipAt(e.getPosition());
        if (!clip) return;

        const auto clipSnapshot = *clip;
        const double splitTime = screenXToTime(e.x);

        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ArrangementViewCore>(this), clipSnapshot, splitTime]() mutable
        {
            if (safe != nullptr)
                safe->performSplit(clipSnapshot, splitTime);
        });
    }

    void ArrangementViewCore::performSplit(const ArrangementClipModel& clipSnapshot, double splitTime)
    {
        auto* clip = m_clipState.findClip(clipSnapshot.id);
        if (clip == nullptr)
            return;

        const auto splitResult = ClipSplitCore::splitClip(*clip, splitTime);
        if (!splitResult.valid)
            return;

        struct SplitUndoContext
        {
            ArrangementClipModel origVisual;
            ArrangementClipModel leftVisual;
            ArrangementClipModel rightVisual;
            std::set<juce::Uuid> preSelection;
            juce::ValueTree origEngineState;
            juce::ValueTree leftEngineState;
            juce::ValueTree rightEngineState;
            juce::ValueTree origAutomationState;
            DAW::ClipRegionPluginCore::ClipStateSnapshot origPluginState;
            DAW::AudioFileManager::CachedAudioHandle audioSnapshot;
            DAW::ClipID origEngineId;
            DAW::ClipID rightEngineId;
            int64_t splitSample = 0;
            bool hasEngine = false;
        };

        const auto origUuid = clip->id;
        auto ctx = std::make_shared<SplitUndoContext>();
        ctx->origVisual = *clip;
        ctx->leftVisual = splitResult.left;
        // The left fragment retains the original arrangement identity.  The
        // right fragment keeps the one generated by ClipSplitCore and is
        // retained in the command context for deterministic Redo.
        ctx->leftVisual.id = origUuid;
        ctx->rightVisual = splitResult.right;
        ctx->preSelection = m_selection.getSelectedIds();

        const bool partialEngineBridge = (m_engineClipManager != nullptr) != (m_engineAudioFiles != nullptr);
        if (partialEngineBridge)
            return;

        if (m_engineClipManager != nullptr && m_engineAudioFiles != nullptr)
        {
            const auto mapIt = m_uuidToEngineId.find(origUuid);
            if (mapIt == m_uuidToEngineId.end())
                return;

            auto* original = m_engineClipManager->getClip(mapIt->second);
            if (original == nullptr)
                return;

            ctx->hasEngine = true;
            ctx->origEngineId = mapIt->second;
            ctx->origEngineState = original->getState();
            ctx->audioSnapshot = m_engineAudioFiles->getCachedAudioSnapshot(ctx->origEngineId);

            if (m_clipRegionPlugins != nullptr)
            {
                ctx->origPluginState = m_clipRegionPlugins->captureClipState(ctx->origEngineId);
                std::vector<juce::String> pluginInstanceIds;
                pluginInstanceIds.reserve(ctx->origPluginState.size());
                for (const auto& entry : ctx->origPluginState)
                    pluginInstanceIds.push_back(entry.instanceId);

                if (m_automationManager != nullptr)
                    ctx->origAutomationState = m_automationManager->captureClipAutomationState(
                        ctx->origEngineId, pluginInstanceIds);
            }
            else if (m_automationManager != nullptr)
            {
                ctx->origAutomationState = m_automationManager->captureClipAutomationState(
                    ctx->origEngineId);
            }

            auto leftState = ctx->origEngineState.createCopy();
            auto rightState = ctx->origEngineState.createCopy();
            auto* audioClip = dynamic_cast<DAW::AudioClip*>(original);

            const auto origStart = original->getStartPosition();
            const auto origLength = original->getLength();
            const auto origTimelineLength = audioClip != nullptr
                ? audioClip->getProcessedTimelineLength()
                : origLength;
            const auto origSourceOffset = original->getSourceOffset();
            const double sourceRate = juce::jmax(1.0,
                m_engineAudioFiles->getSourceSampleRate(ctx->origEngineId));
            const auto sourceTotalSamples = m_engineAudioFiles->getSourceNumSamples(ctx->origEngineId);
            const double totalTimelineSeconds = juce::jmax(0.000001, clip->visualLength());
            const double leftTimelineSeconds = juce::jmax(0.0, splitTime - clip->startTime);
            const double splitFraction = juce::jlimit(0.0, 1.0,
                                                       leftTimelineSeconds / totalTimelineSeconds);

            const auto leftEngineLength = (DAW::SamplePosition)std::llround(
                (double)origLength * splitFraction);
            const auto rightEngineLength = origLength - leftEngineLength;
            const auto leftTimelineLength = (DAW::SamplePosition)std::llround(
                (double)origTimelineLength * splitFraction);
            const auto splitSample = origStart + leftTimelineLength;
            const auto rightSourceOffset = origSourceOffset
                + (DAW::SamplePosition)std::llround(
                    (double)leftEngineLength * (sourceRate / juce::jmax(1.0, m_engineSampleRate)));

            int64_t leftSourceStart = splitResult.left.sourceStartSample;
            int64_t leftSourceEnd = splitResult.left.sourceEndSample;
            int64_t rightSourceStart = splitResult.right.sourceStartSample;
            int64_t rightSourceEnd = splitResult.right.sourceEndSample;
            if (leftSourceEnd <= leftSourceStart || rightSourceEnd <= rightSourceStart)
            {
                leftSourceStart = juce::jmax<int64_t>(0, (int64_t)origSourceOffset);
                rightSourceStart = juce::jmax<int64_t>(leftSourceStart + 1,
                                                       (int64_t)rightSourceOffset);
                rightSourceEnd = juce::jmax<int64_t>(rightSourceStart + 1,
                    leftSourceStart + (int64_t)std::llround(
                        juce::jmax(0.0, clip->length) * sourceRate));
                if (sourceTotalSamples > 0)
                    rightSourceEnd = juce::jlimit<int64_t>(rightSourceStart + 1,
                                                           (int64_t)sourceTotalSamples,
                                                           rightSourceEnd);
                leftSourceEnd = rightSourceStart;
            }

            leftState.setProperty("length", (juce::int64)leftEngineLength, nullptr);
            rightState.setProperty("startPosition", (juce::int64)splitSample, nullptr);
            rightState.setProperty("length", (juce::int64)rightEngineLength, nullptr);
            rightState.setProperty("sourceOffset", (juce::int64)rightSourceOffset, nullptr);
            if (audioClip != nullptr)
            {
                leftState.setProperty("sourceStartSample", (juce::int64)leftSourceStart, nullptr);
                leftState.setProperty("sourceEndSample", (juce::int64)leftSourceEnd, nullptr);
                rightState.setProperty("sourceStartSample", (juce::int64)rightSourceStart, nullptr);
                rightState.setProperty("sourceEndSample", (juce::int64)rightSourceEnd, nullptr);
            }
            if (leftState.hasProperty("fadeOutLength"))
                leftState.setProperty("fadeOutLength", (juce::int64)0, nullptr);
            if (rightState.hasProperty("fadeInLength"))
                rightState.setProperty("fadeInLength", (juce::int64)0, nullptr);

            ctx->leftEngineState = leftState;
            ctx->rightEngineState = rightState;
            ctx->splitSample = (int64_t)splitSample;
        }
        else if (m_automationManager != nullptr)
        {
            ctx->origAutomationState = m_automationManager->captureClipAutomationState(
                origUuid.toString());
            ctx->splitSample = (int64_t)std::llround(
                splitTime * juce::jmax(1.0, m_engineSampleRate));
        }

        const auto restoreVisual = [this, ctx]()
        {
            juce::ScopedValueSetter<bool> suppressEngineCreation(m_suppressEngineClipCreation, true);
            m_clipState.beginBatch();
            if (m_clipState.findClip(ctx->rightVisual.id) != nullptr)
                m_clipState.removeClip(ctx->rightVisual.id);
            if (m_clipState.findClip(ctx->origVisual.id) != nullptr)
                m_clipState.updateClip(ctx->origVisual);
            else
                m_clipState.addClip(ctx->origVisual);
            m_clipState.endBatch();
            m_selection.setSelection(ctx->preSelection);
            rebuildClipRenderers();
        };

        const auto applyVisual = [this, ctx]()
        {
            juce::ScopedValueSetter<bool> suppressEngineCreation(m_suppressEngineClipCreation, true);
            m_clipState.beginBatch();
            if (m_clipState.findClip(ctx->origVisual.id) != nullptr)
                m_clipState.updateClip(ctx->leftVisual);
            else
                m_clipState.addClip(ctx->leftVisual);
            if (m_clipState.findClip(ctx->rightVisual.id) != nullptr)
                m_clipState.updateClip(ctx->rightVisual);
            else
                m_clipState.addClip(ctx->rightVisual);
            m_clipState.endBatch();

            // Keep the left selection identity stable.  The right fragment is
            // not implicitly selected, matching the pre-split selection set.
            m_selection.setSelection(ctx->preSelection);
            rebuildClipRenderers();
        };

        std::function<bool()> restoreOriginal;
        restoreOriginal = [this, ctx, restoreVisual]()
        {
            bool success = true;

            if (ctx->hasEngine)
            {
                juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);

                if (m_clipRegionPlugins != nullptr)
                    m_clipRegionPlugins->removeAllEntriesForClip(ctx->rightEngineId);

                if (ctx->rightEngineId.isNotEmpty()
                    && ctx->rightEngineId != ctx->origEngineId)
                {
                    if (m_engineClipManager->getClip(ctx->rightEngineId) != nullptr
                        && !m_engineClipManager->deleteClip(ctx->rightEngineId))
                        success = false;
                    m_engineAudioFiles->unloadClip(ctx->rightEngineId);
                }

                auto* original = m_engineClipManager->getClip(ctx->origEngineId);
                if (original == nullptr)
                    original = m_engineClipManager->recreateClipFromStateWithId(
                        ctx->origEngineState, ctx->origEngineId);

                if (original == nullptr)
                {
                    success = false;
                }
                else
                {
                    original->restoreState(ctx->origEngineState);
                    m_uuidToEngineId[ctx->origVisual.id] = ctx->origEngineId;

                    if (m_clipRegionPlugins != nullptr)
                    {
                        if (m_pluginFormatManager != nullptr)
                            success = m_clipRegionPlugins->restoreClipState(
                                ctx->origEngineId, ctx->origPluginState,
                                *m_pluginFormatManager) && success;
                        else if (!ctx->origPluginState.empty())
                            success = false;
                        else
                            m_clipRegionPlugins->removeAllEntriesForClip(ctx->origEngineId);
                    }
                }

                m_uuidToEngineId.erase(ctx->rightVisual.id);
                if (ctx->audioSnapshot != nullptr && original != nullptr
                    && m_engineAudioFiles->getCachedAudioSnapshot(ctx->origEngineId) == nullptr)
                    m_engineAudioFiles->shareSnapshotForClip(ctx->audioSnapshot, ctx->origEngineId);
            }

            if (m_automationManager != nullptr && ctx->origAutomationState.isValid())
            {
                const auto originalAutomationId = ctx->hasEngine
                    ? ctx->origEngineId : ctx->origVisual.id.toString();
                const auto rightAutomationId = ctx->hasEngine
                    ? ctx->rightEngineId : ctx->rightVisual.id.toString();
                success = m_automationManager->restoreClipAutomationState(
                    ctx->origAutomationState,
                    { originalAutomationId, rightAutomationId }) && success;
            }

            restoreVisual();
            return success;
        };

        const auto applyVisualAndPublish = [this, ctx, applyVisual]()
        {
            applyVisual();
            if (ctx->hasEngine)
                m_uuidToEngineId[ctx->rightVisual.id] = ctx->rightEngineId;
        };

        const auto shouldFail = [this](const juce::String& stage)
        {
            return splitTransactionFailureInjector != nullptr
                && splitTransactionFailureInjector(stage);
        };

        std::function<bool(const DAW::ClipID&)> applySplit;
        applySplit = [this, ctx, restoreOriginal, applyVisualAndPublish, shouldFail](const DAW::ClipID& requestedRightId)
        {
            if (!ctx->hasEngine)
            {
                if (m_automationManager != nullptr && ctx->origAutomationState.isValid())
                    if (!m_automationManager->applySplitClipAutomation(
                            ctx->origAutomationState,
                            ctx->origVisual.id.toString(),
                            ctx->rightVisual.id.toString(),
                            ctx->splitSample))
                    {
                        (void)restoreOriginal();
                        return false;
                    }
                applyVisualAndPublish();
                return true;
            }

            bool success = true;
            juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);

            if (ctx->rightEngineId.isNotEmpty()
                && ctx->rightEngineId != ctx->origEngineId)
            {
                if (m_clipRegionPlugins != nullptr)
                    m_clipRegionPlugins->removeAllEntriesForClip(ctx->rightEngineId);
                if (m_engineClipManager->getClip(ctx->rightEngineId) != nullptr
                    && !m_engineClipManager->deleteClip(ctx->rightEngineId))
                    success = false;
                m_engineAudioFiles->unloadClip(ctx->rightEngineId);
            }

            auto* original = m_engineClipManager->getClip(ctx->origEngineId);
            if (original == nullptr)
                original = m_engineClipManager->recreateClipFromStateWithId(
                    ctx->origEngineState, ctx->origEngineId);

            if (original == nullptr)
                success = false;

            DAW::Clip* right = nullptr;
            if (success)
            {
                original->restoreState(ctx->leftEngineState);

                if (m_clipRegionPlugins != nullptr)
                {
                    if (m_pluginFormatManager != nullptr)
                        success = m_clipRegionPlugins->restoreClipState(
                            ctx->origEngineId, ctx->origPluginState,
                            *m_pluginFormatManager);
                    else if (!ctx->origPluginState.empty())
                        success = false;
                    else
                        m_clipRegionPlugins->removeAllEntriesForClip(ctx->origEngineId);
                }

                const auto targetRightId = requestedRightId.isNotEmpty()
                    ? requestedRightId
                    : ctx->rightEngineId;
                right = m_engineClipManager->recreateClipFromStateWithId(
                    ctx->rightEngineState, targetRightId);
                if (right == nullptr)
                    success = false;
                else if (ctx->rightEngineId.isEmpty())
                    ctx->rightEngineId = right->getID();
                else if (right->getID() != ctx->rightEngineId)
                    success = false;
            }

            if (success && ctx->audioSnapshot != nullptr)
                m_engineAudioFiles->shareSnapshotForClip(ctx->audioSnapshot, ctx->rightEngineId);

            if (success && shouldFail("engine"))
                success = false;

            std::vector<std::pair<juce::String, juce::String>> pluginInstanceMap;
            if (success && m_clipRegionPlugins != nullptr)
            {
                if (m_pluginFormatManager != nullptr)
                    success = m_clipRegionPlugins->restoreClipState(
                        ctx->rightEngineId, ctx->origPluginState,
                        *m_pluginFormatManager);
                else if (!ctx->origPluginState.empty())
                    success = false;
                else
                    m_clipRegionPlugins->removeAllEntriesForClip(ctx->rightEngineId);

                if (success)
                    pluginInstanceMap = m_clipRegionPlugins->getInstanceIdMapping(
                        ctx->origPluginState, ctx->rightEngineId);
            }

            if (success && shouldFail("plugins"))
                success = false;

            if (success && m_automationManager != nullptr && ctx->origAutomationState.isValid())
            {
                success = m_automationManager->applySplitClipAutomation(
                    ctx->origAutomationState,
                    ctx->origEngineId,
                    ctx->rightEngineId,
                    ctx->splitSample,
                    pluginInstanceMap);
            }

            if (success && shouldFail("automation"))
                success = false;

            if (success)
            {
                m_uuidToEngineId[ctx->origVisual.id] = ctx->origEngineId;
                m_uuidToEngineId[ctx->rightVisual.id] = ctx->rightEngineId;
                if (shouldFail("maps"))
                {
                    (void)restoreOriginal();
                    return false;
                }
                applyVisualAndPublish();
                return true;
            }

            (void)restoreOriginal();
            return false;
        };

        if (!applySplit({}))
            return;

        const auto redoFn = [applySplit, restoreOriginal, ctx]() mutable
        {
            if (!applySplit(ctx->rightEngineId))
                (void)restoreOriginal();
        };
        const auto undoFn = [restoreOriginal]() mutable
        {
            (void)restoreOriginal();
        };

        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            "Split Clip", redoFn, undoFn, /*alreadyApplied*/ true));
        repaint();
    }

    void ArrangementViewCore::handleEraserTool(const juce::MouseEvent& e)
    {
        auto* clip = findClipAt(e.getPosition());
        if (clip)
        {
            const ArrangementClipModel snapshot = *clip;
            m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                "Erase Clip",
                [this, snapshot]() { m_selection.deselectClip(snapshot.id); ClipEraserCore::eraseClip(m_clipState, snapshot.id); },
                [this, snapshot]() { m_clipState.addClip(snapshot); m_selection.deselectAll(); m_selection.selectClip(snapshot.id); }));
        }
    }

    void ArrangementViewCore::handleDrawTool(const juce::MouseEvent& e)
    {
        if (e.mods.isRightButtonDown())
            return;

        if (isAutomationComponentAt(e.getPosition()) || isAutomationInteractionBlockingClipAt(e.getPosition()))
            return;

        // Calculate which track was clicked
        int toolbarH = 48;
        int rulerH = 32;
        int trackH = (int)m_zoom.getTrackHeightPx();
        int trackIndex = (e.y - toolbarH - rulerH) / trackH;

        if (trackIndex < 0 || trackIndex >= 3) return;

        double startTime = screenXToTime(e.x);
        double length = 4.0; // default 4 seconds

        auto newClip = ArrangementClipModel::createNew(trackIndex, startTime, length);
        newClip.clipName = "New Clip";
        newClip.colour = juce::Colour(0xFFFF1678);

        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            "Draw Clip",
            [this, newClip]() { m_clipState.addClip(newClip); },
            [this, newClip]() { m_selection.deselectClip(newClip.id); m_clipState.removeClip(newClip.id); }));
    }

    void ArrangementViewCore::handleGlueTool(const juce::MouseEvent& e)
    {
        auto* clip = findClipAt(e.getPosition());
        if (!clip) return;

        // Find adjacent clip on the same track
        ArrangementClipModel* adjacent = nullptr;
        double minDist = 0.1; // max gap tolerance

        for (auto& c : m_clipState.allClips())
        {
            if (c.id == clip->id) continue;
            if (c.trackIndex != clip->trackIndex) continue;

            double gap = std::abs(c.startTime - clip->endTime());
            if (gap < minDist)
            {
                minDist = gap;
                adjacent = const_cast<ArrangementClipModel*>(&c);
            }
        }

        if (adjacent)
        {
            const ArrangementClipModel leftSnap  = *clip;
            const ArrangementClipModel rightSnap = *adjacent;

            auto result = ClipGlueCore::glueClips(*clip, *adjacent);
            if (result.valid)
            {
                const ArrangementClipModel merged = result.merged;
                m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                    "Glue Clips",
                    [this, leftSnap, rightSnap, merged]()
                    {
                        m_selection.deselectAll();
                        m_clipState.removeClip(leftSnap.id);
                        m_clipState.removeClip(rightSnap.id);
                        m_clipState.addClip(merged);
                        m_selection.selectClip(merged.id);
                    },
                    [this, leftSnap, rightSnap, merged]()
                    {
                        m_selection.deselectAll();
                        m_clipState.removeClip(merged.id);
                        m_clipState.addClip(leftSnap);
                        m_clipState.addClip(rightSnap);
                    }));
            }
        }
    }

    void ArrangementViewCore::handleMuteTool(const juce::MouseEvent& e)
    {
        auto* clip = findClipAt(e.getPosition());
        if (clip)
        {
            const bool before = clip->muted;
            const bool after  = !before;
            const juce::Uuid id = clip->id;

            auto apply = [this, id](bool m)
            {
                if (auto* cc = m_clipState.findClip(id))
                {
                    cc->muted = m;
                    cc->bumpWaveformVisualVersion();
                    m_clipState.updateClip(*cc);
                    syncClipToEngine(*cc);
                }
            };

            m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                after ? "Mute Clip" : "Unmute Clip",
                [apply, after]()  { apply(after);  },
                [apply, before]() { apply(before); }));
        }
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    juce::Rectangle<int> ArrangementViewCore::getViewportVisibleBounds() const
    {
        int viewW = getWidth();
        int viewH = getHeight();
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            viewW = juce::jmax(1, viewport->getViewWidth());
            viewH = juce::jmax(1, viewport->getViewHeight());
        }

        return { m_viewportScrollX, m_viewportScrollY, viewW, viewH };
    }

    bool ArrangementViewCore::shouldShowClipRenderer(const ClipRenderCore& renderer) const
    {
        // Keep active edits alive if the user drags an item across a viewport
        // edge.  This is visual culling only; the authoritative clip model is
        // never removed or changed by visibility.
        if (m_drag.active || renderer.isEditGestureActive())
            return true;

        const int trackH = juce::jmax(1, (int)m_zoom.getTrackHeightPx());
        const int overscanY = juce::jmax(ArrangementClipLodPolicy::kViewportOverscanPx, trackH);
        const auto renderRegion = getViewportVisibleBounds().expanded(
            ArrangementClipLodPolicy::kViewportOverscanPx, overscanY);
        return renderRegion.intersects(renderer.getBounds());
    }

    void ArrangementViewCore::updateClipRendererVisibility()
    {
        for (auto& renderer : m_clipRenderers)
        {
            if (renderer == nullptr)
                continue;

            const bool wasVisible = renderer->isVisible();
            const bool shouldBeVisible = shouldShowClipRenderer(*renderer);
            if (m_inViewportScroll)
            {
                TimelinePaintMetrics::inc(TimelinePaintMetrics::arrangementScrollClipIterations);
                if (wasVisible != shouldBeVisible)
                    TimelinePaintMetrics::inc(TimelinePaintMetrics::arrangementScrollVisibilityChanges);
            }
            if (wasVisible != shouldBeVisible)
                renderer->setVisible(shouldBeVisible);

            if (TimelinePaintMetrics::active.load(std::memory_order_relaxed)
                && !shouldBeVisible)
                TimelinePaintMetrics::clipsCulled.fetch_add(1);

            // Waveform work is activated only after the renderer enters the
            // visible/overscanned region and only when the current LOD needs it.
            // Tiny or non-audio clips therefore never start a cache worker just
            // because their renderer exists.
            if (shouldBeVisible && renderer->shouldRenderWaveform()
                && renderer->getClip() != nullptr
                && !renderer->getClip()->sourcePath.empty()
                && renderer->needsWaveformUpdate())
            {
                if (m_inViewportScroll)
                    TimelinePaintMetrics::inc(TimelinePaintMetrics::arrangementScrollWaveformRefreshes);
                renderer->refresh();
            }
        }
    }

    void ArrangementViewCore::updateRubberBandSelection(const juce::Rectangle<int>& bounds)
    {
        std::set<juce::Uuid> selectedIds;
        if (!m_marqueeHitGeometry.empty())
        {
            for (const auto& entry : m_marqueeHitGeometry)
                if (bounds.intersects(entry.second))
                    selectedIds.insert(entry.first);
        }
        else
        {
            for (const auto& renderer : m_clipRenderers)
                if (renderer != nullptr && bounds.intersects(renderer->getBounds()))
                    selectedIds.insert(renderer->getClipId());
        }

        // ArrangementSelectionCore suppresses notification when the
        // intersected set has not changed, keeping stationary marquee updates
        // out of the renderer/repaint hot path.
        m_selection.setSelection(selectedIds);
    }

    ClipRenderCore* ArrangementViewCore::findClipRendererAt(juce::Point<int> pos)
    {
        for (auto& renderer : m_clipRenderers)
        {
            if (renderer->getBounds().contains(pos))
                return renderer.get();
        }
        return nullptr;
    }

    ClipRenderCore* ArrangementViewCore::findClipRendererFor(const juce::Uuid& clipId) const
    {
        for (auto& renderer : m_clipRenderers)
            if (renderer->getClipId() == clipId)
                return renderer.get();
        return nullptr;
    }

    void ArrangementViewCore::refreshAutoCrossfadesForTrack(int trackIndex)
    {
        if (m_engineClipManager == nullptr || trackIndex < 0)
            return;

        struct PendingPlan
        {
            const ArrangementClipModel* model = nullptr;
            DAW::SoundEngine::ClipCrossfadeState state;
        };

        std::vector<PendingPlan> plans;
        for (auto* model : m_clipState.clipsOnTrack(trackIndex))
        {
            if (model == nullptr)
                continue;

            const auto mapped = m_uuidToEngineId.find(model->id);
            if (mapped == m_uuidToEngineId.end())
                continue;

            if (dynamic_cast<DAW::AudioClip*>(m_engineClipManager->getClip(mapped->second)) == nullptr)
                continue;

            plans.push_back({ model, {} });
        }

        // Crossfade ownership is deterministic: clips are ordered by timeline
        // start, then stable UUID.  Only adjacent audio clips on a track form a
        // relationship, so three-way overlaps cannot create ambiguous fan-in.
        std::sort(plans.begin(), plans.end(), [](const PendingPlan& a, const PendingPlan& b)
        {
            if (a.model->startTime != b.model->startTime)
                return a.model->startTime < b.model->startTime;
            return a.model->id.toString() < b.model->id.toString();
        });

        const double sampleRate = juce::jmax(1.0, m_engineSampleRate);
        const auto toTimelineSample = [sampleRate](double seconds) noexcept -> DAW::SamplePosition
        {
            return (DAW::SamplePosition)std::llround(juce::jmax(0.0, seconds) * sampleRate);
        };

        for (size_t i = 0; i + 1 < plans.size(); ++i)
        {
            const auto& left  = *plans[i].model;
            const auto& right = *plans[i + 1].model;
            const auto leftStart  = toTimelineSample(left.startTime);
            const auto leftEnd    = juce::jmax(leftStart + (DAW::SamplePosition)1,
                                               toTimelineSample(left.endTime()));
            const auto rightStart = toTimelineSample(right.startTime);
            const auto rightEnd   = juce::jmax(rightStart + (DAW::SamplePosition)1,
                                               toTimelineSample(right.endTime()));

            // The earlier clip owns the cosine fade-out and the later clip
            // owns the sine fade-in.  The overlap is the exact intersection;
            // no fixed duration, stale edge length, or manual curve mutation.
            DAW::SoundEngine::ApexClipCrossfadeCore::prepareAdjacentRelationship(
                { leftStart, leftEnd },
                { rightStart, rightEnd },
                plans[i].state,
                plans[i + 1].state);
        }

        // Publish the complete prepared state only after all relationships have
        // been derived.  The AudioClip setter uses a bounded atomic seqlock so
        // the callback never reads a partially written range.
        for (const auto& plan : plans)
        {
            const auto mapped = m_uuidToEngineId.find(plan.model->id);
            if (mapped == m_uuidToEngineId.end())
                continue;

            if (auto* audioClip = dynamic_cast<DAW::AudioClip*>(
                    m_engineClipManager->getClip(mapped->second)))
                audioClip->setAutoCrossfadeRanges(plan.state);
        }
    }

    void ArrangementViewCore::refreshAutoCrossfadesForAllTracks()
    {
        if (m_engineClipManager == nullptr)
            return;

        std::set<int> trackIndices;
        for (const auto& model : m_clipState.allClips())
            trackIndices.insert(model.trackIndex);

        for (const int trackIndex : trackIndices)
            refreshAutoCrossfadesForTrack(trackIndex);
    }

    void ArrangementViewCore::syncClipToEngine(const ArrangementClipModel& clip)
    {
        if (m_engineClipManager == nullptr)
            return;

        const auto it = m_uuidToEngineId.find(clip.id);
        if (it == m_uuidToEngineId.end())
            return;

        auto* baseClip = m_engineClipManager->getClip(it->second);
        auto* audioClip = dynamic_cast<DAW::AudioClip*>(baseClip);
        if (audioClip == nullptr)
            return;

        audioClip->setStartPosition((DAW::SamplePosition) std::llround(clip.startTime * m_engineSampleRate));
        audioClip->setLength(juce::jmax((DAW::SamplePosition) 1,
                            (DAW::SamplePosition) std::llround(clip.length * m_engineSampleRate)));

        const double srcRate = (m_engineAudioFiles != nullptr)
            ? m_engineAudioFiles->getSourceSampleRate(audioClip->getID())
            : m_engineSampleRate;

        audioClip->setSourceOffset(juce::jmax((DAW::SamplePosition) 0,
                                   (DAW::SamplePosition) std::llround(clip.sourceOffset * srcRate)));
        audioClip->setGain(clip.gain);
        audioClip->setMuted(clip.muted);
        audioClip->setFadeInLength((DAW::SamplePosition) std::llround(clip.fadeInLength * m_engineSampleRate));
        audioClip->setFadeOutLength((DAW::SamplePosition) std::llround(clip.fadeOutLength * m_engineSampleRate));

        const int masterOffset = (m_engineTrackManager != nullptr && m_engineTrackManager->hasMasterTrack()) ? 1 : 0;
        const int engineTrackIndex = clip.trackIndex - masterOffset;
        if (m_engineTrackManager != nullptr && engineTrackIndex >= 0 && engineTrackIndex < m_engineTrackManager->getNumTracks())
            if (auto* t = m_engineTrackManager->getTrack(engineTrackIndex))
                audioClip->setTrackID(t->getID());

        ClipTimePitchBridgeCore::pushToAudioClip(clip, audioClip);
    }

    ArrangementClipModel* ArrangementViewCore::findClipAt(juce::Point<int> pos)
    {
        auto* renderer = findClipRendererAt(pos);
        if (renderer)
        {
            return m_clipState.findClip(renderer->getClipId());
        }
        return nullptr;
    }

    double ArrangementViewCore::screenXToTime(int x) const
    {
        return m_zoom.xToTime((double)x);
    }

    int ArrangementViewCore::timeToScreenX(double time) const
    {
        return (int)m_zoom.timeToX(time);
    }

    int ArrangementViewCore::arrangementContentTop() const
    {
        return 48 + 32;
    }

    int ArrangementViewCore::computeClampedDragAnchorX() const
    {
        return juce::jmax(0, m_drag.currentMouse.x - m_drag.mouseOffsetX);
    }

    double ArrangementViewCore::snapTimeToGrid(double time) const
    {
        return juce::jmax(0.0, ArrangementSnapCore::snapToGrid(time, m_tempoBpm, m_snapMode));
    }

    int ArrangementViewCore::computeSnappedDragAnchorX() const
    {
        const int clampedAnchorX = computeClampedDragAnchorX();
        if (m_snapMode == SnapMode::Free)
            return clampedAnchorX;

        return juce::jmax(0, timeToScreenX(snapTimeToGrid(screenXToTime(clampedAnchorX))));
    }

    int ArrangementViewCore::trackIndexAt(juce::Point<int> pos) const
    {
        const int trackHeight = juce::jmax(1, (int)m_zoom.getTrackHeightPx());
        return juce::jlimit(0, juce::jmax(0, getArrangementTrackCount() - 1),
                            (pos.y - arrangementContentTop()) / trackHeight);
    }

    int ArrangementViewCore::getArrangementTrackCount() const
    {
        // Always derive minimum from actual clip data so clampTrackIndex
        // never collapses clips on higher tracks to track 0.
        int maxTrack = 0;
        for (const auto& clip : m_clipState.allClips())
            maxTrack = juce::jmax(maxTrack, clip.trackIndex);
        for (const auto& item : m_drag.items)
            maxTrack = juce::jmax(maxTrack, item.clip.trackIndex);

        const int fromClips = juce::jmax(3, maxTrack + 1);

        if (m_engineTrackManager != nullptr)
            return juce::jmax(fromClips, m_engineTrackManager->getNumTracks() + (m_engineTrackManager->hasMasterTrack() ? 1 : 0));

        return fromClips;
    }

    int ArrangementViewCore::clampTrackIndex(int trackIndex) const
    {
        const int count = getArrangementTrackCount();
        const int maxTrack = juce::jmax(0, count - 1);
        return juce::jlimit(0, maxTrack, trackIndex);
    }

    std::vector<juce::Uuid> ArrangementViewCore::getVisualSelectedClipIds() const
    {
        std::vector<juce::Uuid> ids;
        for (const auto& renderer : m_clipRenderers)
            if (renderer != nullptr && renderer->isSelected())
                ids.push_back(renderer->getClipId());
        return ids;
    }

    int ArrangementViewCore::getVisualSelectedClipCount() const
    {
        return (int)getVisualSelectedClipIds().size();
    }

    void ArrangementViewCore::purgeInvalidSelectedClips()
    {
        m_selection.purgeInvalidSelection([this](const juce::Uuid& id)
        {
            return m_clipState.findClip(id) != nullptr;
        });
    }

    juce::String ArrangementViewCore::formatUuidShort(const juce::Uuid& id) const
    {
        const auto s = id.toString();
        return s.isEmpty() ? "none" : s.substring(0, juce::jmin(8, s.length()));
    }

    juce::String ArrangementViewCore::formatRect(const juce::Rectangle<int>& r) const
    {
        return "[x=" + juce::String(r.getX())
            + " y=" + juce::String(r.getY())
            + " w=" + juce::String(r.getWidth())
            + " h=" + juce::String(r.getHeight()) + "]";
    }

    bool ArrangementViewCore::areAllDragItemBoundsIdentical() const
    {
        if (m_drag.items.size() < 2)
            return false;

        const auto first = m_drag.items.front().bounds;
        for (const auto& item : m_drag.items)
            if (item.bounds != first)
                return false;
        return true;
    }

    bool ArrangementViewCore::areAllGhostPositionsIdentical() const
    {
        if (m_drag.items.size() < 2 || !m_drag.active)
            return false;

        const int trackH = (int)m_zoom.getTrackHeightPx();
        const int contentTop = arrangementContentTop();
        const int newAnchorX = m_drag.currentMouse.x - m_drag.mouseOffsetX;
        const int newAnchorY = m_drag.currentMouse.y - m_drag.mouseOffsetY;
        const int dx = newAnchorX - m_drag.anchorOriginalBounds.getX();
        const int dy = newAnchorY - m_drag.anchorOriginalBounds.getY();
        const int trackDelta = (dy + (dy >= 0 ? trackH / 2 : -(trackH / 2))) / trackH;
        const int snappedDy = trackDelta * trackH;

        juce::Point<int> first;
        bool firstSet = false;
        for (const auto& item : m_drag.items)
        {
            const int rawDestY = item.bounds.getY() + snappedDy;
            const juce::Point<int> p(item.bounds.getX() + dx, rawDestY);
            if (!firstSet)
            {
                first = p;
                firstSet = true;
            }
            else if (p != first)
            {
                return false;
            }
        }

        return true;
    }

    juce::String ArrangementViewCore::buildArrangementDebugText() const
    {
        juce::StringArray lines;
        const auto* anchorRenderer = m_drag.active ? findClipRendererFor(m_drag.clipId) : nullptr;
        const bool isDuplicateDrag = m_lastMouseMods.isCtrlDown() && m_lastMouseMods.isShiftDown();
        const int trackH = (int)m_zoom.getTrackHeightPx();
        const int contentTop = arrangementContentTop();
        const int trackCount = getArrangementTrackCount();

        lines.add("HEADER");
        lines.add("activeTool=" + toolName(m_toolState.getActiveTool()));
        lines.add("mouse=" + m_lastMousePos.toString());
        lines.add("lastMouseDownSource=" + m_lastMouseDownSource + " clip=" + formatUuidShort(m_lastMouseDownClipId));
        lines.add("mods ctrl=" + juce::String(m_lastMouseMods.isCtrlDown() ? 1 : 0)
            + " shift=" + juce::String(m_lastMouseMods.isShiftDown() ? 1 : 0)
            + " alt=" + juce::String(m_lastMouseMods.isAltDown() ? 1 : 0)
            + " left=" + juce::String(m_lastMouseMods.isLeftButtonDown() ? 1 : 0)
            + " right=" + juce::String(m_lastMouseMods.isRightButtonDown() ? 1 : 0));
        lines.add("isDuplicateDrag=" + juce::String(isDuplicateDrag ? "true" : "false"));
        lines.add("rubberBand visible=" + juce::String(m_rubberBand.isVisible() ? "true" : "false"));
        lines.add("drag active=" + juce::String(m_drag.active ? "true" : "false")
            + " duplicate=" + juce::String(m_drag.duplicate ? "true" : "false"));
        lines.add({});

        lines.add("SELECTION STATE");
        juce::StringArray coreIds;
        for (const auto& id : m_selection.getSelectedIds())
            coreIds.add(formatUuidShort(id));
        const auto visualIds = getVisualSelectedClipIds();
        juce::StringArray visualIdStrings;
        for (const auto& id : visualIds)
            visualIdStrings.add(formatUuidShort(id));
        lines.add("selectionCount=" + juce::String(m_selection.getSelectionCount()));
        lines.add("selectedIds=" + coreIds.joinIntoString(", "));
        lines.add("primaryClip=" + formatUuidShort(m_drag.clipId));
        lines.add("visualSelectedCount=" + juce::String((int)visualIds.size()));
        lines.add("visualSelectedIds=" + visualIdStrings.joinIntoString(", "));
        std::set<juce::Uuid> coreSet(m_selection.getSelectedIds().begin(), m_selection.getSelectedIds().end());
        std::set<juce::Uuid> visualSet(visualIds.begin(), visualIds.end());
        if (coreSet != visualSet)
            lines.add("WARNING: visual selection != selection core");
        lines.add({});

        lines.add("DRAG STATE");
        int dx = 0;
        int dy = 0;
        int trackDelta = 0;
        int snappedDy = 0;
        if (m_drag.active)
        {
            const int newAnchorX = m_drag.currentMouse.x - m_drag.mouseOffsetX;
            const int newAnchorY = m_drag.currentMouse.y - m_drag.mouseOffsetY;
            dx = newAnchorX - m_drag.anchorOriginalBounds.getX();
            dy = newAnchorY - m_drag.anchorOriginalBounds.getY();
            trackDelta = (dy + (dy >= 0 ? trackH / 2 : -(trackH / 2))) / trackH;
            snappedDy = trackDelta * trackH;
        }
        lines.add("m_drag.active=" + juce::String(m_drag.active ? "true" : "false"));
        lines.add("m_drag.duplicate=" + juce::String(m_drag.duplicate ? "true" : "false"));
        lines.add("m_drag.clipId=" + formatUuidShort(m_drag.clipId));
        lines.add("m_drag.clipIds.size=" + juce::String((int)m_drag.clipIds.size()));
        lines.add("m_drag.items.size=" + juce::String((int)m_drag.items.size()));
        lines.add("mouseOffset=" + juce::String(m_drag.mouseOffsetX) + "," + juce::String(m_drag.mouseOffsetY));
        lines.add("currentMouse=" + m_drag.currentMouse.toString());
        lines.add("anchorOriginalBounds=" + formatRect(m_drag.anchorOriginalBounds));
        lines.add("anchor renderer bounds=" + (anchorRenderer ? formatRect(anchorRenderer->getBounds()) : juce::String("missing")));
        lines.add("dx/dy=" + juce::String(dx) + "/" + juce::String(dy));
        lines.add("trackDelta/snappedDy=" + juce::String(trackDelta) + "/" + juce::String(snappedDy));
        lines.add("trackCount=" + juce::String(trackCount));
        if (trackCount > 3)
            lines.add("hardcoded clamp warning: legacy 0..2 clamp must not be used");
        if (m_drag.active && anchorRenderer == nullptr)
            lines.add("WARNING: anchor renderer missing during drag");
        lines.add({});

        lines.add("DRAG ITEMS TABLE");
        for (int i = 0; i < (int)m_drag.items.size(); ++i)
        {
            const auto& item = m_drag.items[(size_t)i];
            const auto* renderer = findClipRendererFor(item.clipId);
            lines.add("#" + juce::String(i)
                + " id=" + formatUuidShort(item.clipId)
                + " name=" + juce::String(item.clip.clipName)
                + " track=" + juce::String(item.clip.trackIndex)
                + " start=" + juce::String(item.clip.startTime, 3)
                + " bounds=" + formatRect(item.bounds)
                + " renderer=" + juce::String(renderer != nullptr ? "yes" : "no")
                + " rendererBounds=" + (renderer ? formatRect(renderer->getBounds()) : juce::String("n/a"))
                + " visible=" + juce::String(renderer != nullptr && renderer->isVisible() ? "yes" : "no")
                + " selected=" + juce::String(renderer != nullptr && renderer->isSelected() ? "yes" : "no"));
        }
        lines.add({});

        lines.add("GHOST TABLE");
        int ghostCount = 0;
        for (int i = 0; i < (int)m_drag.items.size(); ++i)
        {
            const auto& item = m_drag.items[(size_t)i];
            const int rawDestY = item.bounds.getY() + snappedDy;
            const int destTrack = clampTrackIndex((rawDestY - contentTop) / trackH);
            const int ghostY = rawDestY;
            const int ghostX = item.bounds.getX() + dx;
            const juce::Rectangle<int> ghostRect(ghostX, ghostY + 1, juce::jmax(4, item.bounds.getWidth()), juce::jmax(8, item.bounds.getHeight()) - 3);
            const bool visible = ghostRect.getWidth() >= 4 && ghostRect.getHeight() >= 4;
            if (visible)
                ++ghostCount;
            lines.add("#" + juce::String(i)
                + " id=" + formatUuidShort(item.clipId)
                + " ghostX=" + juce::String(ghostX)
                + " ghostY=" + juce::String(ghostY)
                + " destTrack=" + juce::String(destTrack)
                + " rect=" + formatRect(ghostRect)
                + " visible=" + juce::String(visible ? "yes" : "no")
                + " skipped=" + juce::String(visible ? "none" : "too small"));
        }
        lines.add({});

        lines.add("WARNINGS");
        if (m_selection.getSelectionCount() > 1 && (int)m_drag.items.size() <= 1)
            lines.add("BUG: multi-selection not captured into drag items");
        if (areAllDragItemBoundsIdentical())
            lines.add("BUG: drag item bounds identical, findClipRendererFor likely wrong");
        if (ghostCount > 1 && areAllGhostPositionsIdentical())
            lines.add("BUG: ghosts overlap perfectly");
        if (trackCount > 3)
        {
            for (const auto& item : m_drag.items)
            {
                const int rawDestY = item.bounds.getY() + snappedDy;
                const int unclamped = (rawDestY - contentTop) / trackH;
                if (unclamped > 2)
                {
                    lines.add("BUG: hardcoded 3-track clamp");
                    break;
                }
            }
        }
        if (m_drag.active && anchorRenderer == nullptr)
            lines.add("WARNING: anchor renderer missing during drag");
        lines.add({});

        lines.add("EVENT LOG");
        lines.add(m_arrangementDebugLog.getText());
        return lines.joinIntoString("\n");
    }

    void ArrangementViewCore::appendDebugLog(const juce::String& line)
    {
        m_arrangementDebugLog.addLine(line);
        if (m_arrangementDebugPanelVisible && m_arrangementDebugPanel != nullptr)
            m_arrangementDebugPanel->refreshNow();
    }

    void ArrangementViewCore::toggleArrangementDebugPanel()
    {
        m_arrangementDebugPanelVisible = !m_arrangementDebugPanelVisible;
        if (m_arrangementDebugPanel != nullptr)
        {
            m_arrangementDebugPanel->setVisible(m_arrangementDebugPanelVisible);
            if (m_arrangementDebugPanelVisible)
            {
                m_arrangementDebugPanel->refreshNow();
                m_arrangementDebugPanel->toFront(false);
            }
        }

        appendDebugLog("[debugPanel] visible=" + juce::String(m_arrangementDebugPanelVisible ? "true" : "false"));
        repaint();
    }

    // =========================================================================
    // Clip context menu — right-click on arrangement clip
    // =========================================================================

    void ArrangementViewCore::showClipContextMenu(ArrangementClipModel* clip,
                                                   juce::Point<int> screenPos,
                                                   juce::Point<int> localPos)
    {
        const ClipFreezeState* fs = nullptr;
        auto it = m_freezeStates.find(clip->id);
        if (it != m_freezeStates.end()) fs = &it->second;

        ClipContextCallbacks cb;
        cb.clickTime  = screenXToTime(localPos.x);
        cb.clickTrack = trackIndexAt(localPos);

        cb.cutClip       = [this](ArrangementClipModel* c) { onClipCut(c);       };
        cb.copyClip      = [this](ArrangementClipModel* c) { onClipCopy(c);      };
        cb.duplicateClip = [this](ArrangementClipModel* c) { onClipDuplicate(c); };
        cb.deleteClip    = [this](ArrangementClipModel* c) { onClipDelete(c);    };
        cb.renameClip    = [this](ArrangementClipModel* c) { onClipRename(c);    };
        cb.muteClip      = [this](ArrangementClipModel* c) { onClipMute(c);      };
        cb.canPaste      = [this]() { return !m_clipboardClips.empty(); };
        cb.pasteClip     = [this](double t, int tr) { onClipPaste(t, tr); };

        cb.openTimePitchPanel = [this](ArrangementClipModel* c)
        {
            if (m_propertiesWindow) m_propertiesWindow->openForClip(c);
        };
        cb.applyPreset    = [this](ArrangementClipModel* c, const std::string& name) { onClipPreset(c, name); };
        cb.bounceClip     = [this](ArrangementClipModel* c) { onClipBounce(c);   };
        cb.freezeClip     = [this](ArrangementClipModel* c) { onClipFreeze(c);   };
        cb.unfreezeClip   = [this](ArrangementClipModel* c) { onClipUnfreeze(c); };
        cb.resetTimePitch = [this](ArrangementClipModel* c) { onClipResetTP(c);  };
        cb.isPresetCurveVisible = [this](const ArrangementClipModel* c) { return isPresetCurveVisible(c); };
        cb.togglePresetCurveVisibility = [this](const ArrangementClipModel* c) { togglePresetCurveVisibility(c); };
        cb.createAutomationForClip = [this](ArrangementClipModel* c, const juce::String& parameterId)
        {
            if (c == nullptr)
                return;

            appendDebugLog("[automationQuickCreate] clip=" + juce::String(c->clipName)
                + " parameter=" + parameterId
                + " start=" + juce::String(c->startTime, 3)
                + " length=" + juce::String(c->length, 3));
        };

        cb.openAutomateClipPanel = [this](ArrangementClipModel* c) { onClipOpenAutomationPanel(c); };
        cb.openVocalTune = [this](ArrangementClipModel* c)
        {
            if (c == nullptr || m_engineClipManager == nullptr || onEngineClipVocalTuneRequested == nullptr)
                return;

            const auto mapped = m_uuidToEngineId.find(c->id);
            if (mapped == m_uuidToEngineId.end())
                return;

            if (auto* engineClip = m_engineClipManager->getClip(mapped->second))
                if (auto* audioClip = dynamic_cast<DAW::AudioClip*>(engineClip))
                    onEngineClipVocalTuneRequested(*audioClip);
        };

        cb.toggleVocalTuneBypass = [this](ArrangementClipModel* c)
        {
            if (c == nullptr || m_engineClipManager == nullptr || !onEngineClipVocalTuneBypassToggled)
                return;
            const auto mapped = m_uuidToEngineId.find(c->id);
            if (mapped == m_uuidToEngineId.end()) return;
            if (auto* engineClip = m_engineClipManager->getClip(mapped->second))
                if (auto* audioClip = dynamic_cast<DAW::AudioClip*>(engineClip))
                    onEngineClipVocalTuneBypassToggled(*audioClip);
        };

        cb.isVocalTuneBypassed = [this](ArrangementClipModel* c) -> bool
        {
            if (c == nullptr || m_engineClipManager == nullptr || !onEngineClipIsVocalTuneBypassed)
                return false;
            const auto mapped = m_uuidToEngineId.find(c->id);
            if (mapped == m_uuidToEngineId.end()) return false;
            if (auto* engineClip = m_engineClipManager->getClip(mapped->second))
                if (auto* audioClip = dynamic_cast<DAW::AudioClip*>(engineClip))
                    return onEngineClipIsVocalTuneBypassed(*audioClip);
            return false;
        };

        cb.normalizeClip = [this](ArrangementClipModel* c)
        {
            if (c == nullptr || m_engineAudioFiles == nullptr) return;
            const auto mapped = m_uuidToEngineId.find(c->id);
            if (mapped == m_uuidToEngineId.end()) return;
            m_engineAudioFiles->togglePeakNormalize(mapped->second);
        };

        cb.isClipNormalized = [this](ArrangementClipModel* c) -> bool
        {
            if (c == nullptr || m_engineAudioFiles == nullptr) return false;
            const auto mapped = m_uuidToEngineId.find(c->id);
            if (mapped == m_uuidToEngineId.end()) return false;
            return m_engineAudioFiles->isPeakNormalized(mapped->second);
        };

        ClipContextMenuCore::show(clip, screenPos, cb, fs);
    }

    void ArrangementViewCore::showEmptyTrackContextMenu(juce::Point<int> screenPos,
                                                        juce::Point<int> localPos)
    {
        ClipContextCallbacks cb;
        cb.clickTime  = screenXToTime(localPos.x);
        cb.clickTrack = trackIndexAt(localPos);
        cb.canPaste   = [this]() { return !m_clipboardClips.empty(); };
        cb.pasteClip  = [this](double t, int tr) { onClipPaste(t, tr); };

        ClipContextMenuCore::showOnEmptyTrack(screenPos, cb);
    }

    void ArrangementViewCore::onClipBounce(ArrangementClipModel* clip)
    {
        // Load source buffer from file
        juce::File f(clip->sourcePath);
        if (!f.existsAsFile()) return;

        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(f));
        if (!reader) return;

        const int srcLen = (int)reader->lengthInSamples;
        juce::AudioBuffer<float> src((int)reader->numChannels, srcLen);
        reader->read(&src, 0, srcLen, 0, true, true);

        m_bounceCore.onBounceComplete = [this, clipId = clip->id]
            (const ArrangementClipModel& newModel, juce::AudioBuffer<float>&& buf)
        {
            // Update clip, update waveform from bounced buffer
            auto* c = m_clipState.findClip(clipId);
            if (!c) return;
            c->timePitch = newModel.timePitch; // identity after bounce

            // Update waveform from the processed audio
            if (auto* renderer = findClipRendererFor(clipId))
            {
                const int w = renderer->getWidth();
                const bool activateNow = renderer->isVisible()
                    && renderer->shouldRenderWaveform();
                renderer->getWaveformCache().requestPeaksFromBuffer(buf, w, activateNow);
                renderer->setHQRendered(true);
                renderer->setRenderingStretch(false);
            }
        };

        m_bounceCore.bounceClip(*clip, src, m_engineSampleRate);

        // Show "rendering" badge while bouncing
        if (auto* renderer = findClipRendererFor(clip->id))
            renderer->setRenderingStretch(true);
    }

    void ArrangementViewCore::onClipFreeze(ArrangementClipModel* clip)
    {
        juce::File f(clip->sourcePath);
        if (!f.existsAsFile()) return;

        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(f));
        if (!reader) return;

        const int srcLen = (int)reader->lengthInSamples;
        juce::AudioBuffer<float> src((int)reader->numChannels, srcLen);
        reader->read(&src, 0, srcLen, 0, true, true);

        auto& fs = m_freezeStates[clip->id];
        m_bounceCore.freezeClip(*clip, fs, src, m_engineSampleRate);

        if (auto* renderer = findClipRendererFor(clip->id))
        {
            renderer->setFrozen(true);
            renderer->setRenderingStretch(false);
        }
    }

    void ArrangementViewCore::onClipUnfreeze(ArrangementClipModel* clip)
    {
        auto it = m_freezeStates.find(clip->id);
        if (it == m_freezeStates.end()) return;

        m_bounceCore.unfreezeClip(*clip, it->second);
        m_freezeStates.erase(it);

        if (auto* renderer = findClipRendererFor(clip->id))
            renderer->setFrozen(false);
    }

    void ArrangementViewCore::onClipPreset(ArrangementClipModel* clip,
                                             const std::string& presetName)
    {
        if (!clip) return;
        const TimePitchState oldState = TimePitchPresetsCore::applyPreset(presetName, *clip);

        // Keep the curve's on-screen visibility across preset applies.
        const bool curveWasVisible = isPresetCurveVisible(clip);

        // Create undo action
        m_undo.perform(new TimePitchSetStateAction(
            clip->id, juce::String(clip->clipName),
            oldState, clip->timePitch,
            [this, clipId = clip->id](const TimePitchState& s)
            {
                if (auto* c = m_clipState.findClip(clipId))
                {
                    c->timePitch = s;
                    clearPresetAutomationLanes(c);
                    createPresetAutomationLanes(c, s);
                    syncClipToEngine(*c);
                    rebuildClipRenderers();
                }
            }));

        // The preset's pitch/stretch curve is created (so it can be seen and
        // edited) but its lane rows are NOT forced into the timeline: the
        // Presets menu's "Show Preset Curve" toggle reveals them on demand.
        // The engine reads the values from the engine clip (pushToAudioClip);
        // an enabled lane curve overrides them inside its drawn region.
        clearPresetAutomationLanes(clip);
        createPresetAutomationLanes(clip, clip->timePitch);
        if (curveWasVisible)
            setPresetCurveVisible(clip, true);

        // Apply the preset to the clip itself so it is audible immediately
        // and the Presets menu check mark matches. The curve above overrides
        // this value between its dots.
        syncClipToEngine(*clip);

        rebuildClipRenderers();
    }

    void ArrangementViewCore::onClipResetTP(ArrangementClipModel* clip)
    {
        if (!clip) return;
        const TimePitchState oldState = clip->timePitch;
        clip->timePitch = TimePitchState{}; // identity

        m_undo.perform(new TimePitchSetStateAction(
            clip->id, juce::String(clip->clipName),
            oldState, clip->timePitch,
            [this, clipId = clip->id](const TimePitchState& s)
            {
                if (auto* c = m_clipState.findClip(clipId))
                {
                    c->timePitch = s;
                    clearPresetAutomationLanes(c);
                    createPresetAutomationLanes(c, s);
                    syncClipToEngine(*c);
                    rebuildClipRenderers();
                }
            }));

        // Reset clears the preset curve entirely (identity state) and hides
        // its lane rows.
        clearPresetAutomationLanes(clip);
        setPresetCurveVisible(clip, false);

        // Push the identity state to the engine clip so the reset is audible.
        syncClipToEngine(*clip);

        rebuildClipRenderers();
    }

    void ArrangementViewCore::clearPresetAutomationLanes(ArrangementClipModel* clip)
    {
        if (clip == nullptr || m_automationManager == nullptr || m_engineClipManager == nullptr)
            return;

        const auto mapped = m_uuidToEngineId.find(clip->id);
        if (mapped == m_uuidToEngineId.end())
            return;

        auto* engineClip = m_engineClipManager->getClip(mapped->second);
        if (engineClip == nullptr)
            return;

        const DAW::TrackID trackId = engineClip->getTrackID();
        const juce::String engineClipId = mapped->second;

        // Presets write pitch/stretch through ClipTimePitchBridgeCore and must
        // not leave automation lanes behind: an enabled clip pitch/stretch lane
        // overrides the clip's own parameters inside the drawn region, so a
        // leftover preset lane makes the knobs look broken. Remove both the
        // model lane (so the override stops) and the lane row (so it goes away).
        const juce::String pitchParam = DAW::AutomationLaneCore::makeClipPitchParameterId(engineClipId);
        const juce::String stretchParam = DAW::AutomationLaneCore::makeClipStretchParameterId(engineClipId);

        m_automationManager->clearLane(trackId, pitchParam);
        m_automationManager->clearLane(trackId, stretchParam);
        hideAutomationLane(trackId, pitchParam);
        hideAutomationLane(trackId, stretchParam);

        m_automationManager->publishSnapshot();
    }

    void ArrangementViewCore::createPresetAutomationLanes(ArrangementClipModel* clip,
                                                          const TimePitchState& state)
    {
        if (clip == nullptr || m_automationManager == nullptr || m_engineClipManager == nullptr)
            return;

        const auto mapped = m_uuidToEngineId.find(clip->id);
        if (mapped == m_uuidToEngineId.end())
            return;

        auto* engineClip = m_engineClipManager->getClip(mapped->second);
        if (engineClip == nullptr)
            return;

        const DAW::TrackID trackId = engineClip->getTrackID();
        const juce::String engineClipId = mapped->second;
        const int64_t startSample   = (int64_t)std::llround(clip->startTime * m_engineSampleRate);
        const int64_t lengthSamples = juce::jmax<int64_t>(1, (int64_t)std::llround(clip->length * m_engineSampleRate));

        // Preset curves cover only the final window (default 0.5 s), not the
        // whole clip: the engine applies a lane only between its first and
        // last dot, so the clip plays normally and the effect ramps in at the
        // end — a real curve down, localised where it belongs.
        const double presetCurveSr = m_engineSampleRate > 0.0 ? m_engineSampleRate : 48000.0;
        const int64_t curveWindow = juce::jmax<int64_t>(1, juce::jmin<int64_t>(
            lengthSamples, (int64_t) std::llround(0.5 * presetCurveSr)));
        const int64_t curveStart = startSample + lengthSamples - curveWindow;

        // Clip pitch lane — ramps from identity at the window start to the
        // preset's pitch at the clip end (a curve down for a pitch drop).
        const juce::String pitchParam = DAW::AutomationLaneCore::makeClipPitchParameterId(engineClipId);
        if (std::abs(state.pitchSemitones) > 0.05f)
        {
            auto& lane = m_automationManager->getOrCreateLane(trackId, pitchParam);
            lane.clear();
            lane.setDefaultValue(0.0f);
            lane.addPoint(curveStart, 0.0f);
            lane.addPoint(curveStart + curveWindow / 2, state.pitchSemitones * 0.35f);
            lane.addPoint(startSample + lengthSamples, state.pitchSemitones);
            lane.setCurveToNext(0, DAW::AutomationCurveType::Smooth);
            lane.setCurveToNext(1, DAW::AutomationCurveType::Smooth);
            lane.setVisible(true);
            lane.setEnabled(true);
        }
        else
        {
            m_automationManager->clearLane(trackId, pitchParam);
        }

        // Clip stretch lane — ramps 1.0 -> the preset's stretch ratio.
        const juce::String stretchParam = DAW::AutomationLaneCore::makeClipStretchParameterId(engineClipId);
        if (std::abs(state.stretchRatio - 1.0) > 0.01f)
        {
            auto& lane = m_automationManager->getOrCreateLane(trackId, stretchParam);
            lane.clear();
            lane.setDefaultValue(1.0f);
            lane.addPoint(curveStart, 1.0f);
            lane.addPoint(startSample + lengthSamples, state.stretchRatio);
            lane.setCurveToNext(0, DAW::AutomationCurveType::Smooth);
            lane.setVisible(true);
            lane.setEnabled(true);
        }
        else
        {
            m_automationManager->clearLane(trackId, stretchParam);
        }

        m_automationManager->publishSnapshot();

        // If the curve rows are currently on screen, rebuild them so the new
        // ramp is drawn instead of a stale lane that still shows the old
        // (flat) curve.
        if (isPresetCurveVisible(clip))
        {
            setPresetCurveVisible(clip, false);
            setPresetCurveVisible(clip, true);
        }
    }

    void ArrangementViewCore::applyPresetCurveEngineState(ArrangementClipModel* clip)
    {
        // The automation curve owns pitch/stretch between its dots. The clip's
        // static values stay at identity so before the first dot and after the
        // last dot the clip plays normally with no residual effect.
        if (clip == nullptr || m_engineClipManager == nullptr)
            return;

        const auto mapped = m_uuidToEngineId.find(clip->id);
        if (mapped == m_uuidToEngineId.end())
            return;

        auto* audioClip = dynamic_cast<DAW::AudioClip*>(m_engineClipManager->getClip(mapped->second));
        if (audioClip == nullptr)
            return;

        audioClip->setPitchTargetSemitones(0.0f);
        audioClip->setFineTuneCents(0.0f);
        audioClip->setTimeStretch(1.0f);
    }

    bool ArrangementViewCore::isPresetCurveVisible(const ArrangementClipModel* clip)
    {
        if (clip == nullptr || m_engineClipManager == nullptr)
            return false;

        const auto mapped = m_uuidToEngineId.find(clip->id);
        if (mapped == m_uuidToEngineId.end())
            return false;

        auto* engineClip = m_engineClipManager->getClip(mapped->second);
        if (engineClip == nullptr)
            return false;

        const auto containerIt = m_automationContainers.find(engineClip->getTrackID());
        if (containerIt == m_automationContainers.end() || containerIt->second == nullptr)
            return false;

        const juce::String engineClipId = mapped->second;
        return containerIt->second->hasLane(DAW::AutomationLaneCore::makeClipPitchParameterId(engineClipId))
            || containerIt->second->hasLane(DAW::AutomationLaneCore::makeClipStretchParameterId(engineClipId));
    }

    void ArrangementViewCore::setPresetCurveVisible(const ArrangementClipModel* clip, bool visible)
    {
        if (clip == nullptr || m_engineClipManager == nullptr)
            return;

        const auto mapped = m_uuidToEngineId.find(clip->id);
        if (mapped == m_uuidToEngineId.end())
            return;

        auto* engineClip = m_engineClipManager->getClip(mapped->second);
        if (engineClip == nullptr)
            return;

        const DAW::TrackID trackId = engineClip->getTrackID();
        const juce::String engineClipId = mapped->second;

        const auto applyToLane = [this, trackId, visible](const juce::String& parameterId)
        {
            if (visible)
            {
                // Only reveal lanes that actually hold a preset curve — an
                // empty lane row would be visual noise.
                if (m_automationManager != nullptr)
                {
                    const auto* lane = m_automationManager->findLane(trackId, parameterId);
                    if (lane == nullptr || !lane->isEnabled() || lane->points.empty())
                        return;
                }
                showAutomationLane(trackId, parameterId);
            }
            else
            {
                hideAutomationLane(trackId, parameterId);
            }
        };

        applyToLane(DAW::AutomationLaneCore::makeClipPitchParameterId(engineClipId));
        applyToLane(DAW::AutomationLaneCore::makeClipStretchParameterId(engineClipId));
    }

    void ArrangementViewCore::togglePresetCurveVisibility(const ArrangementClipModel* clip)
    {
        setPresetCurveVisible(clip, !isPresetCurveVisible(clip));
    }

    // =========================================================================
    // Automate This Clip panel
    // =========================================================================

    void ArrangementViewCore::onClipOpenAutomationPanel(ArrangementClipModel* clip)
    {
        if (!clip) return;

        // Resolve the engine clip ID for correct parameter naming in automation lanes.
        juce::String resolvedEngineClipId;
        {
            const auto mapped = m_uuidToEngineId.find(clip->id);
            if (mapped != m_uuidToEngineId.end())
                resolvedEngineClipId = mapped->second;
        }

        // Fire the optional host-side callback so the main Clip Properties
        // workflow can handle automation from its own Automate button.
        if (m_engineClipManager != nullptr && onEngineClipAutomationRequested && resolvedEngineClipId.isNotEmpty())
        {
            if (auto* engineClip = m_engineClipManager->getClip(resolvedEngineClipId))
                onEngineClipAutomationRequested(*engineClip);
        }
    }

    // =========================================================================
    // Standard DAW clip operations
    // =========================================================================

    void ArrangementViewCore::onClipCut(ArrangementClipModel* clip)
    {
        purgeInvalidSelectedClips();
        if (!clip) return;
        if (m_selection.isSelected(clip->id))
        {
            copySelectedClipsToClipboard(true);
            return;
        }
        DBG("[CRASH TRACE] action=clip_cut file=ArrangementViewCore track=" << clip->trackIndex
            << " clip=" << formatUuidShort(clip->id));
        m_clipboardClips.assign(1, *clip);
        m_clipboardWasCut = true;

        ArrangementClipModel snapshot = *clip;
        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            "Cut Clip",
            [this, snapshot]() { m_clipState.removeClip(snapshot.id); m_selection.deselectAll(); },
            [this, snapshot]() { m_clipState.addClip(snapshot); m_selection.deselectAll(); m_selection.selectClip(snapshot.id); }));
    }

    void ArrangementViewCore::onClipCopy(ArrangementClipModel* clip)
    {
        purgeInvalidSelectedClips();
        if (!clip) return;
        if (m_selection.isSelected(clip->id))
        {
            copySelectedClipsToClipboard(false);
            return;
        }
        DBG("[CRASH TRACE] action=clip_copy file=ArrangementViewCore track=" << clip->trackIndex
            << " clip=" << formatUuidShort(clip->id));
        m_clipboardClips.assign(1, *clip);
        m_clipboardWasCut = false;
    }

    void ArrangementViewCore::copySelectedClipsToClipboard(bool cut)
    {
        purgeInvalidSelectedClips();
        auto selected = m_selection.getSelectedIds();
        if (selected.empty()) return;

        // Snapshot EVERY selected clip — never drop to the first one.
        std::vector<ArrangementClipModel> snapshots;
        snapshots.reserve(selected.size());
        for (auto& id : selected)
            if (auto* c = m_clipState.findClip(id))
                snapshots.push_back(*c);

        if (snapshots.empty()) return;

        m_clipboardClips = snapshots;
        m_clipboardWasCut = cut;

        if (cut)
        {
            m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                snapshots.size() > 1 ? "Cut Clips" : "Cut Clip",
                [this, snapshots]()
                {
                    for (const auto& s : snapshots)
                        m_clipState.removeClip(s.id);
                    m_selection.deselectAll();
                },
                [this, snapshots]()
                {
                    m_selection.deselectAll();
                    for (const auto& s : snapshots)
                    {
                        m_clipState.addClip(s);
                        m_selection.selectClip(s.id);
                    }
                }));
        }
    }

    void ArrangementViewCore::duplicateSelectedClips()
    {
        purgeInvalidSelectedClips();
        auto selected = m_selection.getSelectedIds();
        if (selected.empty()) return;

        // Duplicate EVERY selected clip, each placed right after its source.
        std::vector<ArrangementClipModel> dupes;
        dupes.reserve(selected.size());
        for (auto& id : selected)
        {
            if (auto* c = m_clipState.findClip(id))
            {
                ArrangementClipModel dupe = *c;
                dupe.id        = juce::Uuid();          // new unique ID
                dupe.startTime = c->startTime + c->length;
                dupes.push_back(dupe);
            }
        }

        if (dupes.empty()) return;

        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            dupes.size() > 1 ? "Duplicate Clips" : "Duplicate Clip",
            [this, dupes]()
            {
                m_selection.deselectAll();
                for (const auto& d : dupes)
                {
                    m_clipState.addClip(d);
                    m_selection.selectClip(d.id);
                }
            },
            [this, dupes]()
            {
                m_selection.deselectAll();
                for (const auto& d : dupes)
                    m_clipState.removeClip(d.id);
            }));
    }

    void ArrangementViewCore::onClipDuplicate(ArrangementClipModel* clip)
    {
        purgeInvalidSelectedClips();
        if (!clip) return;
        DBG("[CRASH TRACE] action=clip_duplicate file=ArrangementViewCore track=" << clip->trackIndex
            << " clip=" << formatUuidShort(clip->id));
        ArrangementClipModel dupe = *clip;
        dupe.id        = juce::Uuid();          // new unique ID
        dupe.startTime = clip->startTime + clip->length;

        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            "Duplicate Clip",
            [this, dupe]() { m_clipState.addClip(dupe); m_selection.deselectAll(); m_selection.selectClip(dupe.id); },
            [this, dupe]() { m_clipState.removeClip(dupe.id); m_selection.deselectAll(); }));
    }

    void ArrangementViewCore::onClipDelete(ArrangementClipModel* clip)
    {
        purgeInvalidSelectedClips();
        if (!clip) return;
        DBG("[CRASH TRACE] action=clip_delete file=ArrangementViewCore track=" << clip->trackIndex
            << " clip=" << formatUuidShort(clip->id));

        ArrangementClipModel snapshot = *clip;
        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            "Delete Clip",
            [this, snapshot]() { m_selection.deselectClip(snapshot.id); m_clipState.removeClip(snapshot.id); },
            [this, snapshot]() { m_clipState.addClip(snapshot); m_selection.deselectAll(); m_selection.selectClip(snapshot.id); }));
    }

    void ArrangementViewCore::onClipRename(ArrangementClipModel* clip)
    {
        if (!clip) return;
        // Simple rename: just use the clip name edit inline via properties window,
        // or fall back to a basic input dialog via NativeMessageBox workaround.
        // For now: open properties window focused on name field.
        if (m_propertiesWindow)
            m_propertiesWindow->openForClip(clip);
    }

    void ArrangementViewCore::onClipMute(ArrangementClipModel* clip)
    {
        if (!clip) return;

        const bool before = clip->muted;
        const bool after  = !before;
        const juce::Uuid id = clip->id;

        auto apply = [this, id](bool m)
        {
            if (auto* cc = m_clipState.findClip(id))
            {
                cc->muted = m;
                cc->bumpWaveformVisualVersion();
                m_clipState.updateClip(*cc);
                syncClipToEngine(*cc);
                if (auto* r = findClipRendererFor(id))
                    r->repaint();
            }
        };

        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            after ? "Mute Clip" : "Unmute Clip",
            [apply, after]()  { apply(after);  },
            [apply, before]() { apply(before); }));
    }

    void ArrangementViewCore::onClipPaste(double time, int trackIndex)
    {
        if (m_clipboardClips.empty()) return;

        // Anchor the group: the earliest clip lands exactly at (time, trackIndex);
        // every other clip keeps its relative startTime/trackIndex offset.
        double anchorTime  = std::numeric_limits<double>::max();
        int    anchorTrack = std::numeric_limits<int>::max();
        for (const auto& c : m_clipboardClips)
        {
            anchorTime  = juce::jmin(anchorTime, c.startTime);
            anchorTrack = juce::jmin(anchorTrack, c.trackIndex);
        }

        const double timeDelta  = time - anchorTime;
        const int    trackDelta = trackIndex - anchorTrack;

        std::vector<ArrangementClipModel> pastedClips;
        pastedClips.reserve(m_clipboardClips.size());
        for (const auto& c : m_clipboardClips)
        {
            ArrangementClipModel pasted = c;
            pasted.id         = juce::Uuid();      // fresh ID
            pasted.startTime  = c.startTime + timeDelta;
            pasted.trackIndex = clampTrackIndex(c.trackIndex + trackDelta);
            pastedClips.push_back(pasted);
        }

        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            pastedClips.size() > 1 ? "Paste Clips" : "Paste Clip",
            [this, pastedClips]()
            {
                m_selection.deselectAll();
                for (const auto& p : pastedClips)
                {
                    m_clipState.addClip(p);
                    m_selection.selectClip(p.id);
                }
            },
            [this, pastedClips]()
            {
                m_selection.deselectAll();
                for (const auto& p : pastedClips)
                    m_clipState.removeClip(p.id);
            }));

        // If it was a cut, clear clipboard after first paste
        if (m_clipboardWasCut)
        {
            m_clipboardClips.clear();
            m_clipboardWasCut = false;
        }
    }

    // =========================================================================
    // Clip drag / ghost
    // =========================================================================

    void ArrangementViewCore::startClipDrag(ArrangementClipModel* clip,
                                             juce::Point<int> localPos,
                                             juce::ModifierKeys mods)
    {
        if (!clip) return;
        auto* renderer = findClipRendererFor(clip->id);
        if (!renderer) return;

        const bool shouldDuplicateDrag = mods.isAltDown();

        appendDebugLog("[startClipDrag begin] clickedClip=" + formatUuidShort(clip->id)
            + " duplicate=" + juce::String(shouldDuplicateDrag ? "true" : "false"));

        // Ensure the clicked clip is selected
        if (!m_selection.isSelected(clip->id))
        {
            m_selection.deselectAll();
            m_selection.selectClip(clip->id);
        }

        m_drag.clipId       = clip->id;
        m_drag.duplicate    = shouldDuplicateDrag;
        m_drag.clipIds.clear();
        m_drag.items.clear();
        m_drag.anchorOriginalBounds = renderer->getBounds();

        auto addDragItem = [this](const ArrangementClipModel& sourceClip)
        {
            for (const auto& existing : m_drag.items)
                if (existing.clipId == sourceClip.id)
                    return;

            const auto* selectedRenderer = findClipRendererFor(sourceClip.id);
            juce::Rectangle<int> bounds;
            if (selectedRenderer != nullptr)
            {
                bounds = selectedRenderer->getBounds();
            }
            else
            {
                const int trackH = (int)m_zoom.getTrackHeightPx();
                const int x = timeToScreenX(sourceClip.startTime);
                const int y = arrangementContentTop() + sourceClip.trackIndex * trackH;
                const int w = juce::jmax(4, (int)(sourceClip.visualLength() * m_zoom.getPixelsPerSecond()));
                bounds = { x, y, w, trackH - 2 };
            }

            m_drag.clipIds.push_back(sourceClip.id);
            m_drag.items.push_back({ sourceClip.id, sourceClip, bounds });
            appendDebugLog("[dragItem] id=" + formatUuidShort(sourceClip.id)
                + " name=" + juce::String(sourceClip.clipName)
                + " bounds=" + formatRect(bounds)
                + " track=" + juce::String(sourceClip.trackIndex)
                + " start=" + juce::String(sourceClip.startTime, 3));
        };

        addDragItem(*clip);

        for (const auto& selId : m_selection.getSelectedIds())
        {
            if (selId == clip->id)
                continue;

            if (auto* selectedClip = m_clipState.findClip(selId))
                addDragItem(*selectedClip);
        }

        for (const auto& stateClip : m_clipState.allClips())
        {
            if (m_selection.isSelected(stateClip.id))
                addDragItem(stateClip);
        }

        for (const auto& r : m_clipRenderers)
        {
            if (r != nullptr && r->isSelected() && r->getClip() != nullptr
                && !m_selection.isSelected(r->getClipId()))
            {
                addDragItem(*r->getClip());
            }
        }

        if (m_drag.clipIds.empty())
            addDragItem(*clip);

        for (const auto& item : m_drag.items)
            if (item.clipId == clip->id)
                m_drag.anchorOriginalBounds = item.bounds;

        m_drag.mouseOffsetX = localPos.x - renderer->getX();
        m_drag.mouseOffsetY = localPos.y - renderer->getY();
        m_drag.startMouse   = localPos;
        m_drag.currentMouse = localPos;
        m_drag.pending      = true;
        m_drag.active       = false;

        appendDebugLog("[startClipDrag end] selectionCount=" + juce::String(m_selection.getSelectionCount())
            + " dragItemCount=" + juce::String((int)m_drag.items.size()));
        if (m_selection.getSelectionCount() > 1 && (int)m_drag.items.size() <= 1)
            appendDebugLog("[WARNING] BUG: multi-selection not captured into drag items");

        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::DraggingHandCursor));
        m_ghostOverlay.repaint();
    }

    void ArrangementViewCore::updateClipDrag(juce::Point<int> localPos)
    {
        m_drag.currentMouse = localPos;

        // Activate drag only after moving past a small threshold (avoids
        // triggering rebuild on single-click / double-click).
        if (m_drag.pending && !m_drag.active)
        {
            const int dx = localPos.x - m_drag.startMouse.x;
            const int dy = localPos.y - m_drag.startMouse.y;
            constexpr int kThreshold = 4;
            if (dx * dx + dy * dy >= kThreshold * kThreshold)
            {
                m_drag.active  = true;
                m_drag.pending = false;
            }
        }

        // For a regular (non-duplicate) move, live-move the real renderer
        // components so the actual clip follows the mouse instead of a ghost box.
        // Duplicate drags keep their original renderers in place and show a ghost.
        if (m_drag.active && !m_drag.duplicate)
        {
            const int newAnchorX = computeSnappedDragAnchorX();
            const int newAnchorY = m_drag.currentMouse.y - m_drag.mouseOffsetY;
            const int rawDx = newAnchorX - m_drag.anchorOriginalBounds.getX();
            const int dx = m_lockHorizontal ? 0 : rawDx;
            const int dy = newAnchorY - m_drag.anchorOriginalBounds.getY();

            const int trackH    = (int)m_zoom.getTrackHeightPx();
            const int trackDelta = (dy + (dy >= 0 ? trackH / 2 : -(trackH / 2))) / trackH;
            const int snappedDy  = trackDelta * trackH;

            for (const auto& item : m_drag.items)
            {
                if (auto* renderer = findClipRendererFor(item.clipId))
                {
                    renderer->setBounds(item.bounds.getX() + dx,
                                        item.bounds.getY() + snappedDy,
                                        item.bounds.getWidth(),
                                        item.bounds.getHeight());
                }
            }
        }

        repaint();
    }

    void ArrangementViewCore::commitClipDrag()
    {
        m_drag.pending = false;
        if (!m_drag.active)
        {
            m_drag.clipIds.clear();
            m_drag.items.clear();
            m_drag.clipId = {};
            return;
        }
        purgeInvalidSelectedClips();

        // Compute delta in pixels
        const int newAnchorX  = computeSnappedDragAnchorX();
        const int newAnchorY  = m_drag.currentMouse.y - m_drag.mouseOffsetY;
        const int rawDx = newAnchorX - m_drag.anchorOriginalBounds.getX();
        const int dx = m_lockHorizontal ? 0 : rawDx;
        const int dy = newAnchorY - m_drag.anchorOriginalBounds.getY();

        const int trackH     = (int)m_zoom.getTrackHeightPx();
        const int contentTop = arrangementContentTop();
        const int trackDelta  = (dy + (dy >= 0 ? trackH / 2 : -(trackH / 2))) / trackH;
        const int snappedDy   = trackDelta * trackH;
        std::vector<juce::Uuid> committedIds;

        const bool shouldDuplicateOnCommit = m_drag.duplicate;

        appendDebugLog("[commitDrag] duplicate=" + juce::String(shouldDuplicateOnCommit ? "true" : "false")
            + " itemCount=" + juce::String((int)m_drag.items.size())
            + " dx=" + juce::String(dx)
            + " dy=" + juce::String(dy)
            + " trackDelta=" + juce::String(trackDelta));

        // Move or duplicate every captured clip by the same delta.
        // Guard the entire loop: m_clipState.updateClip fires onClipChanged which
        // triggers engine notifyPropertyChanged -> clipPropertyChanged, which would
        // re-mirror the OLD engine TrackID back and teleport the clip to its original
        // track before syncClipToEngine can write the new one.
        std::vector<ArrangementClipModel> moveBefore, moveAfter;   // for undoable move
        std::vector<ArrangementClipModel> createdDupes;            // for undoable duplicate
        {
            juce::ScopedValueSetter<bool> commitGuard(m_syncingArrangementToEngine, true);

            for (const auto& item : m_drag.items)
            {
                auto* c = m_clipState.findClip(item.clipId);
                if (c == nullptr) continue;

                const int newX       = juce::jmax(0, item.bounds.getX() + dx);
                const double newTime = juce::jmax(0.0, screenXToTime(newX));
                const int newTrack   = clampTrackIndex(item.clip.trackIndex + trackDelta);

                if (shouldDuplicateOnCommit)
                {
                    ArrangementClipModel dupe = item.clip;
                    dupe.id         = juce::Uuid();
                    dupe.startTime  = newTime;
                    dupe.trackIndex = newTrack;
                    m_clipState.addClip(dupe);
                    committedIds.push_back(dupe.id);
                    createdDupes.push_back(dupe);
                }
                else
                {
                    moveBefore.push_back(item.clip);   // original captured model
                    c->startTime  = newTime;
                    c->trackIndex = newTrack;
                    m_clipState.updateClip(*c);
                    syncClipToEngine(*c);
                    committedIds.push_back(c->id);
                    moveAfter.push_back(*c);
                }
        }
        } // commitGuard released — engine feedback re-enabled

        // Moving a clip changes both the source track and the overlap
        // geometry.  Rebuild the prepared relationships once at this control-
        // plane commit boundary; never discover them from the callback.
        refreshAutoCrossfadesForAllTracks();

        // Record the drag as a single unified undo step (skip if nothing moved/changed).
        if (shouldDuplicateOnCommit)
        {
            if (!createdDupes.empty())
                m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                    createdDupes.size() > 1 ? "Duplicate Clips" : "Duplicate Clip",
                    [this, createdDupes]()
                     {
                         m_selection.deselectAll();
                         for (const auto& d : createdDupes) { m_clipState.addClip(d); m_selection.selectClip(d.id); }
                         refreshAutoCrossfadesForAllTracks();
                     },
                     [this, createdDupes]()
                     {
                         for (const auto& d : createdDupes) m_clipState.removeClip(d.id);
                         m_selection.deselectAll();
                         refreshAutoCrossfadesForAllTracks();
                     },
                    true));
        }
        else if (!moveAfter.empty())
        {
            const bool changed = (moveBefore.size() != moveAfter.size()) || [&]
            {
                for (size_t i = 0; i < moveAfter.size(); ++i)
                    if (moveBefore[i].startTime != moveAfter[i].startTime
                        || moveBefore[i].trackIndex != moveAfter[i].trackIndex)
                        return true;
                return false;
            }();

            if (changed)
                m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                    moveAfter.size() > 1 ? "Move Clips" : "Move Clip",
                    [this, moveAfter]()
                    {
                        juce::ScopedValueSetter<bool> g(m_syncingArrangementToEngine, true);
                         for (const auto& a : moveAfter)
                             if (auto* c = m_clipState.findClip(a.id))
                             { c->startTime = a.startTime; c->trackIndex = a.trackIndex; m_clipState.updateClip(*c); syncClipToEngine(*c); }
                         refreshAutoCrossfadesForAllTracks();
                     },
                    [this, moveBefore]()
                    {
                        juce::ScopedValueSetter<bool> g(m_syncingArrangementToEngine, true);
                         for (const auto& b : moveBefore)
                             if (auto* c = m_clipState.findClip(b.id))
                             { c->startTime = b.startTime; c->trackIndex = b.trackIndex; m_clipState.updateClip(*c); syncClipToEngine(*c); }
                         refreshAutoCrossfadesForAllTracks();
                     },
                    true));
        }

        for (auto& r : m_clipRenderers)
        {
            r->setVisible(true);
            r->setAlpha(1.0f);
        }

        m_drag.active = false;
        m_drag.duplicate = false;
        m_drag.clipIds.clear();
        m_drag.items.clear();
        m_drag.anchorOriginalBounds = {};
        m_ghostOverlay.setVisible(false);
        if (!committedIds.empty())
        {
            m_selection.deselectAll();
            for (const auto& id : committedIds)
                m_selection.selectClip(id);
        }

        setMouseCursorForActiveTool();
        rebuildClipRenderers();
        repaint();
    }

    void ArrangementViewCore::cancelClipDrag()
    {
        // Restore visibility
        for (auto& r : m_clipRenderers)
        {
            r->setVisible(true);
            r->setAlpha(1.0f);
        }

        appendDebugLog("[cancelDrag]");

        m_drag.active = false;
        m_drag.pending = false;
        m_drag.duplicate = false;
        m_drag.clipIds.clear();
        m_drag.items.clear();
        m_drag.anchorOriginalBounds = {};
        m_ghostOverlay.setVisible(false);
        setMouseCursorForActiveTool();
        rebuildClipRenderers();
        repaint();
    }

    void ArrangementViewCore::drawGhost(juce::Graphics& g)
    {
        // Compute pixel delta from drag anchor
        const int newAnchorX  = computeSnappedDragAnchorX();
        const int newAnchorY  = m_drag.currentMouse.y - m_drag.mouseOffsetY;
        const int rawDx = newAnchorX - m_drag.anchorOriginalBounds.getX();
        const int dx = m_lockHorizontal ? 0 : rawDx;
        const int dy = newAnchorY - m_drag.anchorOriginalBounds.getY();

        const int trackH     = (int)m_zoom.getTrackHeightPx();
        const int contentTop = arrangementContentTop();
        const int trackCount = getArrangementTrackCount();

        // Snap dy to nearest track lane increment
        const int trackDelta  = (dy + (dy >= 0 ? trackH / 2 : -(trackH / 2))) / trackH;
        const int snappedDy   = trackDelta * trackH;

        appendDebugLog("[drawGhost] active=" + juce::String(m_drag.active ? "true" : "false")
            + " duplicate=" + juce::String(m_drag.duplicate ? "true" : "false")
            + " itemCount=" + juce::String((int)m_drag.items.size()));

        // One-time draw of the drop-lane highlight for the anchor clip
        {
            const int destTrack = clampTrackIndex(
                (m_drag.anchorOriginalBounds.getY() + snappedDy - contentTop) / trackH);
            const int laneY = contentTop + destTrack * trackH;
            const auto* anchorClip = m_clipState.findClip(m_drag.clipId);
            if (anchorClip)
            {
                g.setColour(anchorClip->colour.withAlpha(0.07f));
                g.fillRect(0, laneY, getWidth(), trackH);
            }
        }

        // Snap line at new left edge of anchor clip
        const int snapLineX = newAnchorX;
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawVerticalLine(snapLineX, (float)contentTop,
                           (float)(contentTop + trackCount * trackH));

        // Draw ghost for every clip captured at drag start.
        juce::String ghostLogSignature;
        for (const auto& item : m_drag.items)
        {
            const auto& clip = item.clip;
            const int origX  = item.bounds.getX();
            const int clipW  = juce::jmax(4, item.bounds.getWidth());
            const int clipH  = juce::jmax(8, item.bounds.getHeight());

            // Use the captured renderer Y so ghosts follow the exact visual rows
            // where the selected clips started, including cross-track selections.
            const int ghostY = item.bounds.getY() + snappedDy;
            const int destTrack = clampTrackIndex((ghostY - contentTop) / trackH);
            const int ghostX = origX + dx;

            const juce::Rectangle<float> ghostRect((float)ghostX,
                                                   (float)ghostY + 1.f,
                                                   (float)clipW,
                                                   (float)(clipH - 3));

            if (ghostRect.getWidth() < 4.f || ghostRect.getHeight() < 4.f)
            {
                ghostLogSignature << formatUuidShort(item.clipId) << ":skip;";
                continue;
            }

            ghostLogSignature << formatUuidShort(item.clipId) << ":" << ghostX << "," << ghostY << ";";

            // For a regular move, no ghost box is drawn — the real renderer stays visible.
            // Only duplicate drags get the full ghost treatment.
            if (!m_drag.duplicate)
                continue;

            const auto accent = clip.colour.brighter(m_drag.duplicate ? 1.0f : 0.45f);
            const auto glass  = clip.colour.interpolatedWith(juce::Colours::white, m_drag.duplicate ? 0.34f : 0.20f);

            // ── Drop shadow ──────────────────────────────────────────────
            g.setColour(juce::Colours::black.withAlpha(m_drag.duplicate ? 0.42f : 0.22f));
            g.fillRoundedRectangle(ghostRect.translated(0.f, 6.f).expanded(2.0f, 2.0f), 7.f);

            // ── Body gradient ─────────────────────────────────────────────
            const float gradBot = ghostRect.getBottom();
            const float gradTop = ghostRect.getY();
            juce::ColourGradient body(glass.withAlpha(m_drag.duplicate ? 0.88f : 0.64f),
                                      0.f, gradTop,
                                      clip.colour.darker(0.50f).withAlpha(m_drag.duplicate ? 0.68f : 0.36f),
                                      0.f, juce::jmax(gradTop + 1.f, gradBot), false);
            g.setGradientFill(body);
            g.fillRoundedRectangle(ghostRect, 7.f);

            // ── Gloss sheen ───────────────────────────────────────────────
            const float sheenH = juce::jmin(18.f, ghostRect.getHeight() * 0.42f);
            if (sheenH > 1.f)
            {
                g.setColour(juce::Colours::white.withAlpha(m_drag.duplicate ? 0.28f : 0.12f));
                g.fillRoundedRectangle(ghostRect.reduced(2.f).withHeight(sheenH), 5.f);
            }

            // ── Header strip ──────────────────────────────────────────────
            const float headerH = juce::jmin(16.f, ghostRect.getHeight() * 0.45f);
            if (headerH > 2.f)
            {
                const juce::Rectangle<float> header(ghostRect.getX(), ghostRect.getY(),
                                                    ghostRect.getWidth(), headerH);
                juce::ColourGradient hg(accent.withAlpha(m_drag.duplicate ? 0.98f : 0.74f),
                                        0.f, header.getY(),
                                        clip.colour.withAlpha(m_drag.duplicate ? 0.74f : 0.44f),
                                        0.f, juce::jmax(header.getY() + 1.f, header.getBottom()), false);
                g.setGradientFill(hg);
                g.fillRoundedRectangle(header, 7.f);
                if (headerH > 7.f)
                    g.fillRect(header.withTrimmedTop(7.f));

                // ── Clip name ─────────────────────────────────────────────
                g.setColour(juce::Colours::white.withAlpha(0.94f));
                g.setFont(juce::Font("Segoe UI", 9.5f, juce::Font::bold));
                g.drawText(juce::String(clip.clipName),
                           (int)ghostRect.getX() + 5, (int)ghostRect.getY() + 1,
                           (int)ghostRect.getWidth() - (m_drag.duplicate ? 25 : 10),
                           (int)headerH - 2,
                           juce::Justification::centredLeft, true);
            }

            // ── Duplicate badge (+) ───────────────────────────────────────
            if (m_drag.duplicate && ghostRect.getWidth() >= 22.f)
            {
                const juce::Rectangle<float> badge(ghostRect.getRight() - 19.f,
                                                   ghostRect.getY() + 3.f, 12.f, 12.f);
                g.setColour(juce::Colours::white.withAlpha(0.98f));
                g.drawRoundedRectangle(badge, 3.f, 1.2f);
                g.drawLine(badge.getCentreX(), badge.getY() + 3.f,
                           badge.getCentreX(), badge.getBottom() - 3.f, 1.2f);
                g.drawLine(badge.getX() + 3.f, badge.getCentreY(),
                           badge.getRight() - 3.f, badge.getCentreY(), 1.2f);
            }

            // ── Waveform ghost — real waveform snapshot ───────────────────
            const float waveAreaTop = ghostRect.getY() + juce::jmin(16.f, ghostRect.getHeight() * 0.45f);
            const float waveAreaH   = ghostRect.getBottom() - waveAreaTop;
            if (waveAreaH > 4.f)
            {
                bool drewRealWaveform = false;
                if (auto* renderer = findClipRendererFor(item.clipId))
                {
                    // Capture a snapshot of the real clip renderer and paint it
                    // into the ghost's waveform area, preserving the actual waveform.
                    auto snapshot = renderer->createComponentSnapshot(renderer->getLocalBounds(), true, 1.0f);
                    if (snapshot.isValid())
                    {
                        g.saveState();
                        // Clip to the waveform area of the ghost rectangle
                        g.reduceClipRegion(juce::Rectangle<float>(ghostRect.getX(), waveAreaTop,
                                                                    ghostRect.getWidth(), waveAreaH).toNearestInt());
                        g.setOpacity(m_drag.duplicate ? 0.82f : 0.72f);
                        // Scale the snapshot to fill the ghost bounds so waveform aligns correctly
                        g.drawImage(snapshot,
                                    ghostRect.getX(), ghostRect.getY(),
                                    ghostRect.getWidth(), ghostRect.getHeight(),
                                    0, 0, renderer->getWidth(), renderer->getHeight());
                        g.restoreState();
                        drewRealWaveform = true;
                    }
                }

                if (!drewRealWaveform)
                {
                    // Fallback: simplified mid-line + bar silhouette
                    const float waveY = waveAreaTop + waveAreaH * 0.5f;
                    g.setColour(juce::Colours::white.withAlpha(m_drag.duplicate ? 0.60f : 0.34f));
                    g.drawHorizontalLine((int)waveY, ghostRect.getX() + 4.f, ghostRect.getRight() - 4.f);
                    const int barCount = juce::jmin(24, clipW / 6);
                    if (barCount > 1)
                    {
                        const float step = (ghostRect.getWidth() - 8.f) / (float)barCount;
                        for (int b = 0; b < barCount; ++b)
                        {
                            const float bx = ghostRect.getX() + 4.f + b * step;
                            const float bh = 4.f + 8.f * (float)(b % 3 == 0 ? 1 : (b % 2 == 0 ? 2 : 3)) / 3.f;
                            const float by = waveY - bh * 0.5f;
                            if (bh > 0.f && step * 0.55f > 0.f)
                                g.fillRect(bx, by, step * 0.55f, bh);
                        }
                    }
                }
            }

            // ── Border ────────────────────────────────────────────────────
            g.setColour(accent.withAlpha(m_drag.duplicate ? 1.0f : 0.86f));
            g.drawRoundedRectangle(ghostRect.reduced(0.5f), 7.f, m_drag.duplicate ? 2.2f : 1.4f);
            g.setColour(juce::Colours::white.withAlpha(m_drag.duplicate ? 0.36f : 0.14f));
            g.drawRoundedRectangle(ghostRect.reduced(2.0f), 5.f, m_drag.duplicate ? 1.1f : 0.8f);
        }

        if (ghostLogSignature != m_lastGhostLogSignature)
        {
            m_lastGhostLogSignature = ghostLogSignature;
            for (const auto& item : m_drag.items)
            {
                const int ghostY = item.bounds.getY() + snappedDy;
                const int destTrack = clampTrackIndex((ghostY - contentTop) / trackH);
                const int ghostX = item.bounds.getX() + dx;
                const juce::Rectangle<int> ghostRect(ghostX, ghostY + 1,
                    juce::jmax(4, item.bounds.getWidth()), juce::jmax(8, item.bounds.getHeight()) - 3);
                const bool visible = ghostRect.getWidth() >= 4 && ghostRect.getHeight() >= 4;
                appendDebugLog("[ghostItem] id=" + formatUuidShort(item.clipId)
                    + " ghostX=" + juce::String(ghostX)
                    + " ghostY=" + juce::String(ghostY)
                    + " destTrack=" + juce::String(destTrack)
                    + " rect=" + formatRect(ghostRect)
                    + " visible=" + juce::String(visible ? "yes" : "no"));
            }
            if (areAllDragItemBoundsIdentical())
                appendDebugLog("[WARNING] BUG: drag item bounds identical, findClipRendererFor likely wrong");
            if (m_drag.items.size() > 1 && areAllGhostPositionsIdentical())
                appendDebugLog("[WARNING] BUG: ghosts overlap perfectly");
            if (trackCount > 3)
            {
                for (const auto& item : m_drag.items)
                {
                    const int rawDestY = item.bounds.getY() + snappedDy;
                    if (((rawDestY - contentTop) / trackH) > 2)
                    {
                        appendDebugLog("[WARNING] BUG: hardcoded 3-track clamp");
                        break;
                    }
                }
            }
        }

        // ── Time tooltip at snap line ─────────────────────────────────────
        const double ghostTime = juce::jmax(0.0, screenXToTime(snapLineX));
        const int    totalSec  = (int)ghostTime;
        const int    mins      = totalSec / 60;
        const int    secs      = totalSec % 60;
        const int    ms        = (int)((ghostTime - totalSec) * 1000.0);
        const juce::String timeLabel =
            juce::String::formatted("%d:%02d.%03d", mins, secs, ms);

        const int labelW = 84, labelH = 17;
        int labelX = snapLineX + 4;
        if (labelX + labelW > getWidth()) labelX = snapLineX - labelW - 4;
        const int labelY = contentTop - labelH - 2;

        g.setColour(juce::Colour(0xEE111111));
        g.fillRoundedRectangle((float)labelX, (float)labelY,
                               (float)labelW, (float)labelH, 3.f);
        const auto* anchorClipForLabel = m_clipState.findClip(m_drag.clipId);
        g.setColour(anchorClipForLabel ? anchorClipForLabel->colour.brighter(0.3f)
                                       : juce::Colours::white);
        g.setFont(juce::Font("Segoe UI", 8.5f, juce::Font::bold));
        g.drawText(timeLabel, labelX + 5, labelY + 1,
                   labelW - 10, labelH - 2, juce::Justification::centredLeft);
    }

    // ========================================================================
    // Waveform Loading Timer for batch imports
    // ========================================================================

    void ArrangementViewCore::WaveformLoadTimer::timerCallback()
    {
        if (m_owner.m_waveformLoadQueue.empty())
        {
            stopTimer();
            return;
        }

        // Process up to kWaveformLoadBatchSize clips per timer tick to avoid blocking UI
        int processed = 0;
        while (!m_owner.m_waveformLoadQueue.empty() && processed < kWaveformLoadBatchSize)
        {
            const juce::Uuid clipId = m_owner.m_waveformLoadQueue.front();
            m_owner.m_waveformLoadQueue.pop();

            if (auto* renderer = m_owner.findClipRendererFor(clipId))
            {
                if (renderer->isVisible()
                    && renderer->shouldRenderWaveform()
                    && renderer->getClip() != nullptr
                    && !renderer->getClip()->sourcePath.empty()
                    && renderer->needsWaveformUpdate())
                {
                    // Request waveform cache to generate peaks asynchronously
                    // This triggers peak calculation in background without blocking
                    auto& cache = renderer->getWaveformCache();
                    cache.requestPeaksUpdate();
                    renderer->markWaveformUpdateComplete();
                    renderer->repaint();
                }
            }
            ++processed;
        }

        // If more clips to process, continue timer
        if (!m_owner.m_waveformLoadQueue.empty())
        {
            startTimer(16);  // Next tick at ~60 FPS
        }
        else
        {
            stopTimer();
        }
    }

} // namespace ArrangementEditor

// ============================================================================
// AUTOMATION SUPPORT (Phase 1) - Simplified implementation
// ============================================================================

namespace ArrangementEditor {

void ArrangementViewCore::setAutomationManager(DAW::AutomationManagerCore* automationManager)
{
    if (m_automationUpdateTimer)
    {
        m_automationUpdateTimer->stopTimer();
        m_automationUpdateTimer = nullptr;
    }

    m_automationManager = automationManager;
    m_automationContainers.clear();

    if (m_automationManager)
    {
        m_automationHelper = std::make_unique<DAW::AutomationUIHelper>(*m_automationManager);

        // Start update timer for curve invalidation (33ms = ~30 FPS)
        m_automationUpdateTimer = std::make_unique<AutomationUpdateTimer>(*this);
        m_automationUpdateTimer->startTimer(33);
    }
    else
    {
        m_automationHelper = nullptr;
    }
}

DAW::AutomationLaneContainerComponent* ArrangementViewCore::getOrCreateAutomationContainer(
    const DAW::TrackID& trackId)
{
    auto it = m_automationContainers.find(trackId);
    if (it != m_automationContainers.end())
        return it->second.get();

    // Lazily create a fallback manager+helper if none was wired externally
    if (!m_automationHelper)
    {
        if (!m_fallbackAutomationManager)
            m_fallbackAutomationManager = std::make_unique<DAW::AutomationManagerCore>();

        if (!m_automationManager)
            m_automationManager = m_fallbackAutomationManager.get();

        m_automationHelper = std::make_unique<DAW::AutomationUIHelper>(*m_automationManager);
    }

    auto container = std::make_unique<DAW::AutomationLaneContainerComponent>(
        *m_automationHelper, trackId
    );
    auto* ptr = container.get();

    addAndMakeVisible(container.get());
    container->setEnabled(true);
    m_automationContainers[trackId] = std::move(container);

    resized();
    return ptr;
}

void ArrangementViewCore::showAutomationLane(
    const DAW::TrackID& trackId, const juce::String& parameterId)
{
    const auto apexId = apexIdForVisibleTarget(trackId, parameterId);
    const bool hasApexData = hasApexAutomationLane(apexId);
    bool useComponentLane = true;

    if (m_automationManager != nullptr)
    {
        if (const auto* coreLane = m_automationManager->findLane(trackId, parameterId))
            useComponentLane = coreLane->isEnabled() && !coreLane->points.empty();
        else
            useComponentLane = false;
    }

    if (hasApexData && !useComponentLane)
    {
        if (auto it = m_automationContainers.find(trackId); it != m_automationContainers.end())
            it->second->removeLane(parameterId);

        updateAutomationLayout();
        repaint();
        DBG("[TIMELINE-RECOVERY] showAutomationLane track=" << trackId << " target=" << parameterId << " owner=apex");
        return;
    }

    auto* container = getOrCreateAutomationContainer(trackId);
    if (!container)
        return;

    if (!container->hasLane(parameterId))
        container->addLane(parameterId);

    container->setLaneVisible(parameterId, true);
    updateAutomationLayout();
        rebuildClipRenderers();
    updateClipAutomationDim();
    repaintAutomationLane(trackId, parameterId);
    DBG("[TIMELINE-RECOVERY] showAutomationLane track=" << trackId << " target=" << parameterId);
}

void ArrangementViewCore::hideAutomationLane(
    const DAW::TrackID& trackId, const juce::String& parameterId)
{
    auto it = m_automationContainers.find(trackId);
    if (it != m_automationContainers.end())
    {
        it->second->removeLane(parameterId);
        updateAutomationLayout();
        rebuildClipRenderers();
        updateClipAutomationDim();
        repaint();
    }
}

void ArrangementViewCore::clearAutomationLanes(const DAW::TrackID& trackId)
{
    auto it = m_automationContainers.find(trackId);
    if (it != m_automationContainers.end())
    {
        it->second->clearAllLanes();
        updateAutomationLayout();
        rebuildClipRenderers();
        updateClipAutomationDim();
        repaint();
    }
}

void ArrangementViewCore::updateAutomationLayout()
{
    // Send all clip renderers to the back first
    for (auto& r : m_clipRenderers)
        r->toBack();

    for (auto& [trackId, container] : m_automationContainers)
    {
        if (container == nullptr)
            continue;

        const auto laneBounds = getTrackLaneBounds(trackId);
        if (laneBounds.isEmpty())
        {
            container->setVisible(false);
            continue;
        }

        if (container->getNumLanes() == 0)
        {
            container->setVisible(false);
            continue;
        }

        // Clip headers are reserved for clip interaction. The automation
        // overlay starts BELOW the header strip so the clip stays grabbable
        // (select, drag, header badges, piano-roll button) while automation
        // lanes are visible — the header is not an automation surface.
        constexpr int kClipHeaderStrip = 16;   // ClipRenderCore::kNameH
        const int headerStrip = juce::jlimit(0, juce::jmax(0, laneBounds.getHeight() - 8),
                                             kClipHeaderStrip);
        const int height = laneBounds.getHeight() - headerStrip;
        const int y = laneBounds.getY() + headerStrip;

        container->setVisible(true);
        container->setBounds(laneBounds.getX(), y, laneBounds.getWidth(), height);
        container->toFront(false);
        container->setSamplesPerPixel(juce::jmax(1.0, m_engineSampleRate / juce::jmax(1.0, m_zoom.getPixelsPerSecond())));
        container->setZoomLevel(m_zoom.getPixelsPerSecond());
        container->setSampleRate(m_engineSampleRate);
        container->setTempoBpm(m_tempoBpm);
        container->repaint();

        DBG("[TIMELINE-RECOVERY] automation lane bounds track=" << trackId
            << " y=" << y << " h=" << height);
    }

    // Re-front the pinned toolbar/ruler AFTER every container toFront so the
    // Quick New Track button always wins hit-testing in the pinned strip.
    ensureToolbarZOrder();
}

void ArrangementViewCore::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    for (auto& [trackId, container] : m_automationContainers)
    {
        if (container != nullptr)
        {
            container->invalidateAllCurvePaths();
            container->repaint();
        }
    }
    updateAutomationLayout();
    repaint();
}

int ArrangementViewCore::getYForTrackId(const DAW::TrackID& trackId) const
{
    const auto bounds = getTrackLaneBounds(trackId);
    return bounds.isEmpty() ? -1 : bounds.getY();
}

juce::Rectangle<int> ArrangementViewCore::getTrackLaneBounds(const DAW::TrackID& trackId) const
{
    if (trackId.isEmpty())
        return {};

    // Collapsed folder: children get no lane bounds — automation containers
    // (and any consumer of this helper) hide automatically.
    if (m_engineTrackManager != nullptr)
        if (auto* track = m_engineTrackManager->getTrack(trackId))
            if (isTrackHiddenByCollapsedFolder(track))
                return {};

    const int trackH = juce::jmax(1, (int)m_zoom.getTrackHeightPx());
    int laneIndex = -1;

    if (m_engineTrackManager != nullptr)
    {
        if (m_engineTrackManager->hasMasterTrack())
            if (auto* master = m_engineTrackManager->getMasterTrack())
                if (master->getID() == trackId)
                    laneIndex = 0;

        if (laneIndex < 0)
        {
            const int regularIndex = m_engineTrackManager->getTrackIndex(trackId);
            if (regularIndex >= 0)
                laneIndex = regularIndex + (m_engineTrackManager->hasMasterTrack() ? 1 : 0);
        }
    }

    // Fallback: support "track_N" synthetic IDs used when no engine track manager is wired
    if (laneIndex < 0 && trackId.startsWith("track_"))
    {
        laneIndex = trackId.fromFirstOccurrenceOf("track_", false, false).getIntValue();
    }

    if (laneIndex < 0)
        return {};

    return { 0, arrangementContentTop() + laneIndex * trackH, getWidth(), trackH };
}

juce::Rectangle<int> ArrangementViewCore::getClipBounds(const juce::Uuid& clipId) const
{
    if (auto* renderer = findClipRendererFor(clipId))
        return renderer->getBounds();
    return {};
}

DAW::TrackID ArrangementViewCore::getTrackAtY(int y) const
{
    if (m_engineTrackManager == nullptr)
        return {};

    const int laneIndex = (y - arrangementContentTop()) / juce::jmax(1, (int)m_zoom.getTrackHeightPx());
    if (laneIndex < 0)
        return {};

    if (m_engineTrackManager->hasMasterTrack())
    {
        if (laneIndex == 0)
            if (auto* master = m_engineTrackManager->getMasterTrack())
                return master->getID();

        if (auto* track = m_engineTrackManager->getTrack(laneIndex - 1))
            return track->getID();
    }
    else if (auto* track = m_engineTrackManager->getTrack(laneIndex))
    {
        return track->getID();
    }

    return {};
}

std::vector<DAW::TrackID> ArrangementViewCore::getVisibleTrackIds() const
{
    std::vector<DAW::TrackID> ids;
    if (m_engineTrackManager == nullptr)
        return ids;

    if (m_engineTrackManager->hasMasterTrack())
        if (auto* master = m_engineTrackManager->getMasterTrack())
            ids.push_back(master->getID());

    for (int i = 0; i < m_engineTrackManager->getNumTracks(); ++i)
        if (auto* track = m_engineTrackManager->getTrack(i))
            if (!isTrackHiddenByCollapsedFolder(track))
                ids.push_back(track->getID());

    return ids;
}

void ArrangementViewCore::setCollapsedFolderIds(const std::unordered_set<DAW::TrackID>& ids)
{
    if (m_collapsedFolderIds == ids)
        return;

    m_collapsedFolderIds = ids;
    rebuildClipRenderers();
    updateAutomationLayout();
    repaint();
}

bool ArrangementViewCore::isTrackHiddenByCollapsedFolder(const DAW::Track* track) const
{
    if (track == nullptr || track->isMaster() || m_collapsedFolderIds.empty())
        return false;

    DAW::TrackID parentId = track->getParentTrackID();
    int depth = 0;
    while (parentId.isNotEmpty() && depth < 32)
    {
        if (m_collapsedFolderIds.count(parentId) > 0)
            return true;

        if (m_engineTrackManager == nullptr)
            break;
        auto* parent = m_engineTrackManager->getTrack(parentId);
        if (parent == nullptr)
            break;
        parentId = parent->getParentTrackID();
        ++depth;
    }
    return false;
}

DAW::TrackID ArrangementViewCore::resolveSelectedTrack() const
{
    for (const auto& id : m_selection.getSelectedIds())
        if (auto* clip = m_clipState.findClip(id))
            if (m_engineTrackManager != nullptr)
            {
                const int masterOffset = m_engineTrackManager->hasMasterTrack() ? 1 : 0;
                if (auto* track = m_engineTrackManager->getTrack(clip->trackIndex - masterOffset))
                    return track->getID();
            }

    return resolveArmedTrack();
}

DAW::TrackID ArrangementViewCore::resolveArmedTrack() const
{
    if (m_engineTrackManager == nullptr)
        return {};

    for (int i = 0; i < m_engineTrackManager->getNumTracks(); ++i)
        if (auto* track = m_engineTrackManager->getTrack(i); track != nullptr && track->isArmed())
            return track->getID();

    return {};
}

void ArrangementViewCore::repaintAutomationLane(const DAW::TrackID& trackId, const juce::String& target)
{
    auto it = m_automationContainers.find(trackId);
    if (it != m_automationContainers.end() && it->second != nullptr)
    {
        if (auto* lane = it->second->getLane(target))
        {
            lane->invalidateCurvePath();
            lane->repaint();
        }
        it->second->invalidateAllCurvePaths();
        it->second->repaint();
    }
}

void ArrangementViewCore::repaintClip(const juce::Uuid& clipId)
{
    if (auto* renderer = findClipRendererFor(clipId))
        renderer->repaint();
}

// ===========================================================================
// Diagnostics / benchmark access
// ===========================================================================
ArrangementViewCore::WaveformReadinessSnapshot
ArrangementViewCore::getVisibleWaveformReadinessForDiagnostics() const
{
    WaveformReadinessSnapshot snap;

    // Determine visible viewport rectangle in arrangement coordinates
    int viewW = getWidth();
    int viewH = getHeight();
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        viewW = juce::jmax(1, viewport->getViewWidth());
        viewH = juce::jmax(1, viewport->getViewHeight());
    }
    const juce::Rectangle<int> visibleRect(m_viewportScrollX, m_viewportScrollY, viewW, viewH);
    snap.viewportWidthPx  = viewW;
    snap.viewportHeightPx = viewH;

    for (const auto& renderer : m_clipRenderers)
    {
        if (renderer == nullptr)
        {
            ++snap.missingRenderer;
            continue;
        }

        // Skip renderers outside the visible viewport
        if (!visibleRect.intersects(renderer->getBounds()))
            continue;

        // Count all renderers intersecting viewport (audio, MIDI, pattern)
        ++snap.totalVisibleRenderers;

        // Check if this is an audio clip (identified by non-empty sourcePath)
        const auto* clip = renderer->getClip();
        if (clip == nullptr || clip->sourcePath.empty())
            continue;

        // Audio clip inside viewport
        ++snap.visibleAudioRenderers;

        // Access waveform cache under its existing lock
        auto& cache = renderer->getWaveformCache();
        {
            juce::ScopedLock sl(cache.getPeaksLock());
            if (cache.isReady())
                ++snap.ready;
            else
                ++snap.notReady;
        }
    }

    return snap;
}

} // namespace ArrangementEditor
