// ===========================================================================
// QuickTrackBubblegumTests.cpp
// Auto-bus routing MUST be REAL Bubblegum send routing — not low-level
// RoutingGraph edges hidden from the cable UI.
//
// Regressions prove BOTH layers for every auto route:
//   ENGINE:    RoutingGraph has the canonical ConnectionType::Send edge
//   BUBBLEGUM: BubblegumSendStateCore (the cable model) contains the route
// and the subgroup semantics:
//   source Direct→master edge deactivated (ExistsInactive) — no double path.
// Undo/Redo use the real QuickTrackBatchCommand lifecycle.
// ===========================================================================
#include <JuceHeader.h>
#include "../../../Source/QuickTrackCore/QuickTrackBuilderCore.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/RoutingCore/MasterRouteStateCore.h"
#include "../../../Source/Bubblegum/BubblegumSendStateCore.h"
#include "../../../Source/Bubblegum/BubblegumSendLevelCore.h"
#include "../../../Source/CommandCore/GeneralCommands.h"

namespace
{

using namespace DAW;

const RoutingConnection* findGraphEdge(const RoutingGraph& graph,
                                       const juce::String& srcTrackId,
                                       const juce::String& destNodeId,
                                       ConnectionType type)
{
    auto* srcNode = graph.getNodeByTrackId(srcTrackId);
    if (srcNode == nullptr)
        return nullptr;
    for (auto* conn : graph.getAllConnections())
        if (conn != nullptr
            && conn->sourceNodeId == srcNode->id
            && conn->destNodeId == destNodeId
            && conn->type == type)
            return conn;
    return nullptr;
}

bool hasActiveDirectToMaster(const RoutingGraph& graph, const juce::String& trackId)
{
    if (auto* edge = findGraphEdge(graph, trackId, "master", ConnectionType::Direct))
        return edge->active.load(std::memory_order_relaxed);
    return false;
}

struct Harness
{
    TrackManager tracks;
    RoutingGraph graph;
    MasterRouteStateCore masterRoute { graph };
    BubblegumSendStateCore sends;
    QuickTrackColorSystem colors { 70707 };
    QuickTrackBuilderCore builder { tracks, graph, masterRoute, sends, colors };

    Harness()
    {
        tracks.createMasterTrack();
    }
};

class QuickTrackBubblegumTests final : public juce::UnitTest
{
public:
    QuickTrackBubblegumTests() : juce::UnitTest("QuickTrackBubblegum.Routing", "APEX.QuickTrackBubblegum") {}

