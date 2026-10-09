#pragma once

// ── Bubblegum V2 System ────────────────────────────────────────────────────
// Send-only routing system + real-time visual feedback,
// fully synchronized with the selected track.
//
// Fast. Precise. Minimal. Professional.
// Inspired by Harrison Mixbus, modernized visually.

#include "BubblegumSourceSyncCore.h"
#include "BubblegumPanelCore.h"
#include "BubblegumTargetListCore.h"
#include "BubblegumSendStateCore.h"
#include "BubblegumSendLevelCore.h"
#include "BubblegumInputCore.h"
#include "BubblegumVisualCore.h"
#include "BubblegumTransitionCore.h"
#include "BubblegumSendFeedbackCore.h"
#include "BubblegumTrackPulseCore.h"
#include "BubblegumVisualLinkCore.h"
#include "../BubblegumCable/BubblegumCableSystem.h"
#include "BubblegumOffscreenDetectionCore.h"
#include "BubblegumSidechainStateCore.h"
#include "BubblegumSidechainTargetListCore.h"
#include "../BubblegumCable/BubblegumCableSidechainRenderCore.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../RoutingCore/MasterRouteStateCore.h"
#include "../TrackCore/Track.h"
#include "../Automation/AutomationSystemCore.h"
#include "../Automation/AutomationParameterKeyCore.h"
#include <unordered_map>
#include <unordered_set>

namespace DAW {

/**
 * BubblegumV2System — aggregates all Bubblegum V2 cores.
 *
 * All cores are independent. This struct owns them and provides
 * a high-level API for the mixer integration layer.
 *
 * Requires external RoutingGraph and TrackManager references.
 */
struct BubblegumV2System : public apex::automation::AutomationParameter::Listener
{
    // ── Independent cores ────────────────────────────────────────────────
    BubblegumSourceSyncCore   sourceSync;
    BubblegumPanelCore        panel;
    BubblegumTargetListCore   targetList;
    BubblegumSendStateCore    sendState;
    BubblegumSendLevelCore    sendLevel;
    BubblegumInputCore        input;
    BubblegumVisualCore       visual;
    BubblegumTransitionCore   transition;
    BubblegumSendFeedbackCore sendFeedback;
    BubblegumTrackPulseCore   trackPulse;
    BubblegumVisualLinkCore   visualLink;
    BubblegumCableSystem      liquidCable;   // new modular cable system (geometry/render/effects/debug nucleos)
    BubblegumOffscreenDetectionCore     offscreenDetector;
    BubblegumSidechainStateCore         sidechainState;
    BubblegumSidechainTargetListCore    sidechainTargetList;
    BubblegumCableSidechainRenderCore   sidechainCable;

    // ── External references (set once during init) ───────────────────────
    RoutingGraph* routingGraph = nullptr;
    TrackManager* trackManager = nullptr;
    std::function<void(std::function<void()>, const juce::String&)> onTopologyMutationRequested;
    std::function<void(const juce::String&)> onContinuousTopologyGestureBegin;
    std::function<void(const juce::String&)> onContinuousTopologyGestureCommit;

    struct StringHash
    {
        std::size_t operator() (const juce::String& s) const noexcept { return (std::size_t) s.hashCode64(); }
    };

    struct SendAutomationBinding
    {
        TrackID source;
        TrackID target;
        RouteID routeID;
        bool isBypass = false;
    };

    std::unordered_map<apex::automation::ParameterID, SendAutomationBinding> sendAutomationBindings;
    std::unordered_set<apex::automation::ParameterID> openSendLevelGestures;
    bool applyingSendAutomation = false;

    void parameterValueChanged (apex::automation::AutomationParameter& p,
                                float newNormalized,
                                apex::automation::ChangeSource source) override
    {
        if (applyingSendAutomation || source == apex::automation::ChangeSource::User)
            return;

        auto it = sendAutomationBindings.find(p.getID());
        if (it == sendAutomationBindings.end() || routingGraph == nullptr)
            return;

        const juce::ScopedValueSetter<bool> guard(applyingSendAutomation, true);
        const auto& binding = it->second;
        if (auto* srcNode = routingGraph->getNodeByTrackId(binding.source))
        {
            for (auto* conn : routingGraph->getOutputConnections(srcNode->id))
            {
                if (conn == nullptr || conn->id != binding.routeID)
                    continue;

                const float n = juce::jlimit(0.0f, 1.0f, newNormalized);
                if (binding.isBypass)
                    conn->active.store(n < 0.5f, std::memory_order_relaxed);
                else
                    conn->gain.store(n * 2.0f, std::memory_order_relaxed);
                routingGraph->publishSnapshotOnly();
                return;
            }
        }
    }

