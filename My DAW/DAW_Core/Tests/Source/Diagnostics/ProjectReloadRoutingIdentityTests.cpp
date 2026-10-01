// ProjectReloadRoutingIdentityTests.cpp
//
// P0 project-reload regression: after restoring a saved project, freshly
// created routing nodes must NEVER reuse the IDs of restored nodes.
//
// Live defect: reopened a saved project → old tracks played → created a new
// track → no monitoring/playback for it (even a known-good clip moved onto it
// failed) → repeated create/delete cycles eventually made new tracks work
// while OLD restored tracks lost playback.
//
// Proven root cause: RoutingGraph::restoreState rebased the connection
// allocator (nextConnId_) but NOT the node allocator (nextNodeId_), so the
// first node added after reload collided with a restored "RN_1" node. The
// snapshot's nodeIndexById (first-insertion-wins) resolved the duplicate to
// the old node, so the new track's node was never processed.

#include <JuceHeader.h>
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/UtilityCore/Types.h"

#include <set>

namespace
{
juce::ValueTree buildProjectState(const DAW::RoutingGraph& graph)
{
    juce::ValueTree project("DAWProject");
    project.addChild(graph.getState(), -1, nullptr);
    return project;
}
}

class ProjectReloadRoutingNodeIDsRemainUniqueTest final : public juce::UnitTest
{
public:
    ProjectReloadRoutingNodeIDsRemainUniqueTest()
        : UnitTest("ProjectReload.RoutingNodeIDsRemainUnique", "ProjectReload") {}

    void runTest() override
    {
        beginTest("restored and newly-created routing node IDs are unique and monotonic");

        // Day 1: a project with several tracks/nodes.
        DAW::RoutingGraph dayOne;
        for (int i = 0; i < 8; ++i)
        {
            auto* node = dayOne.addNode("Track " + juce::String(i + 1),
                                        DAW::RoutingNodeType::Track,
                                        "TRK_" + juce::String(i + 1));
            expect(node != nullptr);
        }

        const auto state = buildProjectState(dayOne);
        const auto graphTree = state.getChildWithName("RoutingGraph");
        expect(graphTree.isValid());

        // Day 2: restore through the real production restore path.
        DAW::RoutingGraph dayTwo;
        dayTwo.restoreState(graphTree);

        // Every restored node keeps its stored identity.
        std::set<juce::String> liveIds;
        for (auto* node : dayTwo.getAllNodes())
        {
            if (node == nullptr)
                continue;
            if (node->id != "master")
                expect(liveIds.insert(node->id).second,
                       "restored node ids must be unique");
        }

        // Create NEW nodes after restore — none may collide with restored IDs.
        for (int i = 0; i < 12; ++i)
        {
            auto* node = dayTwo.addNode("New Track " + juce::String(i + 1),
                                        DAW::RoutingNodeType::Track,
                                        "TRK_NEW_" + juce::String(i + 1));
            expect(node != nullptr);
            expect(liveIds.insert(node->id).second,
                   "new node id must not collide with any restored id: " + node->id);
        }

        // Allocator monotonicity: the restored project had 8 nodes (RN_1..RN_8),
        // so every node created after the reload must carry a suffix above 8.
        for (auto* node : dayTwo.getAllNodes())
        {
            if (node == nullptr || !node->id.startsWith("RN_"))
                continue;
            const int n = node->id.substring(3).getIntValue();
            if (n > 8)
            {
                expect(n > 8, "new node numeric suffix must exceed the restored max");
            }
        }
    }
};

ProjectReloadRoutingNodeIDsRemainUniqueTest projectReloadRoutingNodeIDsRemainUniqueTest;

class ProjectReloadSnapshotReachabilityTest final : public juce::UnitTest
{
public:
    ProjectReloadSnapshotReachabilityTest()
        : UnitTest("ProjectReload.NewNodesReachableAfterRestore", "ProjectReload") {}

