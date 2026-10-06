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
#include <set>
#include "../Source/ClipCore/Clip.h"
#include "../Source/MidiCore/MidiClip.h"
#include "../Source/AudioEngineCore/AudioFileManager.h"
#include "../Source/TrackCore/Track.h"
#include <functional>

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t bg = 0xFF1E1E1E;
        constexpr uint32_t trackBg = 0xFF252525;
        constexpr uint32_t trackLine = 0xFF181818;
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

        static void addOrReplaceApexPoint(const juce::String& parameterKey, double timeSeconds, float normalisedValue)
        {
            if (parameterKey.isEmpty())
                return;

            auto& registry = apex::automation::AutomationParameterKeyRegistry::getInstance();
            const auto id = registry.getOrCreateID(parameterKey);
            auto& lane = apex::automation::AutomationLaneStore::getInstance().getOrCreateLane(id);
            auto snap = lane.getSnapshot();
            apex::automation::AutomationLane::PointVector next = snap ? *snap : apex::automation::AutomationLane::PointVector{};
            constexpr double beatsPerSecondFallback = 2.0;
            const double ppq = juce::jmax(0.0, timeSeconds) * beatsPerSecondFallback;
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
            // TODO: Open Piano Roll window
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Piano Roll",
                "Piano Roll will open here.\nDouble-click MIDI clips to edit notes.",
                "OK");
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
            for (auto& renderer : m_clipRenderers)
            {
                auto* clip = m_clipState.findClip(renderer->getClip()->id);
                if (clip)
                    renderer->setSelected(m_selection.isSelected(clip->id));
            }

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
        m_rubberBand.onSelectionComplete = [this](juce::Rectangle<int> bounds) {
            std::set<juce::Uuid> selectedIds;
            for (auto& renderer : m_clipRenderers)
            {
                if (renderer != nullptr && bounds.intersects(renderer->getBounds()))
                    if (auto* clip = renderer->getClip())
                        selectedIds.insert(clip->id);
            }

            m_selection.setSelection(selectedIds);
            appendDebugLog("[rubberBandSelection] bounds=" + formatRect(bounds)
                + " selected=" + juce::String((int)selectedIds.size()));
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
                                                   double                 engineSampleRate)
    {
        m_engineClipManager   = clipManager;
        m_engineAudioFiles    = audioFileManager;
        m_engineTrackManager  = trackManager;
        m_engineSampleRate    = juce::jmax(1.0, engineSampleRate);

        if (m_engineClipManager == nullptr || m_engineAudioFiles == nullptr)
            return;

        // ---- onClipAdded: create a matching engine clip and cache the ID ----
        m_clipState.onClipAdded = [this](const juce::Uuid& uuid)
        {
            rebuildClipRenderers();

            if (m_suppressEngineClipCreation)
                return;

            if (m_engineClipManager == nullptr || m_engineAudioFiles == nullptr)
                return;

            auto* aclip = m_clipState.findClip(uuid);
            if (!aclip || aclip->sourcePath.empty())
                return;

            // CRITICAL: Ensure clip defaults prevent BPM-based stretching.
            // Force stretchRatio=1.0 and mode=Resample (0) for all imported/recorded clips.
            if (aclip->timePitch.stretchRatio != 1.0)
            {
                DBG("[ClipInit] Resetting stretchRatio from " << aclip->timePitch.stretchRatio << " to 1.0");
                aclip->timePitch.stretchRatio = 1.0;
            }
            if (static_cast<int>(aclip->timePitch.mode) != 0) // 0 = Resample
            {
                DBG("[ClipInit] Resetting mode from " << static_cast<int>(aclip->timePitch.mode) << " to 0 (Resample)");
                aclip->timePitch.mode = TimePitchMode::Resample;
            }

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
                        mutClip->sourceTotalSamples = (int64_t)res.numSamples;
                }
            }

            const double srcRate = m_engineAudioFiles->getSourceSampleRate(ec->getID());
            ec->setStartPosition((DAW::SamplePosition)std::llround(aclip->startTime * srcRate));
            ec->setLength(juce::jmax((DAW::SamplePosition)1,
                          (DAW::SamplePosition)std::llround(aclip->length * srcRate)));
            ec->setSourceOffset(juce::jmax((DAW::SamplePosition)0,
                                (DAW::SamplePosition)std::llround(aclip->sourceOffset * srcRate)));
            if (aclip->sourceEndSample > aclip->sourceStartSample)
            {
                ec->setSourceStartSample(aclip->sourceStartSample);
                ec->setSourceEndSample(aclip->sourceEndSample);
            }
            ec->setMuted(aclip->muted);

            // Map trackIndex → TrackID
            if (m_engineTrackManager != nullptr && aclip->trackIndex < m_engineTrackManager->getNumTracks())
                if (auto* t = m_engineTrackManager->getTrack(aclip->trackIndex))
                    ec->setTrackID(t->getID());

            m_uuidToEngineId[uuid] = ec->getID();

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

            // Sync the fields the audio thread reads without entering the raw pitch trap.
            ec->setPitchTargetSemitones(static_cast<float>(aclip->timePitch.pitchSemitones));
            ec->setGain     (aclip->gain);

            // CRITICAL FIX: Force stretchRatio to 1.0 (no stretching) for all clips by default.
            // Clips should NOT stretch based on BPM unless explicitly set to FollowProjectTempo.
            double actualStretch = aclip->timePitch.stretchRatio;
            if (actualStretch != 1.0)
            {
                DBG("[EngineSync] WARNING: Clip stretchRatio=" << actualStretch 
                    << " (should be 1.0 for Fixed mode). Forcing to 1.0.");
            }
            ec->setTimeStretch(1.0f); // FORCE: Always 1.0 = no time stretch

            ec->setFineTuneCents(static_cast<float>(aclip->timePitch.fineTuneCents));

            // FORCE mode to Resample (mode 0) to ensure no automatic stretching.
            // User can manually change to Stretch/Vocal/etc if they want tempo-follow.
            int actualMode = static_cast<int>(aclip->timePitch.mode);
            int forcedMode = 0; // Resample = 0
            if (actualMode != forcedMode)
            {
                DBG("[EngineSync] WARNING: Clip mode=" << actualMode 
                    << " (should be 0=Resample). Forcing to 0.");
            }
            ec->setTimePitchMode(forcedMode);

            DBG("[EngineSync] onClipChanged uuid=" << uuid.toString()
                << " pitch=" << aclip->timePitch.pitchSemitones
                << " stretch=" << actualStretch << " (forced to 1.0)"
                << " mode=" << actualMode << " (forced to 0=Resample)");
        };
    }

    void ArrangementViewCore::setPlayheadPosition(double timeSeconds)
    {
        timeSeconds = juce::jmax(0.0, timeSeconds);
        if (std::abs(m_playheadTimeSeconds - timeSeconds) < 0.0001)
            return;

        m_playheadTimeSeconds = timeSeconds;
        m_ruler.setPlayheadPosition(timeSeconds);
        repaint();
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
                upsertClipRenderer(updated.id);
                repaint();
                return;
            }
        }

        m_suppressEngineClipCreation = true;
        m_clipState.addClip(model);
        m_suppressEngineClipCreation = false;
        m_uuidToEngineId[model.id] = engineClipId;
        upsertClipRenderer(model.id);
        repaint();
    }

    void ArrangementViewCore::paint(juce::Graphics& g)
    {
        g.fillAll(fromU32(Col::bg));

        // Draw track lanes
        int trackH = (int)m_zoom.getTrackHeightPx();
        int y = arrangementContentTop();
        const int trackCount = getArrangementTrackCount();

        for (int i = 0; i < trackCount; ++i)
        {
            g.setColour(fromU32(i % 2 == 0 ? Col::trackBg : Col::bg));
            g.fillRect(0, y, getWidth(), trackH);

                g.setColour(fromU32(Col::trackLine));
                    g.drawHorizontalLine(y + trackH - 1, 0.f, (float)getWidth());

                    y += trackH;
                }

            }

    void ArrangementViewCore::drawPhase1VolumeAutomation(juce::Graphics& g)
    {
        if (m_engineTrackManager == nullptr)
            return;

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
            g.setColour(juce::Colour(0xFF0F1720).withAlpha(0.84f));
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
        }
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

        constexpr double beatsPerSecondFallback = 2.0; // 120 BPM fallback until lane UI receives transport tempo.
        juce::Path path;
        bool started = false;
        std::vector<juce::Point<float>> visiblePoints;
        visiblePoints.reserve(snap->size());

        for (size_t i = 0; i < snap->size(); ++i)
        {
            const auto& bp = snap->at(i);
            const float x = (float)m_zoom.timeToX(bp.timePPQ / beatsPerSecondFallback);
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
        // Automation lanes drawn here so they always appear above clip components
        drawPhase1VolumeAutomation(g);

        if (m_drag.active)
            drawGhost(g);

        const int x = timeToScreenX(m_playheadTimeSeconds);
        if (x >= 0 && x <= getWidth())
        {
            const auto accent = juce::Colour(0xFFE07B39);
            const int top = m_ruler.getBottom();

            g.setColour(accent.withAlpha(0.24f));
            g.fillRect(x - 2, top, 5, juce::jmax(0, getHeight() - top));
            g.setColour(accent);
            g.drawVerticalLine(x, (float)top, (float)getHeight());
        }

        if (m_arrangementDebugPanelVisible && m_arrangementDebugPanel != nullptr)
            m_arrangementDebugPanel->toFront(false);
    }

    void ArrangementViewCore::resized()
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

        m_toolbar.setBounds(m_viewportScrollX, m_viewportScrollY, juce::jmax(120, viewportWidth - 90), toolbarH);
        m_pianoRollBtn.setBounds(m_viewportScrollX + viewportWidth - 76, m_viewportScrollY + 8, 70, 32);
        m_ruler.setBounds(m_viewportScrollX, m_viewportScrollY + toolbarH, viewportWidth, rulerH);
        m_ghostOverlay.setBounds(getLocalBounds());

        if (m_arrangementDebugPanel != nullptr)
            m_arrangementDebugPanel->setBounds(juce::Rectangle<int>(juce::jmax(20, w - 460), toolbarH + rulerH + 8, 440, juce::jmax(220, getHeight() - (toolbarH + rulerH + 16))));

        rebuildClipRenderers();
        updateAutomationLayout();
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
        resized();
        repaint();
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
            auto* clip = (*it != nullptr) ? (*it)->getClip() : nullptr;
            if (clip == nullptr || liveClipIds.find(clip->id) == liveClipIds.end())
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

        // Automation containers must always sit above clips in z-order
        for (auto& [trackId, container] : m_automationContainers)
            if (container != nullptr && container->isVisible())
                container->toFront(false);

        updateClipAutomationDim();

        // Queue waveform loads for any clips that need them
        for (auto& clip : m_clipState.allClips())
            if (auto* renderer = findClipRendererFor(clip.id))
                if (renderer->needsWaveformUpdate())
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

        if (auto* renderer = findClipRendererFor(clipId))
        {
            renderer->setBounds(x, y, w, h);
            renderer->setSelected(m_selection.isSelected(clip->id));
            renderer->repaint();
            return;
        }

        auto renderer = std::make_unique<ClipRenderCore>(
            const_cast<ArrangementClipModel&>(*clip), m_zoom);
        renderer->setBounds(x, y, w, h);
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

            // ── Auto-crossfade ────────────────────────────────────────────
            // After any trim/resize commit, if this clip now overlaps the
            // next/previous clip on the same track, paint a crossfade
            // (complementary fade-out / fade-in across the overlap region).
            // Matches Pro Tools / Logic / Live behavior. Applied BEFORE the
            // history push below so the whole commit (trim/resize + painted
            // neighbour fades) lands on the unified history as ONE step —
            // a single Ctrl+Z reverts the edit and the crossfade together.
            struct FadePatch { juce::Uuid id; float beforeIn, beforeOut, afterIn, afterOut; };
            std::vector<FadePatch> fadePatches;

            if (auto* edited = m_clipState.findClip(clipUuid))
            {
                for (const auto& other : m_clipState.allClips())
                {
                    if (other.id == edited->id) continue;
                    if (other.trackIndex != edited->trackIndex) continue;

                    const double aStart = edited->startTime;
                    const double aEnd   = edited->startTime + edited->length;
                    const double bStart = other.startTime;
                    const double bEnd   = other.startTime + other.length;

                    // Overlap region
                    const double overlapStart = juce::jmax(aStart, bStart);
                    const double overlapEnd   = juce::jmin(aEnd,   bEnd);
                    const double overlapLen   = overlapEnd - overlapStart;
                    if (overlapLen <= 0.001) continue;
                    if (overlapLen > 5.0)    continue; // sanity cap — 5 s

                    auto* otherPtr = m_clipState.findClip(other.id);
                    if (otherPtr == nullptr) continue;

                    FadePatch patch { other.id,
                                      otherPtr->fadeInLength, otherPtr->fadeOutLength,
                                      otherPtr->fadeInLength, otherPtr->fadeOutLength };

                    // Edited is on the LEFT (ends inside other) → fade-out on
                    // edited, fade-in on other.
                    if (aStart < bStart)
                    {
                        edited->fadeOutLength = juce::jmax(edited->fadeOutLength, (float)overlapLen);
                        otherPtr->fadeInLength = juce::jmax(otherPtr->fadeInLength, (float)overlapLen);
                    }
                    else // edited is on the RIGHT
                    {
                        edited->fadeInLength = juce::jmax(edited->fadeInLength, (float)overlapLen);
                        otherPtr->fadeOutLength = juce::jmax(otherPtr->fadeOutLength, (float)overlapLen);
                    }

                    patch.afterIn  = otherPtr->fadeInLength;
                    patch.afterOut = otherPtr->fadeOutLength;
                    if (patch.afterIn != patch.beforeIn || patch.afterOut != patch.beforeOut)
                        fadePatches.push_back(patch);

                    edited->bumpWaveformVisualVersion();
                    otherPtr->bumpWaveformVisualVersion();
                    m_clipState.updateClip(*edited);
                    m_clipState.updateClip(*otherPtr);
                    syncClipToEngine(*edited);
                    syncClipToEngine(*otherPtr);
                }
            }

            // Re-capture the edited clip: the crossfade may have extended its
            // own fades beyond the state captured at commit time.
            ArrangementClipModel afterFinal = after;
            if (auto* editedNow = m_clipState.findClip(clipUuid))
                afterFinal = *editedNow;

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

            auto applyFades = [this](const std::vector<FadePatch>& patches, bool useAfter)
            {
                for (const auto& p : patches)
                {
                    if (auto* cc = m_clipState.findClip(p.id))
                    {
                        cc->fadeInLength  = useAfter ? p.afterIn  : p.beforeIn;
                        cc->fadeOutLength = useAfter ? p.afterOut : p.beforeOut;
                        cc->bumpWaveformVisualVersion();
                        juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);
                        m_clipState.updateClip(*cc);
                        syncClipToEngine(*cc);
                    }
                }
            };

            m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                "Edit Clip",
                [applyState, applyFades, afterFinal, fadePatches]() { applyState(afterFinal); applyFades(fadePatches, true);  },
                [applyState, applyFades, before, fadePatches]()     { applyState(before);     applyFades(fadePatches, false); },
                /*alreadyApplied*/ true));
        };

        addAndMakeVisible(*renderer);
        m_clipRenderers.push_back(std::move(renderer));

        // Always keep automation containers above clips
        for (auto& [trackId, container] : m_automationContainers)
            if (container != nullptr && container->isVisible())
                container->toFront(false);
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
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
        case EditorTool::Split:
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            break;
        case EditorTool::RazorEdit:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
        case EditorTool::Eraser:
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            break;
        case EditorTool::Draw:
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            break;
        case EditorTool::Glue:
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            break;
        case EditorTool::Mute:
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            break;
        case EditorTool::Zoom:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case EditorTool::TimeScrub:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
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
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            return;
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
                                addOrReplaceApexPoint(activeParameterId, timeSeconds, normalised);
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

            // Rubber band select
            if (!findClipAt(e.getPosition()))
                m_rubberBand.updateDrag(e.getPosition());
        }
    }

    void ArrangementViewCore::mouseUp(const juce::MouseEvent& e)
    {
        DBG("[CRASH TRACE] action=mouseUp file=ArrangementViewCore track=" << trackIndexAt(e.getPosition())
            << " clip=" << formatUuidShort(m_lastMouseDownClipId));
        m_lastMousePos = e.getPosition();
        m_lastMouseMods = e.mods;

        if (m_middleMousePanActive)
        {
            m_middleMousePanActive = false;
            setMouseCursorForActiveTool();
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
            purgeInvalidSelectedClips();
            auto selected = m_selection.getSelectedIds();
            if (!selected.empty())
                if (auto* c = m_clipState.findClip(*selected.begin()))
                    onClipCopy(c);
            return true;
        }
        if (key == juce::KeyPress('x', juce::ModifierKeys::ctrlModifier, 0))
        {
            purgeInvalidSelectedClips();
            auto selected = m_selection.getSelectedIds();
            if (!selected.empty())
                if (auto* c = m_clipState.findClip(*selected.begin()))
                    onClipCut(c);
            return true;
        }
        if (key == juce::KeyPress('v', juce::ModifierKeys::ctrlModifier, 0))
        {
            // Paste at end of clipboard clip's original position + length
            if (m_clipboardClip.has_value())
                onClipPaste(m_clipboardClip->startTime + m_clipboardClip->length,
                            m_clipboardClip->trackIndex);
            return true;
        }
        if (key == juce::KeyPress('d', juce::ModifierKeys::ctrlModifier, 0))
        {
            purgeInvalidSelectedClips();
            auto selected = m_selection.getSelectedIds();
            if (!selected.empty())
                if (auto* c = m_clipState.findClip(*selected.begin()))
                    onClipDuplicate(c);
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
            // Start rubber band select
            if (!e.mods.isCtrlDown())
                m_selection.deselectAll();

            m_rubberBand.startDrag(e.getPosition());
            appendDebugLog("[postSelect] coreSelectionCount=" + juce::String(m_selection.getSelectionCount())
                + " selectedIds=" + juce::String());
            if (onEngineClipSelectionCleared)
                onEngineClipSelectionCleared();
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

        const juce::Uuid origUuid = clip->id;

        // ── Undo context ──────────────────────────────────────────────────
        // Captured across the engine + visual mutation below so the whole
        // split lands on the unified history as ONE undoable step (pro-DAW:
        // Ctrl+Z restores the original unsplit clip, redo re-splits).
        struct SplitUndoCtx
        {
            ArrangementClipModel   origVisual, leftVisual, rightVisual;
            juce::ValueTree        origEngineState, leftEngineState, rightEngineState;
            DAW::AudioFileManager::CachedAudioHandle audioSnapshot;
            juce::String           origEngineId, leftEngineId, rightEngineId;
            bool                   hasEngine = false;
        };
        auto undoCtx = std::make_shared<SplitUndoCtx>();
        undoCtx->origVisual = *clip;

        auto result = ClipSplitCore::splitClip(*clip, splitTime);

        if (!result.valid)
        {
            DBG("[Split] ClipSplitCore returned invalid result");
            return;
        }

        DBG("===== SPLIT DEBUG START =====");
        DBG("[Split] sourcePath=" << result.left.sourcePath);
        DBG("[Split] Left  start=" << result.left.startTime  << " len=" << result.left.length  << " srcOffset=" << result.left.sourceOffset);
        DBG("[Split] Right start=" << result.right.startTime << " len=" << result.right.length << " srcOffset=" << result.right.sourceOffset);
        DBG("[Split] Bounds left=" << (juce::int64)result.left.sourceStartSample << ".." << (juce::int64)result.left.sourceEndSample
            << " right=" << (juce::int64)result.right.sourceStartSample << ".." << (juce::int64)result.right.sourceEndSample);

        bool engineSplitCompleted = false;

        // ------------------------------------------------------------------
        // 1. Split the real DAW audio engine clip, but do not let engine
        //    clip-added/removed callbacks rebuild the arrangement. Blade split
        //    owns the visual model update below.
        // ------------------------------------------------------------------
        if (m_engineClipManager != nullptr && m_engineAudioFiles != nullptr)
        {
            auto mapIt = m_uuidToEngineId.find(origUuid);
            if (mapIt != m_uuidToEngineId.end())
            {
                const juce::String oid = mapIt->second;

                // Capture engine clip state
                auto* origEngineClip = m_engineClipManager->getClip(oid);
                if (origEngineClip != nullptr)
                {
                    auto leftState  = origEngineClip->getState();
                    auto rightState = origEngineClip->getState();
                    undoCtx->origEngineState = origEngineClip->getState();
                    auto originalAudioSnapshot = m_engineAudioFiles->getCachedAudioSnapshot(oid);
                    auto* origAudioClip = dynamic_cast<DAW::AudioClip*>(origEngineClip);

                    const auto origStart  = origEngineClip->getStartPosition();
                    const auto origLen    = origEngineClip->getLength();
                    const auto origTimelineLen = origAudioClip != nullptr
                        ? origAudioClip->getProcessedTimelineLength()
                        : origLen;
                    const auto origSrcOff = origEngineClip->getSourceOffset();
                    const double srcRate  = m_engineAudioFiles->getSourceSampleRate(oid);
                    const auto sourceTotalSamples = m_engineAudioFiles->getSourceNumSamples(oid);
                    DBG("[Split] EngineSource sr=" << srcRate << " total=" << (juce::int64)sourceTotalSamples
                        << " origSrcOff=" << (juce::int64)origSrcOff);
                    const double totalTimelineSec = juce::jmax(0.000001, clip->visualLength());
                    const double leftTimelineSec  = juce::jmax(0.0, splitTime - clip->startTime);
                    const double splitFraction    = juce::jlimit(0.0, 1.0, leftTimelineSec / totalTimelineSec);

                    const auto leftEngineLen  = (DAW::SamplePosition)std::round(
                        (double)origLen * splitFraction);
                    const auto rightEngineLen = origLen - leftEngineLen;
                    const auto leftTimelineLen = (DAW::SamplePosition) std::round(
                        (double) origTimelineLen * splitFraction);
                    const auto splitSample    = origStart + leftTimelineLen;
                    const auto rightSrcOffset = origSrcOff
                        + (DAW::SamplePosition)std::round((double)leftEngineLen * (srcRate / m_engineSampleRate));

                    if (result.left.sourceEndSample <= result.left.sourceStartSample
                        || result.right.sourceEndSample <= result.right.sourceStartSample)
                    {
                        const int64_t sourceStart = juce::jmax((int64_t)0, (int64_t)origSrcOff);
                        const int64_t sourceSplit = juce::jmax(sourceStart + 1, (int64_t)rightSrcOffset);
                        int64_t sourceEnd = juce::jmax(sourceSplit + 1,
                            sourceStart + (int64_t)std::llround(juce::jmax(0.0, clip->length) * juce::jmax(1.0, srcRate)));

                        if (sourceTotalSamples > 0)
                            sourceEnd = juce::jlimit(sourceSplit + 1, (int64_t)sourceTotalSamples, sourceEnd);

                        result.left.sourceStartSample = sourceStart;
                        result.left.sourceEndSample = sourceSplit;
                        result.right.sourceStartSample = sourceSplit;
                        result.right.sourceEndSample = sourceEnd;
                    }

                    leftState .setProperty("length",        (juce::int64)leftEngineLen,  nullptr);
                    rightState.setProperty("startPosition", (juce::int64)splitSample,    nullptr);
                    rightState.setProperty("length",        (juce::int64)rightEngineLen, nullptr);
                    rightState.setProperty("sourceOffset",  (juce::int64)rightSrcOffset, nullptr);
                    if (origAudioClip != nullptr)
                    {
                        leftState .setProperty("sourceStartSample", (juce::int64)result.left.sourceStartSample,   nullptr);
                        leftState .setProperty("sourceEndSample",   (juce::int64)result.left.sourceEndSample,     nullptr);
                        rightState.setProperty("sourceStartSample", (juce::int64)result.right.sourceStartSample,  nullptr);
                        rightState.setProperty("sourceEndSample",   (juce::int64)result.right.sourceEndSample,    nullptr);
                    }
                    if (leftState .hasProperty("fadeOutLength")) leftState .setProperty("fadeOutLength", (juce::int64)0, nullptr);
                    if (rightState.hasProperty("fadeInLength"))  rightState.setProperty("fadeInLength",  (juce::int64)0, nullptr);

                    DAW::Clip* leftClip = nullptr;
                    DAW::Clip* rightClip = nullptr;

                    {
                        juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);

                        // Remove the original visual->engine mapping before deleting the engine clip.
                        // deleteClip() notifies ArrangementView::clipRemoved() synchronously; keep
                        // that mirror path suppressed so blade split remains the sole visual mutation.
                        m_uuidToEngineId.erase(origUuid);
                        m_engineClipManager->deleteClip(oid);

                        // Create two replacement engine clips. Their clipAdded callbacks are also
                        // suppressed; arrangement state is updated from result.left/right below.
                        leftClip  = m_engineClipManager->recreateClipFromState(leftState);
                        rightClip = m_engineClipManager->recreateClipFromState(rightState);
                    }

                    // Share cached audio buffer — no file reload
                    if (leftClip)  { m_engineAudioFiles->shareSnapshotForClip(originalAudioSnapshot, leftClip->getID());  m_uuidToEngineId[result.left.id]  = leftClip->getID(); }
                    if (rightClip) { m_engineAudioFiles->shareSnapshotForClip(originalAudioSnapshot, rightClip->getID()); m_uuidToEngineId[result.right.id] = rightClip->getID(); }
                    engineSplitCompleted = (leftClip != nullptr && rightClip != nullptr);

                    if (engineSplitCompleted)
                    {
                        undoCtx->hasEngine        = true;
                        undoCtx->leftEngineState  = leftState;
                        undoCtx->rightEngineState = rightState;
                        undoCtx->audioSnapshot    = originalAudioSnapshot;
                        undoCtx->origEngineId     = oid;
                        undoCtx->leftEngineId     = leftClip->getID();
                        undoCtx->rightEngineId    = rightClip->getID();
                    }

                    DBG("[Split] Engine: left="  << (leftClip  ? leftClip->getID()  : "FAILED")
                        << " right=" << (rightClip ? rightClip->getID() : "FAILED")
                        << " rightSrcOffset=" << (juce::int64)rightSrcOffset);
                }
            }
            else
            {
                DBG("[Split] WARNING: origUuid not in engine map - bridge may not be wired");
            }
        }
        else
        {
            DBG("[Split] No engine bridge - visual-only split. Call setAudioEngineBridge().");
        }

        // ------------------------------------------------------------------
        // 2. Update visual clip state
        // ------------------------------------------------------------------
        if (m_engineClipManager != nullptr && m_engineAudioFiles != nullptr)
        {
            juce::ScopedValueSetter<bool> suppressEngineCreation(m_suppressEngineClipCreation, true);
            m_clipState.removeClip(origUuid);

            if (engineSplitCompleted)
            {
                m_clipState.addClip(result.left);
                m_clipState.addClip(result.right);
            }
        }
        else
        {
            m_clipState.removeClip(origUuid);
            m_clipState.addClip(result.left);
            m_clipState.addClip(result.right);
        }

        // ── History ───────────────────────────────────────────────────────
        // Record only when the split fully succeeded (engine split completed,
        // or a pure visual split with no engine bridge wired).
        const bool recordSplitHistory = engineSplitCompleted
            || m_engineClipManager == nullptr || m_engineAudioFiles == nullptr;

        if (recordSplitHistory)
        {
            undoCtx->leftVisual  = result.left;
            undoCtx->rightVisual = result.right;

            const juce::Uuid leftUuid  = result.left.id;
            const juce::Uuid rightUuid = result.right.id;
            auto ctx = undoCtx;

            auto redoFn = [this, ctx, origUuid, leftUuid, rightUuid]()
            {
                if (ctx->hasEngine && m_engineClipManager != nullptr && m_engineAudioFiles != nullptr)
                {
                    juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);
                    auto it = m_uuidToEngineId.find(origUuid);
                    if (it != m_uuidToEngineId.end())
                    {
                        const juce::String currentOrigId = it->second;
                        m_uuidToEngineId.erase(origUuid);
                        m_engineClipManager->deleteClip(currentOrigId);
                    }
                    if (auto* l = m_engineClipManager->recreateClipFromState(ctx->leftEngineState))
                    {
                        ctx->leftEngineId = l->getID();
                        m_engineAudioFiles->shareSnapshotForClip(ctx->audioSnapshot, l->getID());
                        m_uuidToEngineId[leftUuid] = l->getID();
                    }
                    if (auto* r = m_engineClipManager->recreateClipFromState(ctx->rightEngineState))
                    {
                        ctx->rightEngineId = r->getID();
                        m_engineAudioFiles->shareSnapshotForClip(ctx->audioSnapshot, r->getID());
                        m_uuidToEngineId[rightUuid] = r->getID();
                    }
                }
                {
                    juce::ScopedValueSetter<bool> suppress(m_suppressEngineClipCreation, true);
                    m_clipState.removeClip(origUuid);
                    m_clipState.addClip(ctx->leftVisual);
                    m_clipState.addClip(ctx->rightVisual);
                }
                repaint();
            };

            auto undoFn = [this, ctx, origUuid, leftUuid, rightUuid]()
            {
                if (ctx->hasEngine && m_engineClipManager != nullptr && m_engineAudioFiles != nullptr)
                {
                    juce::ScopedValueSetter<bool> syncGuard(m_syncingArrangementToEngine, true);
                    m_uuidToEngineId.erase(leftUuid);
                    m_uuidToEngineId.erase(rightUuid);
                    if (ctx->leftEngineId.isNotEmpty())  m_engineClipManager->deleteClip(ctx->leftEngineId);
                    if (ctx->rightEngineId.isNotEmpty()) m_engineClipManager->deleteClip(ctx->rightEngineId);
                    if (auto* o = m_engineClipManager->recreateClipFromState(ctx->origEngineState))
                    {
                        ctx->origEngineId = o->getID();
                        m_engineAudioFiles->shareSnapshotForClip(ctx->audioSnapshot, o->getID());
                        m_uuidToEngineId[origUuid] = o->getID();
                    }
                }
                {
                    juce::ScopedValueSetter<bool> suppress(m_suppressEngineClipCreation, true);
                    m_clipState.removeClip(leftUuid);
                    m_clipState.removeClip(rightUuid);
                    m_clipState.addClip(ctx->origVisual);
                }
                repaint();
            };

            m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
                "Split Clip", std::move(redoFn), std::move(undoFn), /*alreadyApplied*/ true));
        }

        DBG("===== SPLIT DEBUG END =====");
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
        newClip.colour = juce::Colour(0xFFE07B39);

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
            if (renderer->getClip() && renderer->getClip()->id == clipId)
                return renderer.get();
        return nullptr;
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
            return m_clipState.findClip(renderer->getClip()->id);
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
                if (auto* clip = renderer->getClip())
                    ids.push_back(clip->id);
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
        cb.canPaste      = [this]() { return m_clipboardClip.has_value(); };
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
        cb.canPaste   = [this]() { return m_clipboardClip.has_value(); };
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
                renderer->getWaveformCache().requestPeaksFromBuffer(buf, w);
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

        // Create undo action
        m_undo.perform(new TimePitchSetStateAction(
            clip->id, juce::String(clip->clipName),
            oldState, clip->timePitch,
            [this, clipId = clip->id](const TimePitchState& s)
            {
                if (auto* c = m_clipState.findClip(clipId))
                {
                    c->timePitch = s;
                    rebuildClipRenderers();
                }
            }));

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
                    rebuildClipRenderers();
                }
            }));

        rebuildClipRenderers();
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
        DBG("[CRASH TRACE] action=clip_cut file=ArrangementViewCore track=" << clip->trackIndex
            << " clip=" << formatUuidShort(clip->id));
        m_clipboardClip   = *clip;
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
        DBG("[CRASH TRACE] action=clip_copy file=ArrangementViewCore track=" << clip->trackIndex
            << " clip=" << formatUuidShort(clip->id));
        m_clipboardClip   = *clip;
        m_clipboardWasCut = false;
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
        if (!m_clipboardClip.has_value()) return;
        ArrangementClipModel pasted = *m_clipboardClip;
        pasted.id         = juce::Uuid();      // fresh ID
        pasted.startTime  = time;
        pasted.trackIndex = clampTrackIndex(trackIndex);

        m_undo.executeCommand(std::make_unique<LambdaArrangementCommand>(
            "Paste Clip",
            [this, pasted]() { m_clipState.addClip(pasted); m_selection.deselectAll(); m_selection.selectClip(pasted.id); },
            [this, pasted]() { m_clipState.removeClip(pasted.id); m_selection.deselectAll(); }));

        // If it was a cut, clear clipboard after first paste
        if (m_clipboardWasCut)
        {
            m_clipboardClip.reset();
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
                && !m_selection.isSelected(r->getClip()->id))
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

        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
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
            const int dx = newAnchorX - m_drag.anchorOriginalBounds.getX();
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
        const int dx = newAnchorX - m_drag.anchorOriginalBounds.getX();
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
                    },
                    [this, createdDupes]()
                    {
                        for (const auto& d : createdDupes) m_clipState.removeClip(d.id);
                        m_selection.deselectAll();
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
                    },
                    [this, moveBefore]()
                    {
                        juce::ScopedValueSetter<bool> g(m_syncingArrangementToEngine, true);
                        for (const auto& b : moveBefore)
                            if (auto* c = m_clipState.findClip(b.id))
                            { c->startTime = b.startTime; c->trackIndex = b.trackIndex; m_clipState.updateClip(*c); syncClipToEngine(*c); }
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
        const int dx = newAnchorX - m_drag.anchorOriginalBounds.getX();
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
                if (renderer->needsWaveformUpdate())
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

        const int height = laneBounds.getHeight();
        const int y = laneBounds.getY();

        container->setVisible(true);
        container->setBounds(laneBounds.getX(), y, laneBounds.getWidth(), height);
        container->toFront(false);
        container->setSamplesPerPixel(juce::jmax(1.0, m_engineSampleRate / juce::jmax(1.0, m_zoom.getPixelsPerSecond())));
        container->setZoomLevel(m_zoom.getPixelsPerSecond());
        container->repaint();

        DBG("[TIMELINE-RECOVERY] automation lane bounds track=" << trackId
            << " y=" << y << " h=" << height);
    }
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
            ids.push_back(track->getID());

    return ids;
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

} // namespace ArrangementEditor