    /**
     * Tear down project-scoped send parameter/listener registrations BEFORE
     * replacing a project's routing and automation trees. Old IDs are not
     * authoritative in the new session; leaving them registered can route a
     * newly restored curve to a connection from the previous project.
     *
     * Message thread only. Must be invoked before APEXAutomation restore:
     * removeNativeParameter() also removes the previous project's lane.
     */
    void releaseProjectSendAutomationBindings()
    {
        auto& sys = apex::automation::AutomationSystem::getInstance();
        auto& registry = sys.getRegistry();
        for (auto id : openSendLevelGestures)
            if (auto* param = registry.find(id))
                param->endGesture();
        openSendLevelGestures.clear();

        for (const auto& binding : sendAutomationBindings)
        {
            if (auto* param = registry.find(binding.first))
                param->removeListener(this);
            sys.removeNativeParameter(binding.first);
        }
        sendAutomationBindings.clear();
    }

    /**
     * Rebind only the saved lanes that actually exist on live Send edges.
     * ProjectManager restores RoutingGraph + APEXAutomation first, so stable
     * source-track/route IDs and lane/mode state are already authoritative.
     *
     * No beginGesture(), no setValueFromUser(), and NO mutation of the
     * saved lane or static send gain: automation playback owns those values.
     */
    void rebindPersistedSendAutomation()
    {
        if (routingGraph == nullptr)
            return;

        auto& sys = apex::automation::AutomationSystem::getInstance();
        auto& keys = apex::automation::AutomationParameterKeyRegistry::getInstance();
        auto& lanes = sys.getLaneStore();

        for (auto* conn : routingGraph->getAllConnections())
        {
            if (conn == nullptr || !BubblegumSendStateCore::isSendConnection(conn->type))
                continue;

            auto* srcNode = routingGraph->getNode(conn->sourceNodeId);
            auto* dstNode = routingGraph->getNode(conn->destNodeId);
            if (srcNode == nullptr || dstNode == nullptr
                || srcNode->trackId.isEmpty() || dstNode->trackId.isEmpty())
                continue;

            const auto bindIfSaved = [&](bool bypass)
            {
                const auto key = bypass
                    ? apex::automation::AutomationParameterKeyRegistry::trackSendBypassKey(srcNode->trackId, conn->id)
                    : apex::automation::AutomationParameterKeyRegistry::trackSendLevelKey(srcNode->trackId, conn->id);
                const auto id = keys.findID(key);
                if (id == apex::automation::kInvalidParameterID)
                    return;

                const auto lane = lanes.findLane(id);
                if (lane == nullptr || lane->isEmpty())
                    return;

                auto* param = sys.getRegistry().find(id);
                if (param == nullptr)
                {
                    apex::automation::ParameterRange range;
                    range.minValue = 0.0f;
                    range.maxValue = 1.0f;
                    range.defaultValue = bypass ? 0.0f : 0.5f;
                    range.isStepped = bypass;
                    range.numSteps = bypass ? 2 : 0;
                    param = sys.createNativeParameter(id,
                        "Send " + conn->id + (bypass ? " Bypass" : " Level"), range);
                }
                if (param == nullptr)
                    return;

                sendAutomationBindings[id] = { srcNode->trackId, dstNode->trackId, conn->id, bypass };
                param->addListener(this);
            };

            bindIfSaved(false);
            bindIfSaved(true);
        }
    }

    void parameterGestureBegan (apex::automation::AutomationParameter&) override {}
    void parameterGestureEnded (apex::automation::AutomationParameter&) override {}

    void performTopologyMutation(std::function<void()> mutation, const juce::String& description)
    {
        if (!mutation)
            return;

        if (onTopologyMutationRequested)
            onTopologyMutationRequested(std::move(mutation), description);
        else
            mutation();
    }

    void beginContinuousTopologyGesture(const juce::String& description)
    {
        if (onContinuousTopologyGestureBegin)
            onContinuousTopologyGestureBegin(description);
    }

    void commitContinuousTopologyGesture(const juce::String& description)
    {
        if (onContinuousTopologyGestureCommit)
            onContinuousTopologyGestureCommit(description);

        auto& sys = apex::automation::AutomationSystem::getInstance();
        for (auto id : openSendLevelGestures)
            if (auto* param = sys.getRegistry().find(id))
                param->endGesture();
        openSendLevelGestures.clear();
    }