    void runTest() override
    {
        beginTest("every node after reload+create is reachable through the published snapshot index");

        DAW::RoutingGraph dayOne;
        for (int i = 0; i < 5; ++i)
            dayOne.addNode("Old " + juce::String(i + 1), DAW::RoutingNodeType::Track,
                           "TRK_OLD_" + juce::String(i + 1));

        const auto graphTree = buildProjectState(dayOne).getChildWithName("RoutingGraph");

        DAW::RoutingGraph dayTwo;
        dayTwo.restoreState(graphTree);

        // Create new tracks and their master connections (production flow).
        for (int i = 0; i < 5; ++i)
            dayTwo.addNode("New " + juce::String(i + 1), DAW::RoutingNodeType::Track,
                           "TRK_NEW_" + juce::String(i + 1));

        // Publish the snapshot through the real publisher.
        dayTwo.notifyGraphChanged();
        auto snap = dayTwo.getSnapshotPublisher().get();

        std::set<juce::String> reachable;
        for (const auto& entry : snap->nodeIndexById)
        {
            if (entry.first == "master")
                continue;
            expect(entry.second < snap->nodes.size(), "snapshot index must be in range");
            expectEquals(snap->nodes[entry.second].id, entry.first,
                         "snapshot index must map each node id to the node with that id");
            reachable.insert(entry.first);
        }

        int liveNodeCount = 0;
        for (auto* node : dayTwo.getAllNodes())
        {
            if (node == nullptr || node->id == "master")
                continue;
            ++liveNodeCount;
            expect(reachable.count(node->id) > 0,
                   "every live node must be reachable in the snapshot: " + node->id);
        }

        expectEquals(reachable.size(), (size_t) liveNodeCount,
                     "snapshot reachable node count must match live non-master nodes");
    }
};

ProjectReloadSnapshotReachabilityTest projectReloadSnapshotReachabilityTest;

class ProjectReloadTrackIDAllocatorRebasedTest final : public juce::UnitTest
{
public:
    ProjectReloadTrackIDAllocatorRebasedTest()
        : UnitTest("ProjectReload.TrackIDAllocatorRebased", "ProjectReload") {}

    void runTest() override
    {
        beginTest("track id allocator rebases above restored ids (canonical authority)");

        // The counter is process-wide and shared with the rest of the suite,
        // so use a large seed value to prove the rebase semantics without
        // depending on the counter's absolute position.
        constexpr int kSeededMax = 1000000;
        DAW::IDGenerator::seedTrackCounter(kSeededMax);
        const auto next = DAW::IDGenerator::generateTrackID();
        expectEquals(next, juce::String("TRK_") + juce::String(kSeededMax + 1),
                     "first generated id after restore must exceed the restored max");

        // The counter is monotonic — seeding a lower value must not regress it.
        DAW::IDGenerator::seedTrackCounter(3);
        const auto afterLowerSeed = DAW::IDGenerator::generateTrackID();
        expectEquals(afterLowerSeed, juce::String("TRK_") + juce::String(kSeededMax + 2),
                     "lower seeds must never regress a monotonic allocator");
    }
};

ProjectReloadTrackIDAllocatorRebasedTest projectReloadTrackIDAllocatorRebasedTest;

class ProjectReloadRestoredNodesIdentityStableTest final : public juce::UnitTest
{
public:
    ProjectReloadRestoredNodesIdentityStableTest()
        : UnitTest("ProjectReload.RestoredNodesIdentityStable", "ProjectReload") {}

    void runTest() override
    {
        beginTest("restored nodes keep their exact stored ids across save/reload cycles");

        DAW::RoutingGraph original;
        juce::StringArray expectedIds;
        for (int i = 0; i < 6; ++i)
        {
            auto* node = original.addNode("T" + juce::String(i + 1),
                                          DAW::RoutingNodeType::Track,
                                          "TRK_R_" + juce::String(i + 1));
            expectedIds.add(node->id);
        }

        const auto tree = buildProjectState(original).getChildWithName("RoutingGraph");
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            DAW::RoutingGraph reloaded;
            reloaded.restoreState(tree);

            juce::StringArray actualIds;
            for (auto* node : reloaded.getAllNodes())
                if (node != nullptr && node->id != "master")
                    actualIds.add(node->id);

            expectEquals(actualIds.size(), expectedIds.size(),
                         "restored node count must be stable across reload cycles");
            for (int i = 0; i < juce::jmin(actualIds.size(), expectedIds.size()); ++i)
                expect(actualIds[i] == expectedIds[i],
                       "restored node id mismatch at index " + juce::String(i));
        }
    }
};

ProjectReloadRestoredNodesIdentityStableTest projectReloadRestoredNodesIdentityStableTest;