    void runTest() override
    {
        beginTest("AutoBusUsesCanonicalSendPath");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 4 }, { busRole, 1 } });
            expect(result.ok());
            const TrackID busId = result.created[4].trackId;
            for (int i = 0; i < 4; ++i)
            {
                const auto& src = result.created[i].trackId;
                // The ENGINE edge must be the canonical Bubblegum Send type
                // (the cable type) — never a hidden Direct edge.
                auto* busNode = h.graph.getNodeByTrackId(busId);
                expect(busNode != nullptr);
                expect(findGraphEdge(h.graph, src, busNode->id, ConnectionType::Send) != nullptr,
                       "engine edge is ConnectionType::Send for Doubles " + juce::String(i + 1));
                expect(findGraphEdge(h.graph, src, busNode->id, ConnectionType::Direct) == nullptr,
                       "no hidden Direct edge to the bus");
            }
        }

        beginTest("AutoBusCreatesVisibleCableModel");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 2 }, { busRole, 1 } });
            expect(result.ok());
            const TrackID busId = result.created[2].trackId;
            for (int i = 0; i < 2; ++i)
            {
                // The Bubblegum cable model (what the UI renders) contains
                // the route — the OLD QuickTrack implementation (Direct-only
                // edges) FAILS this regression.
                expect(h.sends.hasSend(h.graph, result.created[i].trackId, busId),
                       "Bubblegum send model contains Doubles " + juce::String(i + 1) + " cable");
            }
        }

        beginTest("DoublesRouteVisible");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 4 }, { busRole, 1 } });
            const TrackID busId = result.created[4].trackId;
            for (int i = 0; i < 4; ++i)
                expect(h.sends.sendExists(h.graph, result.created[i].trackId, busId),
                       "cable exists in the send model for each Doubles");
        }

        beginTest("FamilyBusToVocalBusVisible");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            const auto* vocalBus = QuickTrackRoleCatalog::findById("vocal_bus");
            auto result = h.builder.createBatch({ { doubles, 1 }, { busRole, 1 }, { vocalBus, 1 } });
            const TrackID busId = result.created[1].trackId;
            const TrackID vocalBusId = result.created[2].trackId;
            expect(h.sends.hasSend(h.graph, busId, vocalBusId),
                   "Doubles Bus → Vocal Bus is a real Bubblegum cable");
            auto* vocalNode = h.graph.getNodeByTrackId(vocalBusId);
            expect(findGraphEdge(h.graph, busId, vocalNode->id, ConnectionType::Send) != nullptr,
                   "engine edge Doubles Bus → Vocal Bus is a Send");
        }

        beginTest("AutoBusRemovesDirectMaster");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 2 }, { busRole, 1 } });
            for (int i = 0; i < 2; ++i)
            {
                const auto state = h.masterRoute.getMasterRouteState(result.created[i].trackId);
                expectEquals((int) state, (int) RouteState::ExistsInactive,
                             "source Direct→master deactivated (subgroup)");
            }
        }

        beginTest("NoDoubleAudiblePath");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 3 }, { busRole, 1 } });
            h.graph.notifyGraphChanged();
            auto snap = h.graph.getSnapshotPublisher().get();
            expect(snap != nullptr);
            for (int i = 0; i < 3; ++i)
            {
                expect(!hasActiveDirectToMaster(h.graph, result.created[i].trackId),
                       "no ACTIVE direct Master path for Doubles " + juce::String(i + 1));
                auto* srcNode = h.graph.getNodeByTrackId(result.created[i].trackId);
                for (const auto& edge : snap->edges)
                    if (edge.sourceNodeId == srcNode->id && edge.destNodeId == "master"
                        && edge.type == ConnectionType::Direct)
                        expect(!edge.active, "published snapshot: master edge inactive");
            }
        }

        beginTest("UndoRemovesAutoBusRoute");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            std::vector<QuickTrackRequest> requests { { doubles, 2 }, { busRole, 1 } };
            QuickTrackBatchCommand cmd(h.builder, h.sends, h.masterRoute, h.graph, requests);
            cmd.execute();
            const TrackID busId = cmd.getResult().created[2].trackId;
            const TrackID srcId = cmd.getResult().created[0].trackId;
            expect(h.sends.hasSend(h.graph, srcId, busId), "cable present after create");

            cmd.undo();
            expect(!h.sends.hasSend(h.graph, srcId, busId), "cable removed by Undo");
        }

        beginTest("UndoRestoresPreviousMasterRoute");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            std::vector<QuickTrackRequest> requests { { doubles, 2 }, { busRole, 1 } };
            QuickTrackBatchCommand cmd(h.builder, h.sends, h.masterRoute, h.graph, requests);
            cmd.execute();
            const TrackID srcId = cmd.getResult().created[0].trackId;
            expectEquals((int) h.masterRoute.getMasterRouteState(srcId),
                         (int) RouteState::ExistsInactive, "before undo: master inactive");

            cmd.undo();
            expectEquals((int) h.masterRoute.getMasterRouteState(srcId),
                         (int) RouteState::ExistsActive, "Undo restores the previous direct Master");
            expect(hasActiveDirectToMaster(h.graph, srcId), "direct Master audible again after Undo");
        }

        beginTest("RedoRestoresAutoBusRoute");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            std::vector<QuickTrackRequest> requests { { doubles, 2 }, { busRole, 1 } };
            QuickTrackBatchCommand cmd(h.builder, h.sends, h.masterRoute, h.graph, requests);
            cmd.execute();
            const TrackID busId = cmd.getResult().created[2].trackId;
            const TrackID srcId = cmd.getResult().created[0].trackId;
            cmd.undo();
            expect(!h.sends.hasSend(h.graph, srcId, busId), "cable gone after Undo");
            expectEquals((int) h.masterRoute.getMasterRouteState(srcId),
                         (int) RouteState::ExistsActive, "master restored after Undo");

            // Redo: CommandManager re-invokes execute() — the redo-aware
            // command re-applies the recorded cables to the SAME tracks.
            cmd.execute();
            expect(h.sends.hasSend(h.graph, srcId, busId), "cable recreated by Redo");
            expectEquals((int) h.masterRoute.getMasterRouteState(srcId),
                         (int) RouteState::ExistsInactive, "master inactive again after Redo");
            expect(!hasActiveDirectToMaster(h.graph, srcId), "no double Master after Redo");
            expectEquals(h.tracks.getNumTracks(), 3, "Redo does not duplicate the batch");
        }

        beginTest("ManualDisconnectUsesCanonicalLifecycle");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 1 }, { busRole, 1 } });
            const TrackID srcId = result.created[0].trackId;
            const TrackID busId = result.created[1].trackId;
            expect(h.sends.hasSend(h.graph, srcId, busId));

            // Manual cable removal via the canonical Bubblegum delete path.
            h.sends.deleteSend(h.graph, srcId, busId);
            expect(!h.sends.hasSend(h.graph, srcId, busId), "cable removed canonically");
            // The master route remains inactive (ExistsInactive) — the
            // source is not silently re-routed; the user restores it through
            // the normal Bubblegum master workflow.
            expectEquals((int) h.masterRoute.getMasterRouteState(srcId),
                         (int) RouteState::ExistsInactive, "master state untouched by cable delete");
        }

        beginTest("ProjectReloadPreservesCable");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 2 }, { busRole, 1 } });
            const TrackID busId = result.created[2].trackId;

            const auto graphState = h.graph.getState();
            RoutingGraph reloaded;
            reloaded.restoreState(graphState);
            BubblegumSendStateCore reloadedSends;
            for (int i = 0; i < 2; ++i)
                expect(reloadedSends.hasSend(reloaded, result.created[i].trackId, busId),
                       "Bubblegum cable survives project reload");
        }

        beginTest("TemplateInstantiationUsesFreshConnections");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            std::vector<QuickTrackRequest> requests { { doubles, 1 }, { busRole, 1 } };

            auto first = h.builder.createBatch(requests);
            auto second = h.builder.createBatch(requests);
            const TrackID bus1 = first.created[1].trackId;
            const TrackID bus2 = second.created[1].trackId;
            expect(bus1 != bus2, "fresh bus identity per instantiation");
            expect(h.sends.hasSend(h.graph, first.created[0].trackId, bus1), "first batch cable");
            expect(h.sends.hasSend(h.graph, second.created[0].trackId, bus2), "second batch cable");
            // No shared connection identity: each batch owns its own sends.
            expect(findGraphEdge(h.graph, second.created[0].trackId,
                                 h.graph.getNodeByTrackId(bus1)->id, ConnectionType::Send) == nullptr,
                   "connections are not shared between instantiations");
        }

        beginTest("EffectSendRemainsParallel");
        {
            Harness h;
            const auto* lead = QuickTrackRoleCatalog::findById("lead_vocal");
            h.builder.createBatch({ { lead, 1 } });
            const TrackID srcId = h.tracks.getTrack(0)->getID();

            // A normal parallel effect send (e.g. to a Reverb return) must
            // NOT deactivate the source's direct Master route.
            auto* returnNode = h.graph.addNode("Reverb", RoutingNodeType::Bus, "Reverb");
            juce::ignoreUnused(returnNode);
            h.sends.createSend(h.graph, srcId, "Reverb", 0.5f);
            expect(h.sends.hasSend(h.graph, srcId, "Reverb"), "parallel effect send exists");
            expectEquals((int) h.masterRoute.getMasterRouteState(srcId),
                         (int) RouteState::ExistsActive,
                         "parallel effect send keeps the direct Master route");
            expect(hasActiveDirectToMaster(h.graph, srcId), "Master stays audible");
        }

        beginTest("BusRouteIsNotParallel");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 1 }, { busRole, 1 } });
            const TrackID srcId = result.created[0].trackId;
            // Subgroup routing deactivates the direct Master (NOT parallel).
            expectEquals((int) h.masterRoute.getMasterRouteState(srcId),
                         (int) RouteState::ExistsInactive,
                         "QuickTrack bus route is subgroup, not a parallel send");
        }

        beginTest("AutoBusDefaultsToUnity");
        {
            Harness h;
            BubblegumSendLevelCore levels;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 2 }, { busRole, 1 } });
            const TrackID busId = result.created[2].trackId;
            for (int i = 0; i < 2; ++i)
            {
                const float level = levels.getLevel(h.graph, result.created[i].trackId, busId);
                expect(std::fabs(level - 1.0f) < 0.001f,
                       "auto bus route defaults to canonical unity amount: " + juce::String(level));
            }
        }

        beginTest("AutoBusHasEditableAmount");
        {
            Harness h;
            BubblegumSendLevelCore levels;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 1 }, { busRole, 1 } });
            const TrackID srcId = result.created[0].trackId;
            const TrackID busId = result.created[1].trackId;

            // The canonical Bubblegum send-level core (the SAME object the
            // send knob drives) can change the amount after creation.
            levels.setLevel(h.graph, srcId, busId, 0.7f);
            expect(std::fabs(levels.getLevel(h.graph, srcId, busId) - 0.7f) < 0.001f,
                   "send amount is editable via the canonical Bubblegum level core");
        }

        beginTest("SendAmountCanChange");
        {
            Harness h;
            BubblegumSendLevelCore levels;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 1 }, { busRole, 1 } });
            const TrackID srcId = result.created[0].trackId;
            const TrackID busId = result.created[1].trackId;

            levels.setLevel(h.graph, srcId, busId, 0.65f);
            expect(std::fabs(levels.getLevel(h.graph, srcId, busId) - 0.65f) < 0.001f,
                   "knob-style setLevel changes the amount");
            levels.adjustLevel(h.graph, srcId, busId, -0.1f);
            expect(std::fabs(levels.getLevel(h.graph, srcId, busId) - 0.55f) < 0.001f,
                   "knob-style drag adjusts the amount");
        }

        beginTest("SendAmountAffectsCanonicalModel");
        {
            Harness h;
            BubblegumSendLevelCore levels;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 1 }, { busRole, 1 } });
            const TrackID srcId = result.created[0].trackId;
            const TrackID busId = result.created[1].trackId;

            levels.setLevel(h.graph, srcId, busId, 0.7f);
            // The published engine snapshot carries edge.gain = the amount —
            // this is the exact value the audio renderer applies
            // (getConnectionGainRamp consumes edge.gain), so the knob change
            // affects the real routed audio gain — no QuickTrack gain stage.
            h.graph.notifyGraphChanged();
            auto snap = h.graph.getSnapshotPublisher().get();
            expect(snap != nullptr);
            auto* busNode = h.graph.getNodeByTrackId(busId);
            bool found = false;
            for (const auto& edge : snap->edges)
                if (edge.sourceNodeId == h.graph.getNodeByTrackId(srcId)->id
                    && edge.destNodeId == busNode->id
                    && edge.type == ConnectionType::Send)
                {
                    found = true;
                    expect(std::fabs(edge.gain - 0.7f) < 0.001f,
                           "snapshot edge gain reflects the canonical send amount");
                }
            expect(found, "send edge present in the published snapshot");
        }

        beginTest("SendAmountPersistsReload");
        {
            Harness h;
            BubblegumSendLevelCore levels;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 1 }, { busRole, 1 } });
            const TrackID srcId = result.created[0].trackId;
            const TrackID busId = result.created[1].trackId;

            levels.setLevel(h.graph, srcId, busId, 0.7f);

            const auto graphState = h.graph.getState();
            RoutingGraph reloaded;
            reloaded.restoreState(graphState);
            BubblegumSendLevelCore reloadedLevels;
            const float level = reloadedLevels.getLevel(reloaded, srcId, busId);
            expect(std::fabs(level - 0.7f) < 0.001f,
                   "send amount persists project reload (gain serialized): " + juce::String(level));
        }

        beginTest("SendAmountUndoRedo");
        {
            Harness h;
            BubblegumSendLevelCore levels;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            std::vector<QuickTrackRequest> requests { { doubles, 1 }, { busRole, 1 } };
            QuickTrackBatchCommand cmd(h.builder, h.sends, h.masterRoute, h.graph, requests);
            cmd.execute();
            const TrackID srcId = cmd.getResult().created[0].trackId;
            const TrackID busId = cmd.getResult().created[1].trackId;

            // The amount lives on the SAME canonical connection object the
            // cable renders (BubblegumSendLevelCore::getLevel reads the
            // RoutingConnection::gain). Amount edits use the canonical
            // level-core lifecycle — identical to a manual send knob.
            levels.setLevel(h.graph, srcId, busId, 0.4f);
            expect(std::fabs(levels.getLevel(h.graph, srcId, busId) - 0.4f) < 0.001f);

            // The batch command's Undo removes the cable + restores Master.
            cmd.undo();
            expect(!h.sends.hasSend(h.graph, srcId, busId), "Undo removes the auto cable");
            expectEquals((int) h.masterRoute.getMasterRouteState(srcId),
                         (int) RouteState::ExistsActive, "Undo restores the direct Master");

            // Redo re-creates the canonical connection; the new connection
            // starts at the canonical default unity (fresh connection).
            cmd.execute();
            expect(h.sends.hasSend(h.graph, srcId, busId), "Redo re-creates the cable");
            const float level = levels.getLevel(h.graph, srcId, busId);
            expect(std::fabs(level - 1.0f) < 0.001f,
                   "Redo connection defaults to canonical unity: " + juce::String(level));
        }
    }
};

static QuickTrackBubblegumTests quickTrackBubblegumTests;

} // namespace