    void publishSendLevelAutomationGesture(const TrackID& source, const TrackID& targetId, bool endGestureNow = false)
    {
        if (routingGraph == nullptr || source.isEmpty() || targetId.isEmpty())
            return;

        auto* srcNode = routingGraph->getNodeByTrackId(source);
        auto* dstNode = routingGraph->getNodeByTrackId(targetId);
        if (srcNode == nullptr || dstNode == nullptr)
            return;

        RoutingConnection* sendConn = nullptr;
        for (auto* conn : routingGraph->getOutputConnections(srcNode->id))
        {
            if (conn != nullptr && conn->destNodeId == dstNode->id && BubblegumSendStateCore::isSendConnection(conn->type))
            {
                sendConn = conn;
                break;
            }
        }

        if (sendConn == nullptr)
            return;

        const auto key = apex::automation::AutomationParameterKeyRegistry::trackSendLevelKey(source, sendConn->id);
        const auto id = apex::automation::AutomationParameterKeyRegistry::getInstance().getOrCreateID(key);
        auto& sys = apex::automation::AutomationSystem::getInstance();
        auto* param = sys.getRegistry().find(id);
        if (param == nullptr)
        {
            apex::automation::ParameterRange range;
            range.minValue = 0.0f;
            range.maxValue = 1.0f;
            range.defaultValue = 0.5f;
            range.skew = 1.0f;
            range.isStepped = false;
            param = sys.createNativeParameter(id, "Send " + sendConn->id + " Level", range);
            const auto volumeId = apex::automation::AutomationParameterKeyRegistry::getInstance()
                .findID(apex::automation::AutomationParameterKeyRegistry::trackVolumeKey(source));
            if (volumeId != apex::automation::kInvalidParameterID)
                sys.setMode(id, sys.getModeState().getMode(volumeId));
        }

        if (param == nullptr)
            return;

        sendAutomationBindings[id] = { source, targetId, sendConn->id, false };
        param->addListener(this);

        const float gain = juce::jlimit(0.0f, 2.0f, sendConn->gain.load(std::memory_order_relaxed));
        if (openSendLevelGestures.insert(id).second)
            param->beginGesture();
        param->setValueFromUser(juce::jlimit(0.0f, 1.0f, gain * 0.5f));
        if (endGestureNow)
        {
            param->endGesture();
            openSendLevelGestures.erase(id);
        }
    }

    void publishSendBypassAutomationGesture(const TrackID& source, const TrackID& targetId)
    {
        if (routingGraph == nullptr || source.isEmpty() || targetId.isEmpty())
            return;

        auto* srcNode = routingGraph->getNodeByTrackId(source);
        auto* dstNode = routingGraph->getNodeByTrackId(targetId);
        if (srcNode == nullptr || dstNode == nullptr)
            return;

        RoutingConnection* sendConn = nullptr;
        for (auto* conn : routingGraph->getOutputConnections(srcNode->id))
        {
            if (conn != nullptr && conn->destNodeId == dstNode->id && BubblegumSendStateCore::isSendConnection(conn->type))
            {
                sendConn = conn;
                break;
            }
        }
        if (sendConn == nullptr)
            return;

        const auto key = apex::automation::AutomationParameterKeyRegistry::trackSendBypassKey(source, sendConn->id);
        const auto id = apex::automation::AutomationParameterKeyRegistry::getInstance().getOrCreateID(key);
        auto& sys = apex::automation::AutomationSystem::getInstance();
        auto* param = sys.getRegistry().find(id);
        if (param == nullptr)
        {
            apex::automation::ParameterRange range;
            range.minValue = 0.0f;
            range.maxValue = 1.0f;
            range.defaultValue = 0.0f;
            range.skew = 1.0f;
            range.isStepped = true;
            range.numSteps = 2;
            param = sys.createNativeParameter(id, "Send " + sendConn->id + " Bypass", range);
            const auto volumeId = apex::automation::AutomationParameterKeyRegistry::getInstance()
                .findID(apex::automation::AutomationParameterKeyRegistry::trackVolumeKey(source));
            if (volumeId != apex::automation::kInvalidParameterID)
                sys.setMode(id, sys.getModeState().getMode(volumeId));
        }

        if (param == nullptr)
            return;

        sendAutomationBindings[id] = { source, targetId, sendConn->id, true };
        param->addListener(this);

        const bool active = sendConn->active.load(std::memory_order_relaxed);
        param->beginGesture();
        param->setValueFromUser(active ? 0.0f : 1.0f);
        param->endGesture();
    }

    // ── Lifecycle ────────────────────────────────────────────────────────

    void init(RoutingGraph& graph, TrackManager& tracks)
    {
        routingGraph   = &graph;
        trackManager   = &tracks;
        masterRoute_   = std::make_unique<MasterRouteStateCore>(graph);
    }

