#include <JuceHeader.h>

#include "../../../Source/AudioEngineCore/ConnectionGainRampCore.h"
#include "../../../Source/Bubblegum/BubblegumFolderProjectionCore.h"
#include "../../../Source/FolderBusCore/FolderBusCore.h"
#include "../../../Source/RoutingCore/MasterRouteStateCore.h"
#include "../../../Source/RenderCore/StemRouteMaskCore.h"
#include "../../../Source/CommandCore/GeneralCommands.h"

namespace
{
const DAW::RoutingSnapshot::EdgeSnapshot* findSnapshotEdge(
    const DAW::RoutingSnapshot& snapshot,
    const juce::String& sourceNodeId,
    const juce::String& destinationNodeId,
    DAW::ConnectionType type)
{
    for (const auto& edge : snapshot.edges)
        if (edge.sourceNodeId == sourceNodeId
            && edge.destNodeId == destinationNodeId
            && edge.type == type)
            return &edge;
    return nullptr;
}

DAW::RoutingConnection* findGraphEdge(
    DAW::RoutingGraph& graph,
    const juce::String& sourceNodeId,
    const juce::String& destinationNodeId,
    DAW::ConnectionType type)
{
    for (auto* edge : graph.getAllConnections())
        if (edge != nullptr
            && edge->sourceNodeId == sourceNodeId
            && edge->destNodeId == destinationNodeId
            && edge->type == type)
            return edge;
    return nullptr;
}

class RoutingFolderRegressionTests final : public juce::UnitTest
{
public:
    RoutingFolderRegressionTests()
        : juce::UnitTest("routing.folder-master-disconnect.v1", "APEX.Routing")
    {
    }