    void open(const TrackID& sourceTrackId = {})
    {
        DBG("[BubblegumV2::open] sourceTrackId=" + sourceTrackId
            + " routingGraph=" + juce::String(routingGraph != nullptr ? "OK" : "NULL")
            + " trackManager=" + juce::String(trackManager != nullptr ? "OK" : "NULL"));
        panel.open();
        if (sourceTrackId.isNotEmpty())
            sourceSync.sync(sourceTrackId);
        rebuildTargets();
        transition.beginFadeIn();
        trackPulse.setEnabled(true);
        DBG("[BubblegumV2::open] done: panel.isOpen=" + juce::String(panel.isOpen() ? 1 : 0)
            + " source=" + sourceSync.getSourceTrackId()
            + " targets=" + juce::String(targetList.getCount()));
    }

    void close()
    {
        DBG("[BubblegumV2::close] wasActive=" + juce::String(panel.isOpen() ? 1 : 0)
            + " source=" + sourceSync.getSourceTrackId());
        transition.beginFadeOut();
        trackPulse.setEnabled(false);
        panel.close();
        sourceSync.clear();
        targetList.clear();
        DBG("[BubblegumV2::close] done: panel.isOpen=" + juce::String(panel.isOpen() ? 1 : 0));
    }

    void toggle(const TrackID& sourceTrackId = {})
    {
        if (panel.isOpen()) close();
        else                open(sourceTrackId);
    }

    bool isActive() const noexcept { return panel.isOpen(); }

    // ── Auto source sync (Rule 1: Selected Track = Source) ──────────────

    void onTrackSelected(const TrackID& trackId)
    {
        DBG("[BubblegumV2::onTrackSelected] trackId=" + trackId
            + " panel.isOpen=" + juce::String(panel.isOpen() ? 1 : 0)
            + " oldSource=" + sourceSync.getSourceTrackId());

        // Rule 1 (Selected Track = Source) applies even when the panel is
        // closed: force-visible cables and the offscreen endpoint bubbles must
        // follow the current selection in realtime from the very first launch.
        // Only the panel-side target rebuild / UI refresh stays gated.
        const bool wasOpen = panel.isOpen();
        sourceSync.sync(trackId);
        const bool sourceChanged = sourceSync.consumeDirty();

        if (!wasOpen)
        {
            DBG("[BubblegumV2::onTrackSelected] panel closed — source synced="
                + sourceSync.getSourceTrackId());
            return;
        }

        if (sourceChanged)
        {
            rebuildTargets();
            DBG("[BubblegumV2::onTrackSelected] source changed -> rebuilt targets="
                + juce::String(targetList.getCount())
                + " newSource=" + sourceSync.getSourceTrackId());
        }
        else
        {
            DBG("[BubblegumV2::onTrackSelected] same source, no rebuild needed");
        }
    }

    // Rebuild target lists for the current source while Bubblegum is open.
    void refreshTargets()
    {
        if (!panel.isOpen())
            return;
        rebuildTargets();
    }

    // ── Send actions ─────────────────────────────────────────────────────

    void handleTargetTap(const TrackID& targetId)
    {
        DBG("[BubblegumV2::handleTargetTap] targetId=" + targetId
            + " routingGraph=" + juce::String(routingGraph != nullptr ? "OK" : "NULL")
            + " panel.isOpen=" + juce::String(panel.isOpen() ? 1 : 0)
            + " source=" + sourceSync.getSourceTrackId());
        if (!routingGraph || !panel.isOpen()) { DBG("[BubblegumV2::handleTargetTap] ABORT: no graph or panel closed"); return; }
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty() || source == targetId) { DBG("[BubblegumV2::handleTargetTap] ABORT: empty source or self-send"); return; }

        // Master is a Direct-edge target (see RoutingGraph::addNode auto-route).
        // Create / reactivate the Direct→master edge via MasterRouteStateCore,
        // NEVER a parallel Send edge — otherwise delete/toggle become asymmetric.
        if (isMasterTarget(targetId))
        {
            if (masterRoute_)
            {
                const auto stateBefore = masterRoute_->getMasterRouteState(source);
                FORENSIC_LOG("[BUBBLEGUM CLICK] create/toggle master route src=" << source
                    << " dst=" << targetId << " stateBefore=" << (int)stateBefore);
                performTopologyMutation([this, source]() { masterRoute_->registerTrack(source); }, "Enable Master Route");
                const auto stateAfter = masterRoute_->getMasterRouteState(source);
                FORENSIC_LOG("[BUBBLEGUM CLICK] master route stateAfter=" << (int)stateAfter);
                DBG("[BubblegumV2::handleTargetTap] master route (re)activated for " + source);
            }
            return;
        }

        bool exists = sendState.sendExists(*routingGraph, source, targetId);
        DBG("Send exists check: source=" << source << " target=" << targetId << " result=" << (int)exists);
        if (!exists)
            performTopologyMutation([this, source, targetId]() { sendState.createSend(*routingGraph, source, targetId); }, "Create Send");
        else if (!sendState.isSendActive(*routingGraph, source, targetId))
            performTopologyMutation([this, source, targetId]() { sendState.setSendActive(*routingGraph, source, targetId, true); }, "Enable Send");
        DBG("[BubblegumV2::handleTargetTap] " + source + " -> " + targetId
            + " hasSend=" + juce::String(sendState.hasSend(*routingGraph, source, targetId) ? 1 : 0));
    }

    void handleKnobDrag(const TrackID& targetId, float dragDeltaY)
    {
        if (!routingGraph || !panel.isOpen()) return;
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty()) return;

        auto result = input.handleKnobDrag(targetId, dragDeltaY);
        if (result.action == BubblegumInputCore::Action::AdjustLevel)
        {
            if (isMasterTarget(targetId))
            {
                if (masterRoute_) masterRoute_->adjustMasterRouteLevel(source, result.levelDelta);
            }
            else
            {
                sendLevel.adjustLevel(*routingGraph, source, targetId, result.levelDelta);
            publishSendLevelAutomationGesture(source, targetId);
            }
        }
    }

    void handlePrecisionDrag(const TrackID& targetId, float dragDeltaY)
    {
        if (!routingGraph || !panel.isOpen()) return;
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty()) return;

        auto result = input.handlePrecisionDrag(targetId, dragDeltaY);
        if (result.action == BubblegumInputCore::Action::PrecisionLevel)
        {
            if (isMasterTarget(targetId))
            {
                if (masterRoute_) masterRoute_->adjustMasterRouteLevel(source, result.levelDelta);
            }
            else
            {
                sendLevel.adjustLevel(*routingGraph, source, targetId, result.levelDelta);
            publishSendLevelAutomationGesture(source, targetId);
            }
        }
    }

    void setSendLevel(const TrackID& targetId, float level)
    {
        if (!routingGraph) return;
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty()) return;
        if (isMasterTarget(targetId))
        {
            if (masterRoute_) masterRoute_->setMasterRouteLevel(source, level);
        }
        else
        {
            sendLevel.setLevel(*routingGraph, source, targetId, level);
            publishSendLevelAutomationGesture(source, targetId);
        }
        // Real-time broadcast to the cable overlay anchor readout.
        routingGraph->notifyGraphChanged();
    }

    // ── Send mute/delete (for offscreen popup controls) ────────────────

    void toggleSendActive(const TrackID& targetId)
    {
        if (!routingGraph) return;
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty()) return;
        DBG("TOGGLE send: source=" << source << " target=" << targetId);
        if (isMasterTarget(targetId))
        {
            if (masterRoute_) performTopologyMutation([this, source]() { masterRoute_->toggleMasterRoute(source); }, "Toggle Master Route");
        }
        else
        {
            performTopologyMutation([this, source, targetId]() { sendState.toggleSendActive(*routingGraph, source, targetId); }, "Toggle Send");
            publishSendBypassAutomationGesture(source, targetId);
        }
    }

    bool isSendActive(const TrackID& targetId) const
    {
        if (!routingGraph) return false;
        if (isMasterTarget(targetId))
            return masterRoute_ && masterRoute_->isMasterRouteActive(sourceSync.getSourceTrackId());
        return sendState.isSendActive(*routingGraph,
                                      sourceSync.getSourceTrackId(), targetId);
    }

    void removeSend(const TrackID& targetId)
    {
        if (!routingGraph) return;
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty()) return;
        DBG("DELETE send: source=" << source << " target=" << targetId);
        if (isMasterTarget(targetId))
        {
            if (masterRoute_) performTopologyMutation([this, source]() { masterRoute_->deleteMasterRoute(source); }, "Delete Master Route");
        }
        else
        {
            performTopologyMutation([this, source, targetId]() { sendState.deleteSend(*routingGraph, source, targetId); }, "Delete Send");
        }
    }

    void handleTargetTapFrom(const TrackID& source, const TrackID& targetId)
    {
        if (!routingGraph || source.isEmpty() || source == targetId) return;
        if (isMasterTarget(targetId))
        {
            if (masterRoute_)
                performTopologyMutation([this, source]() { masterRoute_->registerTrack(source); }, "Enable Master Route");
            return;
        }

        const bool exists = sendState.sendExists(*routingGraph, source, targetId);
        if (!exists)
            performTopologyMutation([this, source, targetId]() { sendState.createSend(*routingGraph, source, targetId); }, "Create Send");
        else if (!sendState.isSendActive(*routingGraph, source, targetId))
        {
            performTopologyMutation([this, source, targetId]() { sendState.setSendActive(*routingGraph, source, targetId, true); }, "Enable Send");
            publishSendBypassAutomationGesture(source, targetId);
        }
    }

    void toggleSendActiveFrom(const TrackID& source, const TrackID& targetId)
    {
        if (!routingGraph || source.isEmpty()) return;
        if (isMasterTarget(targetId))
        {
            if (masterRoute_) performTopologyMutation([this, source]() { masterRoute_->toggleMasterRoute(source); }, "Toggle Master Route");
        }
        else
        {
            performTopologyMutation([this, source, targetId]() { sendState.toggleSendActive(*routingGraph, source, targetId); }, "Toggle Send");
            publishSendBypassAutomationGesture(source, targetId);
        }
    }

    void removeSendFrom(const TrackID& source, const TrackID& targetId)
    {
        if (!routingGraph || source.isEmpty()) return;
        if (isMasterTarget(targetId))
        {
            if (masterRoute_) performTopologyMutation([this, source]() { masterRoute_->deleteMasterRoute(source); }, "Delete Master Route");
        }
        else
        {
            performTopologyMutation([this, source, targetId]() { sendState.deleteSend(*routingGraph, source, targetId); }, "Delete Send");
        }
    }

    void setSendLevelFrom(const TrackID& source, const TrackID& targetId, float level)
    {
        if (!routingGraph || source.isEmpty()) return;
        if (isMasterTarget(targetId))
        {
            if (masterRoute_) masterRoute_->setMasterRouteLevel(source, level);
        }
        else
        {
            sendLevel.setLevel(*routingGraph, source, targetId, level);
            publishSendLevelAutomationGesture(source, targetId);
        }
        // Broadcast so the cable overlay anchor label refreshes in real time —
        // send-level edits from the Bubblegum panel and from anchor drags both
        // land here and both must update the on-cable readout immediately.
        routingGraph->notifyGraphChanged();
    }

    /** True when the single send source→target is pre-fader (see BubblegumSendStateCore). */
    bool isPreFaderFromTo(const TrackID& source, const TrackID& targetId) const
    {
        if (!routingGraph || source.isEmpty()) return false;
        return sendState.isSendPreFader(*routingGraph, source, targetId);
    }

    /** Toggle ONE send (source→target) between post-fader and pre-fader.
     *  Other sends leaving `source` are untouched. */
    void togglePreFaderFromTo(const TrackID& source, const TrackID& targetId)
    {
        if (!routingGraph || source.isEmpty()) return;
        const bool pre = !sendState.isSendPreFader(*routingGraph, source, targetId);
        performTopologyMutation([this, source, targetId, pre]()
        {
            sendState.setSendPreFader(*routingGraph, source, targetId, pre);
        }, pre ? "Set Send Pre-Fader" : "Set Send Post-Fader");
    }

    /** Summary of the pre/post-fader state of every send leaving `source`
     *  (0 = no sends, 1 = all post, 2 = all pre, 3 = mixed). */
    int getSendPreFaderSummaryFrom(const TrackID& source) const
    {
        if (!routingGraph || source.isEmpty()) return 0;
        return sendState.getSendPreFaderSummary(*routingGraph, source);
    }

    /** (targetId, isPreFader) for every send leaving `source`.
     *  Used by the per-target toggle menu on the strip/row button. */
    std::vector<std::pair<TrackID, bool>> getSendPreFaderTargetsFrom(const TrackID& source) const
    {
        if (!routingGraph || source.isEmpty()) return {};
        return sendState.getSendPreFaderTargets(*routingGraph, source);
    }

    // ── Queries ──────────────────────────────────────────────────────────

    bool hasSendTo(const TrackID& targetId) const
    {
        if (!routingGraph) return false;
        if (isMasterTarget(targetId))
            return masterRoute_
                && masterRoute_->getMasterRouteState(sourceSync.getSourceTrackId())
                       != RouteState::DoesNotExist;
        return sendState.sendExists(*routingGraph,
                                    sourceSync.getSourceTrackId(), targetId);
    }

    float getSendLevelTo(const TrackID& targetId) const
    {
        if (!routingGraph) return 0.0f;
        if (isMasterTarget(targetId))
            return masterRoute_ ? masterRoute_->getMasterRouteLevel(sourceSync.getSourceTrackId()) : 0.0f;
        return sendLevel.getLevel(*routingGraph,
                                  sourceSync.getSourceTrackId(), targetId);
    }

    bool hasSendFromTo(const TrackID& source, const TrackID& targetId) const
    {
        if (!routingGraph || source.isEmpty()) return false;
        if (isMasterTarget(targetId))
            return masterRoute_ && masterRoute_->getMasterRouteState(source) != RouteState::DoesNotExist;
        return sendState.sendExists(*routingGraph, source, targetId);
    }

    bool isSendActiveFromTo(const TrackID& source, const TrackID& targetId) const
    {
        if (!routingGraph || source.isEmpty()) return false;
        if (isMasterTarget(targetId))
            return masterRoute_ && masterRoute_->isMasterRouteActive(source);
        return sendState.isSendActive(*routingGraph, source, targetId);
    }

    float getSendLevelFromTo(const TrackID& source, const TrackID& targetId) const
    {
        if (!routingGraph || source.isEmpty()) return 0.0f;
        if (isMasterTarget(targetId))
            return masterRoute_ ? masterRoute_->getMasterRouteLevel(source) : 0.0f;
        return sendLevel.getLevel(*routingGraph, source, targetId);
    }

    BubblegumVisualCore::TrackVisualState getVisualState(const TrackID& trackId) const
    {
        auto source = sourceSync.getSourceTrackId();
        bool has    = hasSendTo(trackId);
        float level = has ? getSendLevelTo(trackId) : 0.0f;
        return visual.computeState(trackId, source, has, level);
    }

    // ── Animation tick (call from timer at ~60 Hz) ───────────────────────

    /** Returns true if any sidechain cable is visible and animating.
     *  Used by the overlay repaint gate to avoid repainting static frames. */
    bool hasSidechainCablesActive() const noexcept
    {
        return sidechainCablesVisible_ && sidechainCable.hasActiveEdges();
    }

    // Set once per frame from the cable overlay (mirrors cablesVisible || offscreenButtonsVisible).
    // When false, sidechainCable.tick() is skipped so offscreen cables burn no CPU.
    void setSidechainCablesVisible(bool visible) noexcept
    {
        sidechainCablesVisible_ = visible;
        sidechainCable.reducedMotion = reducedMotion_;
    }

    void setReducedMotion(bool reduced) noexcept
    {
        reducedMotion_ = reduced;
        sidechainCable.reducedMotion = reduced;
    }

    void tick(float deltaMs)
    {
        transition.tick(deltaMs);
        trackPulse.tick(deltaMs);
        liquidCable.tick(deltaMs, trackPulse.getPulseAlpha());
        if (sidechainCablesVisible_)
            sidechainCable.tick(deltaMs);
    }

    // ── Sidechain routing API ─────────────────────────────────────────────

    /** Create or toggle a sidechain connection source → dest. */
    void handleSidechainTap(const TrackID& destTrackId,
                             const juce::String& destPluginId = {},
                             int destBusIndex = 1,
                             TapPoint tap = TapPoint::PreFX)
    {
        if (!routingGraph || !panel.isOpen()) return;
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty() || source == destTrackId) return;
        DBG("[SC] handleSidechainTap src=" << source << " dest=" << destTrackId);
        performTopologyMutation([this, source, destTrackId, destPluginId, destBusIndex, tap]()
        {
            sidechainState.toggleSidechain(*routingGraph, source, destTrackId,
                                           destPluginId, destBusIndex, tap);
        }, "Toggle Sidechain");
    }

    void removeSidechain(const TrackID& destTrackId)
    {
        if (!routingGraph) return;
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty()) return;
        // Capture the edge ID before deletion so we can purge its animator state.
        auto* conn = sidechainState.getConnection(*routingGraph, source, destTrackId);
        if (conn)
            sidechainCable.removeEdge(conn->id);
        performTopologyMutation([this, source, destTrackId]() { sidechainState.deleteSidechain(*routingGraph, source, destTrackId); }, "Delete Sidechain");
    }

    void handleSidechainTapFrom(const TrackID& source,
                                const TrackID& destTrackId,
                                const juce::String& destPluginId = {},
                                int destBusIndex = 1,
                                TapPoint tap = TapPoint::PreFX)
    {
        if (!routingGraph || source.isEmpty() || source == destTrackId) return;
        performTopologyMutation([this, source, destTrackId, destPluginId, destBusIndex, tap]()
        {
            sidechainState.toggleSidechain(*routingGraph, source, destTrackId,
                                           destPluginId, destBusIndex, tap);
        }, "Toggle Sidechain");
    }

    void removeSidechainFrom(const TrackID& source, const TrackID& destTrackId)
    {
        if (!routingGraph || source.isEmpty()) return;
        auto* conn = sidechainState.getConnection(*routingGraph, source, destTrackId);
        if (conn)
            sidechainCable.removeEdge(conn->id);
        performTopologyMutation([this, source, destTrackId]() { sidechainState.deleteSidechain(*routingGraph, source, destTrackId); }, "Delete Sidechain");
    }

    /** Toggle a sidechain connection's active flag without removing it
     *  (cable-anchor click language). Undo-wrapped like every topology write. */
    void toggleSidechainActiveFrom(const TrackID& source, const TrackID& destTrackId)
    {
        if (!routingGraph || source.isEmpty()) return;
        performTopologyMutation([this, source, destTrackId]() { sidechainState.toggleActive(*routingGraph, source, destTrackId); }, "Toggle Sidechain");
    }

    bool hasSidechainTo(const TrackID& destTrackId) const
    {
        if (!routingGraph) return false;
        return sidechainState.sidechainExists(*routingGraph,
                                              sourceSync.getSourceTrackId(), destTrackId);
    }

    bool isSidechainActive(const TrackID& destTrackId) const
    {
        if (!routingGraph) return false;
        return sidechainState.isSidechainActive(*routingGraph,
                                                sourceSync.getSourceTrackId(), destTrackId);
    }

    bool hasSidechainFromTo(const TrackID& source, const TrackID& destTrackId) const
    {
        if (!routingGraph || source.isEmpty()) return false;
        return sidechainState.sidechainExists(*routingGraph, source, destTrackId);
    }

    bool isSidechainActiveFromTo(const TrackID& source, const TrackID& destTrackId) const
    {
        if (!routingGraph || source.isEmpty()) return false;
        return sidechainState.isSidechainActive(*routingGraph, source, destTrackId);
    }

    void setSidechainTapPoint(const TrackID& destTrackId, TapPoint tap)
    {
        if (!routingGraph) return;
        auto source = sourceSync.getSourceTrackId();
        if (source.isEmpty()) return;
        performTopologyMutation([this, source, destTrackId, tap]()
        {
            sidechainState.setTapPoint(*routingGraph, source, destTrackId, tap);
        }, "Change Sidechain Tap");
    }

    /**
     * Feed a trigger level for a sidechain cable so the chain pulse fires.
     * Call this from the same level-meter poll that feeds the detector.
     * linearLevel: 0..1 (0 = silence, 1 = full scale).
     */
    void feedSidechainTriggerLevel(const juce::String& edgeId, float linearLevel)
    {
        sidechainCable.feedTriggerLevel(edgeId, linearLevel);
    }

    // Remove animator states for edges that no longer exist in the routing graph.
    // Called from the overlay whenever topologyHash changes.
    void pruneSidechainCableStates(const juce::StringArray& liveIds)
    {
        sidechainCable.pruneStates(liveIds);
    }

    /** Set the user-visible chain cable colour (base + bright highlight). */
    void setSidechainCableColour(juce::Colour base, juce::Colour bright)
    {
        sidechainCable.colourBase   = base;
        sidechainCable.colourBright = bright;
    }
    juce::Colour getSidechainCableColour() const noexcept { return sidechainCable.colourBase; }
    void setSidechainCableQuality(bubblegum::BubblegumCableStyleSettingsCore::Style::QualityMode q) noexcept
    {
        sidechainCable.qualityMode = q;
    }

    /**
     * Paint all active sidechain cables.
     * Call from the Bubblegum cable overlay paint path, after normal cables.
     * sidechainCables: list of BubblegumCableInput built from Sidechain connections
     *                  (edgeId set to the RoutingConnection::id).
     */
    void paintSidechainCables(juce::Graphics& g,
                               const std::vector<BubblegumCableInput>& sidechainCables,
                               float laneBottomY) const
    {
        for (const auto& cable : sidechainCables)
        {
            if (!cable.shouldDraw()) continue;
            const bool active = cable.alphaMult > 0.5f; // active connections get full alphaMult
            sidechainCable.paint(g, cable, active, laneBottomY);
        }
    }

    /** Shared MasterRouteStateCore — owned by this system, reused by UI layers
     *  (e.g. MixerPanel folder-drop + timeline folder-drop wiring). */
    MasterRouteStateCore* getMasterRoute() const noexcept { return masterRoute_.get(); }

private:
    std::unique_ptr<MasterRouteStateCore> masterRoute_;
    bool sidechainCablesVisible_ = false;
    bool reducedMotion_           = false;

    bool isMasterTarget(const TrackID& targetId) const noexcept
    {
        if (!routingGraph) return false;
        auto* node = routingGraph->getNodeByTrackId(targetId);
        return node != nullptr && node->type == RoutingNodeType::Master;
    }

    void rebuildTargets()
    {
        if (!trackManager) return;
        targetList.rebuild(sourceSync.getSourceTrackId(), *trackManager);
        sidechainTargetList.rebuild(sourceSync.getSourceTrackId(), *trackManager);
        // Master send is established once at track-creation time (FL Studio behaviour).
        // Do not re-create it here — user may have intentionally removed it.
    }
};

} // namespace DAW