    void runTest() override
    {
        beginTest("regular Master deletion is canonical absence plus a bounded render retirement");
        {
            DAW::RoutingGraph graph;
            DAW::MasterRouteStateCore masterRoutes(graph);
            auto* source = graph.addNode("Source", DAW::RoutingNodeType::Track, "track-source");
            expect(source != nullptr);
            expect(findGraphEdge(graph, source->id, "master", DAW::ConnectionType::Direct) != nullptr);

            masterRoutes.deleteMasterRoute("track-source");
            expectEquals((int)masterRoutes.getMasterRouteState("track-source"),
                         (int)DAW::RouteState::DoesNotExist,
                         "the editable graph must not retain an implicit Master route");
            expect(findGraphEdge(graph, source->id, "master", DAW::ConnectionType::Direct) == nullptr);

            auto retirement = graph.getSnapshotPublisher().get();
            expect(retirement != nullptr);
            if (retirement != nullptr)
            {
                auto* edge = findSnapshotEdge(*retirement, source->id, "master", DAW::ConnectionType::Direct);
                expect(edge != nullptr, "the first render generation should carry a fade-to-silence tombstone");
                if (edge != nullptr)
                {
                    expect(edge->retiring);
                    expect(edge->active);
                    expectWithinAbsoluteError(edge->gain, 0.0f, 1.0e-7f);
                }
                expect(!retirement->processingOrder.empty()
                           && retirement->processingOrder.back() == "master",
                       "Master must execute after a retiring source");
            }

            graph.publishSnapshotOnly();
            auto clean = graph.getSnapshotPublisher().get();
            expect(clean != nullptr);
            if (clean != nullptr)
                expect(findSnapshotEdge(*clean, source->id, "master", DAW::ConnectionType::Direct) == nullptr,
                       "runtime tombstones must not become persistent routes");
        }

        beginTest("a disconnected track is audible only through an explicit downstream path");
        {
            DAW::RoutingGraph graph;
            DAW::MasterRouteStateCore masterRoutes(graph);
            auto* source = graph.addNode("Source", DAW::RoutingNodeType::Track, "track-source");
            auto* bus = graph.addNode("Bus", DAW::RoutingNodeType::Bus, "track-bus");
            expect(source != nullptr && bus != nullptr);

            masterRoutes.deleteMasterRoute("track-source");
            graph.publishSnapshotOnly(); // retire the render-only direct edge
            auto* send = graph.connect(source->id, bus->id, DAW::ConnectionType::Send);
            expect(send != nullptr);
            expect(findGraphEdge(graph, source->id, "master", DAW::ConnectionType::Direct) == nullptr);
            expect(findGraphEdge(graph, source->id, bus->id, DAW::ConnectionType::Send) != nullptr);
            expect(findGraphEdge(graph, bus->id, "master", DAW::ConnectionType::Direct) != nullptr);
        }

        beginTest("route retirement gain is de-zippered and reaches exact silence");
        {
            DAW::ConnectionGainRampCore ramp;
            ramp.prepare(48000.0, 128);
            ramp.reset(1.0f);

            const float* first = ramp.generate(0.0f, 128);
            expect(first[0] < 1.0f && first[0] > 0.0f);
            for (int i = 1; i < 128; ++i)
                expect(first[i] <= first[i - 1]);

            for (int block = 0; block < 40; ++block)
                ramp.generate(0.0f, 128);
            expectWithinAbsoluteError(ramp.getCurrentGain(), 0.0f, 1.0e-7f);
        }

        beginTest("folder creation preserves visual sibling order for either drag direction");
        {
            DAW::TrackManager tracks;
            DAW::RoutingGraph graph;
            DAW::MasterRouteStateCore masterRoutes(graph);
            DAW::FolderBusCore folders;

            auto* top = tracks.createTrack("Top");
            auto* bottom = tracks.createTrack("Bottom");
            expect(top != nullptr && bottom != nullptr);
            graph.addNode(top->getName(), DAW::RoutingNodeType::Track, top->getID());
            graph.addNode(bottom->getName(), DAW::RoutingNodeType::Track, bottom->getID());

            const auto topId = top->getID();
            const auto bottomId = bottom->getID();
            const auto folderId = folders.createFolderBus(
                "Folder", { bottomId, topId }, {}, graph, masterRoutes, tracks);

            expect(folderId.isNotEmpty());
            expectEquals(tracks.getTrack(0)->getID(), folderId);
            expectEquals(tracks.getTrack(1)->getID(), topId);
            expectEquals(tracks.getTrack(2)->getID(), bottomId);

            const auto& children = folders.getDirectChildren(folderId);
            expectEquals((int)children.size(), 2);
            if (children.size() == 2)
            {
                expectEquals(children[0], topId);
                expectEquals(children[1], bottomId);
            }

            auto* folderNode = graph.getNodeByTrackId(folderId);
            auto* topNode = graph.getNodeByTrackId(topId);
            auto* bottomNode = graph.getNodeByTrackId(bottomId);
            expect(folderNode != nullptr && topNode != nullptr && bottomNode != nullptr);
            if (folderNode != nullptr && topNode != nullptr && bottomNode != nullptr)
            {
                expect(findGraphEdge(graph, topNode->id, "master", DAW::ConnectionType::Direct) == nullptr);
                expect(findGraphEdge(graph, bottomNode->id, "master", DAW::ConnectionType::Direct) == nullptr);
                expect(findGraphEdge(graph, topNode->id, folderNode->id, DAW::ConnectionType::FolderSum) != nullptr);
                expect(findGraphEdge(graph, bottomNode->id, folderNode->id, DAW::ConnectionType::FolderSum) != nullptr);
                expect(findGraphEdge(graph, folderNode->id, "master", DAW::ConnectionType::Direct) != nullptr);

                masterRoutes.deleteMasterRoute(folderId);
                expect(findGraphEdge(graph, folderNode->id, "master", DAW::ConnectionType::Direct) == nullptr);
                auto snapshot = graph.getSnapshotPublisher().get();
                auto* retiring = snapshot != nullptr
                    ? findSnapshotEdge(*snapshot, folderNode->id, "master", DAW::ConnectionType::Direct)
                    : nullptr;
                expect(retiring != nullptr && retiring->retiring,
                       "FolderBus Master deletion must use the same silent retirement contract");
            }
        }

        beginTest("all cable types resolve nested collapsed folders to the outermost visible representative");
        {
            DAW::TrackManager tracks;
            DAW::RoutingGraph graph;
            DAW::MasterRouteStateCore masterRoutes(graph);
            DAW::FolderBusCore folders;

            auto* leaf = tracks.createTrack("Leaf");
            auto* sibling = tracks.createTrack("Sibling");
            graph.addNode(leaf->getName(), DAW::RoutingNodeType::Track, leaf->getID());
            graph.addNode(sibling->getName(), DAW::RoutingNodeType::Track, sibling->getID());

            const auto leafId = leaf->getID();
            const auto siblingId = sibling->getID();
            const auto innerId = folders.createFolderBus(
                "Inner", { leafId }, {}, graph, masterRoutes, tracks);
            const auto outerId = folders.createFolderBus(
                "Outer", { innerId, siblingId }, {}, graph, masterRoutes, tracks);

            std::unordered_set<DAW::TrackID> collapsed { outerId, innerId };
            std::unordered_set<DAW::TrackID> visible { outerId };
            auto resolve = [&](const DAW::TrackID& routeEndpoint)
            {
                return DAW::BubblegumFolderProjectionCore::resolveVisibleRepresentative(
                    routeEndpoint, &folders, collapsed,
                    [&visible](const DAW::TrackID& candidate)
                    {
                        return visible.count(candidate) != 0;
                    });
            };

            expectEquals(resolve(leafId), outerId);
            expectEquals(resolve(innerId), outerId);
            expectEquals(resolve(siblingId), outerId);
            expectEquals(resolve(outerId), outerId);
            // NormalSend, MasterSend and Sidechain all consume this same pure
            // endpoint projection before applying their type-specific curves.
        }

        beginTest("FolderBus conversion preserves identity, routes, children and published node role");
        {
            DAW::TrackManager tracks;
            DAW::RoutingGraph graph;
            DAW::MasterRouteStateCore masterRoutes(graph);
            DAW::FolderBusCore folders;

            auto* first = tracks.createTrack("First");
            auto* second = tracks.createTrack("Second");
            graph.addNode(first->getName(), DAW::RoutingNodeType::Track, first->getID());
            graph.addNode(second->getName(), DAW::RoutingNodeType::Track, second->getID());
            const auto firstId = first->getID();
            const auto secondId = second->getID();
            const auto folderId = folders.createFolderBus(
                "Folder", { firstId, secondId }, {}, graph, masterRoutes, tracks);
            auto* folderNode = graph.getNodeByTrackId(folderId);
            auto* auxNode = graph.addNode("Aux", DAW::RoutingNodeType::Bus, "aux-track");
            expect(folderNode != nullptr && auxNode != nullptr);
            const auto folderNodeId = folderNode != nullptr ? folderNode->id : juce::String();
            const auto auxNodeId = auxNode != nullptr ? auxNode->id : juce::String();
            auto* send = folderNode != nullptr && auxNode != nullptr
                ? graph.connect(folderNode->id, auxNode->id, DAW::ConnectionType::Send)
                : nullptr;
            expect(send != nullptr);

            expect(folders.convertFolderBusToTrack(folderId, graph, masterRoutes, tracks));
            expect(!folders.isFolderBus(folderId));
            auto* converted = tracks.getTrack(folderId);
            expect(converted != nullptr);
            if (converted != nullptr)
                expectEquals((int) converted->getRole(), (int) DAW::TrackRole::Audio);
            auto* convertedNode = graph.getNodeByTrackId(folderId);
            expect(convertedNode != nullptr);
            if (convertedNode != nullptr)
            {
                expectEquals(convertedNode->id, folderNodeId);
                expectEquals((int) convertedNode->type, (int) DAW::RoutingNodeType::Track);
            }
            expect(folders.getParentFolderBus(firstId).isEmpty());
            expect(folders.getParentFolderBus(secondId).isEmpty());
            expectEquals(tracks.getTrack(0)->getID(), folderId);
            expectEquals(tracks.getTrack(1)->getID(), firstId);
            expectEquals(tracks.getTrack(2)->getID(), secondId);
            auto snapshot = graph.getSnapshotPublisher().get();
            bool snapshotHasTrackRole = false;
            bool snapshotHasSendEdge = false;
            if (snapshot != nullptr)
            {
                for (const auto& node : snapshot->nodes)
                    if (node.id == folderNodeId && node.type == DAW::RoutingNodeType::Track)
                        snapshotHasTrackRole = true;
                if (findSnapshotEdge(*snapshot, folderNodeId, auxNodeId, DAW::ConnectionType::Send) != nullptr)
                    snapshotHasSendEdge = true;
            }
            expect(snapshotHasSendEdge,
                   "non-folder sends from the converted track must survive");
            expect(snapshotHasTrackRole);
        }

        beginTest("exclusive mute command restores the complete mute and solo vector");
        {
            DAW::TrackManager tracks;
            auto* target = tracks.createTrack("Target");
            auto* other = tracks.createTrack("Other");
            auto* master = tracks.createMasterTrack();
            expect(master != nullptr);
            target->setMuted(true);
            other->setSoloed(true);
            master->setMuted(true);

            std::vector<DAW::ExclusiveTrackMuteCommand::Entry> entries {
                { target->getID(), true, false, false, false },
                { other->getID(), false, true, true, false }
            };
            DAW::ExclusiveTrackMuteCommand command(tracks, entries);
            command.execute();
            expect(!target->isMuted());
            expect(other->isMuted());
            expect(!other->isSoloed());
            expect(master->isMuted(), "Master state must remain outside the command");

            command.undo();
            expect(target->isMuted());
            expect(!other->isMuted());
            expect(other->isSoloed());
            expect(master->isMuted());
        }

        beginTest("stem route plan isolates programme paths while retaining sidechain dependencies");
        {
            DAW::RoutingGraph graph;
            auto* vocal = graph.addNode("Vocal", DAW::RoutingNodeType::Track, "track-vocal");
            auto* key = graph.addNode("Key", DAW::RoutingNodeType::Track, "track-key");
            auto* folder = graph.addNode("Folder", DAW::RoutingNodeType::FolderBus, "track-folder");
            expect(vocal != nullptr && key != nullptr && folder != nullptr);

            // Replace the generated direct routes with explicit folder routing.
            if (auto* direct = findGraphEdge(graph, vocal->id, "master", DAW::ConnectionType::Direct))
                graph.disconnect(direct->id);
            graph.publishSnapshotOnly();
            auto* vocalToFolder = graph.connect(vocal->id, folder->id, DAW::ConnectionType::FolderSum);
            auto* keySidechain = graph.connect(key->id, vocal->id, DAW::ConnectionType::Sidechain);
            auto snapshot = graph.getSnapshotPublisher().get();
            expect(snapshot != nullptr && vocalToFolder != nullptr && keySidechain != nullptr);

            if (snapshot != nullptr)
            {
                DAW::StemExportTarget target;
                target.trackId = "track-vocal";
                target.displayName = "Vocal";
                target.sourceTrackIds = { "track-vocal" };
                auto mask = DAW::StemRouteMaskCore::build(*snapshot, target);
                expect(mask != nullptr);
                if (mask != nullptr)
                {
                    expect(mask->programmeEdges.count(vocalToFolder->id) > 0);
                    expect(mask->sidechainEdges.count(keySidechain->id) > 0);
                    auto* keyMaster = findGraphEdge(graph, key->id, "master", DAW::ConnectionType::Direct);
                    expect(keyMaster != nullptr);
                    if (keyMaster != nullptr)
                        expect(mask->programmeEdges.count(keyMaster->id) == 0,
                               "the sidechain key must not leak into the programme stem");
                }
            }
        }
    }
};

RoutingFolderRegressionTests routingFolderRegressionTests;
}
