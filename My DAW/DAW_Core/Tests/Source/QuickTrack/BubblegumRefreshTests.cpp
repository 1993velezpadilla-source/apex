// ===========================================================================
// BubblegumRefreshTests.cpp
//
// Tests the restored Bubblegum presentation contract. The application target
// owns BubblegumCableOverlayComponent.cpp; the headless test target does not
// link that UI translation unit. The scheduler probe below mirrors the
// observable presentation-tick decisions while the routing/state/animation
// tests exercise the real Bubblegum cores and the real RoutingGraph.
//
// Contract under test:
//
//     ApexPresentationClock::TickReceiver
//       -> onPresentationTick(deltaSeconds)
//       -> poll Mixer/scroll motion
//       -> onTick()
//       -> refresh cable state / advance animation
//       -> repaint only when needed
// ===========================================================================
#include <JuceHeader.h>

#include "../../../Source/QuickTrackCore/QuickTrackBuilderCore.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/RoutingCore/MasterRouteStateCore.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/FolderBusCore/FolderBusCore.h"

#include "../../../Source/Bubblegum/BubblegumSendStateCore.h"
#include "../../../Source/Bubblegum/BubblegumSidechainStateCore.h"
#include "../../../Source/Bubblegum/BubblegumV2System.h"
#include "../../../Source/Bubblegum/BubblegumRoutingAdapter_SourceSyncExample.h"
#include "../../../Source/Bubblegum/BubblegumAnchorResolver.h"
#include "../../../Source/Bubblegum/BubblegumCableSnapshotBuilder.h"
#include "../../../Source/BubblegumCable/BubblegumCableSystem.h"
#include "../../../Source/BubblegumCable/BubblegumCableSidechainRenderCore.h"
#include "../../../Source/UICore/BubblegumCableOverlayComponent.h"
#include "../../../Source/UICore/BubblegumOffscreenEndpointComponent.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace
{

using namespace DAW;

// ---------------------------------------------------------------------------
// Test-side representation of presentation-tick decisions.
//
// This intentionally contains no source-selection or geometry-revision gate.
// It models only the behavior that is observable at the tick boundary:
// polling invalidates presentation state, onTick is dispatched every tick, and
// repaint is requested only for visible dirty work (or a clear).
// ---------------------------------------------------------------------------
struct PresentationRefreshProbe final : RoutingGraph::Listener
{
    bool overlayVisible = true;
    bool cablesVisible = true;
    bool snapshotCacheValid = true;
    bool compositeFrameDirty = false;
    bool wasRendering = false;
    int forceRefreshFrames = 0;
    int lastScrollX = std::numeric_limits<int>::min();
    int lastScrollY = std::numeric_limits<int>::min();

    int presentationTickCount = 0;
    int onTickCount = 0;
    int paintedTickCount = 0;
    int gatedTickCount = 0;
    int topologyRefreshCount = 0;

    std::function<void()> onTick;

    void requestTopologyRefresh()
    {
        snapshotCacheValid = false;
        compositeFrameDirty = true;
        forceRefreshFrames = juce::jmax(forceRefreshFrames, 8);
        ++topologyRefreshCount;
    }

    void connectionAdded(RoutingConnection*) override { requestTopologyRefresh(); }
    void connectionRemoved(const RouteID&) override { requestTopologyRefresh(); }
    void graphChanged() override { requestTopologyRefresh(); }

    void presentationTick(int scrollX, int scrollY,
                       bool sidechainAnimating = false)
    {
        ++presentationTickCount;

        // Motion polling: no callback/revision authority is involved.
        if (scrollX != lastScrollX || scrollY != lastScrollY)
        {
            lastScrollX = scrollX;
            lastScrollY = scrollY;
            snapshotCacheValid = false;
            compositeFrameDirty = true;
        }

        if (onTick)
        {
            ++onTickCount;
            onTick();
        }

        const bool nowRendering = overlayVisible && cablesVisible;
        const bool snapshotStale = nowRendering && !snapshotCacheValid;
        const bool clearFrame = !nowRendering && wasRendering;
        const bool forceRefresh = nowRendering && forceRefreshFrames > 0;

        if (snapshotStale || forceRefresh || (nowRendering && sidechainAnimating))
        {
            compositeFrameDirty = true;
            if (forceRefresh)
            {
                snapshotCacheValid = false;
                --forceRefreshFrames;
            }
        }

        if (overlayVisible && (compositeFrameDirty || clearFrame))
            ++paintedTickCount;
        else
            ++gatedTickCount;

        wasRendering = nowRendering;
    }
};

struct FixedSourceSync
{
    TrackID source;
    bubblegum::TrackId getSourceTrackId() const { return source; }
};

// The same stable-ID lookup used by the production overlay when it maps a
// Bubblegum send record back to the live RoutingGraph connection.
struct GraphSendStateAdapter
{
    explicit GraphSendStateAdapter(const RoutingGraph& graphIn) : graph(graphIn) {}

    bool exists(bubblegum::SendId sendId) const noexcept
    {
        return find(sendId) != nullptr;
    }

    bool isActive(bubblegum::SendId sendId) const noexcept
    {
        const auto* connection = find(sendId);
        return connection != nullptr
            && connection->active.load(std::memory_order_relaxed);
    }

private:
    const RoutingGraph& graph;

    const RoutingConnection* find(bubblegum::SendId sendId) const noexcept
    {
        for (auto* connection : graph.getAllConnections())
        {
            if (connection == nullptr)
                continue;

            const bool isSend = connection->type == ConnectionType::Send;
            const bool isDirect = connection->type == ConnectionType::Direct;
            if ((isSend || isDirect)
                && static_cast<bubblegum::SendId>(connection->id.hashCode64()) == sendId)
                return connection;
        }

        return nullptr;
    }
};

struct Harness
{
    TrackManager tracks;
    RoutingGraph graph;
    MasterRouteStateCore masterRoute { graph };
    BubblegumSendStateCore sends;
    QuickTrackColorSystem colors { 70707 };
    QuickTrackBuilderCore builder { tracks, graph, masterRoute, sends, colors };
    PresentationRefreshProbe refresh;

    Harness()
    {
        tracks.createMasterTrack();
        graph.addListener(&refresh);
    }

    ~Harness()
    {
        graph.removeListener(&refresh);
    }
};

static const bubblegum::CableWorldSnapshot* findSnapshot(
    const std::vector<bubblegum::CableWorldSnapshot>& snapshots,
    const TrackID& source,
    const TrackID& destination)
{
    for (const auto& snapshot : snapshots)
        if (snapshot.key.sourceTrack == source
            && snapshot.key.destinationTrack == destination)
            return &snapshot;

    return nullptr;
}

static bool hasDestination(
    const std::vector<bubblegum::BubblegumSendRecord>& records,
    const TrackID& destination)
{
    return std::any_of(records.begin(), records.end(),
                       [&destination](const auto& record)
                       {
                           return record.destinationTrackId == destination;
                       });
}

class BubblegumRefreshTests final : public juce::UnitTest
{
public:
    BubblegumRefreshTests()
        : juce::UnitTest("BubblegumRefresh", "APEX.BubblegumRefresh")
    {
    }

    void runTest() override
    {
        beginTest("OverlayUsesSharedPresentationClock");
        {
            using Overlay = BubblegumCableOverlayComponent;
            static_assert(std::is_base_of<ApexPresentationClock::TickReceiver, Overlay>::value,
                          "Bubblegum overlay must receive the shared presentation clock");
            static_assert(! std::is_base_of<juce::Timer, Overlay>::value,
                          "Bubblegum overlay must not own an independent timer");

            const auto tick = &Overlay::onPresentationTick;
            expect(tick != nullptr,
                   "onPresentationTick is the shared clock entry point");
        }

        beginTest("PresentationTickDispatchesOnTickAndGatesRepaint");
        {
            PresentationRefreshProbe probe;
            probe.onTick = [this, &probe]
            {
                expect(probe.presentationTickCount > 0,
                       "onTick runs from inside presentationTick");
            };

            // Static, clean frames are gated rather than repainting the whole
            // Mixer on every presentation tick.
            probe.presentationTick(0, 0);
            probe.snapshotCacheValid = true;
            probe.compositeFrameDirty = false;
            probe.presentationTick(0, 0);

            expectEquals(probe.presentationTickCount, 2,
                         "presentation tick is invoked for each clock update");
            expectEquals(probe.onTickCount, 2,
                         "onTick is dispatched through the presentation path");
            expect(probe.gatedTickCount > 0,
                   "clean frames remain repaint-gated");
            expect(probe.paintedTickCount > 0,
                   "the initial dirty frame is eligible for repaint");
        }

        beginTest("MixerAndScrollMotionAreDetectedByPresentationTicks");
        {
            PresentationRefreshProbe probe;
            probe.presentationTick(10, 0);
            probe.snapshotCacheValid = true;
            probe.compositeFrameDirty = false;
            const int paintedBeforeStaticFrame = probe.paintedTickCount;

            probe.presentationTick(10, 0);
            expectEquals(probe.paintedTickCount, paintedBeforeStaticFrame,
                         "unchanged polled geometry does not repaint");

            probe.presentationTick(42, 0);
            expect(probe.paintedTickCount > paintedBeforeStaticFrame,
                   "scroll motion invalidates the presentation cache");
            expectEquals(probe.lastScrollX, 42,
                         "legacy polling stores the latest scroll position");
        }

        beginTest("RoutingMutationRefreshesNormalSendState");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            auto result = h.builder.createBatch({ { doubles, 1 } });
            expect(result.ok(), "source track fixture created");
            if (result.ok())
            {
                const TrackID sourceId = result.created[0].trackId;
                const TrackID destinationId = "Bubblegum-Reverb";
                h.graph.addNode("Bubblegum Reverb", RoutingNodeType::Bus, destinationId);

                const int refreshesBefore = h.refresh.topologyRefreshCount;
                h.sends.createSend(h.graph, sourceId, destinationId, 0.5f);

                expect(h.refresh.topologyRefreshCount > refreshesBefore,
                       "real RoutingGraph mutation reaches the refresh listener");
                expect(!h.refresh.snapshotCacheValid,
                       "normal Send mutation invalidates cached cable state");

                bubblegum::BubblegumRoutingAdapterSourceSyncExample adapter;
                FixedSourceSync sync { sourceId };
                const auto records = adapter.getSendRecords(h.graph, sync, {});
                expect(hasDestination(records, destinationId),
                       "normal Send is present in the live presentation records");

                bubblegum::BubblegumAnchorResolver resolver;
                resolver.setTrackBounds(sourceId, { 20.0f, 80.0f, 1.0f, 1.0f });
                resolver.setTrackBounds(destinationId, { 220.0f, 80.0f, 1.0f, 1.0f });
                resolver.setTrackBounds(h.tracks.getMasterTrack()->getID(),
                                        { 420.0f, 80.0f, 1.0f, 1.0f });

                GraphSendStateAdapter sendState(h.graph);
                bubblegum::BubblegumCableSnapshotBuilder builder;
                const auto activeSnapshots = builder.build(records, sendState, resolver);
                const auto* active = findSnapshot(activeSnapshots, sourceId, destinationId);
                expect(active != nullptr, "active Send snapshot is built");
                if (active != nullptr)
                {
                    expect(active->state == bubblegum::SendVisualState::ExistsActive,
                           "active RoutingGraph Send maps to ExistsActive");
                    expect(active->visible, "resolved Send endpoints are visible");
                }

                h.sends.toggleSendActive(h.graph, sourceId, destinationId);
                const auto inactiveRecords = adapter.getSendRecords(h.graph, sync, {});
                const auto inactiveSnapshots = builder.build(inactiveRecords, sendState, resolver);
                const auto* inactive = findSnapshot(inactiveSnapshots, sourceId, destinationId);
                expect(inactive != nullptr, "inactive Send remains in the visual model");
                if (inactive != nullptr)
                    expect(inactive->state == bubblegum::SendVisualState::ExistsInactive,
                           "bypassed Send maps to ExistsInactive without deleting identity");
            }
        }

        beginTest("SidechainStateRefreshesFromTheLiveRoutingGraph");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            auto result = h.builder.createBatch({ { doubles, 2 } });
            expect(result.ok(), "sidechain source fixtures created");
            if (result.ok())
            {
                const TrackID sourceId = result.created[0].trackId;
                const TrackID destinationId = result.created[1].trackId;
                BubblegumSidechainStateCore sidechains;
                const int refreshesBefore = h.refresh.topologyRefreshCount;
                auto* connection = sidechains.createSidechain(
                    h.graph, sourceId, destinationId, "Compressor", 1, TapPoint::PreFX);

                expect(connection != nullptr, "sidechain connection is created");
                expect(h.refresh.topologyRefreshCount > refreshesBefore,
                       "sidechain mutation reaches the RoutingGraph listener");
                expect(sidechains.sidechainExists(h.graph, sourceId, destinationId),
                       "sidechain state exists in the live graph");
                expect(sidechains.isSidechainActive(h.graph, sourceId, destinationId),
                       "new sidechain is active");

                int matchingSidechains = 0;
                for (auto* candidate : h.graph.getAllConnections())
                {
                    if (candidate == nullptr || candidate->type != ConnectionType::Sidechain)
                        continue;
                    auto* srcNode = h.graph.getNode(candidate->sourceNodeId);
                    auto* dstNode = h.graph.getNode(candidate->destNodeId);
                    if (srcNode != nullptr && dstNode != nullptr
                        && srcNode->trackId == sourceId && dstNode->trackId == destinationId)
                        ++matchingSidechains;
                }
                expectEquals(matchingSidechains, 1,
                             "live sidechain enumeration finds exactly one edge");

                sidechains.toggleActive(h.graph, sourceId, destinationId);
                expect(!sidechains.isSidechainActive(h.graph, sourceId, destinationId),
                       "sidechain toggle changes active state without removing the edge");
                expect(sidechains.sidechainExists(h.graph, sourceId, destinationId),
                       "inactive sidechain identity remains present");
            }
        }

        beginTest("SendAndSidechainAnimationAdvanceOnTheirFrameTicks");
        {
            BubblegumCableSystem sendCables;
            const float sendTimeBefore = sendCables.animation().time();
            sendCables.tick(16.0f, 0.0f);
            expect(sendCables.animation().time() > sendTimeBefore,
                   "Send cable animation time advances on a frame tick");

            BubblegumV2System bubblegum;
            bubblegum.setSidechainCablesVisible(true);
            bubblegum.feedSidechainTriggerLevel("edge-1", 1.0f);
            expect(!bubblegum.hasSidechainCablesActive(),
                   "sidechain is not active before its first animation tick");
            bubblegum.tick(16.0f);
            expect(bubblegum.hasSidechainCablesActive(),
                   "sidechain flow advances through the Bubblegum frame tick");

            BubblegumCableSidechainRenderCore sidechain;
            sidechain.feedTriggerLevel("edge-2", 1.0f);
            sidechain.tick(16.0f);
            expect(sidechain.hasActiveEdges(),
                   "sidechain renderer reports active animated state after tick");
        }

        beginTest("SelectionChangesRefreshTheLegacySourceProjection");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            auto result = h.builder.createBatch({ { doubles, 2 } });
            expect(result.ok(), "selection source fixtures created");
            if (result.ok())
            {
                const TrackID sourceA = result.created[0].trackId;
                const TrackID sourceB = result.created[1].trackId;
                const TrackID busA = "Bubblegum-Source-A-Bus";
                const TrackID busB = "Bubblegum-Source-B-Bus";
                h.graph.addNode("Source A Bus", RoutingNodeType::Bus, busA);
                h.graph.addNode("Source B Bus", RoutingNodeType::Bus, busB);
                h.sends.createSend(h.graph, sourceA, busA, 0.5f);
                h.sends.createSend(h.graph, sourceB, busB, 0.5f);

                BubblegumV2System bubblegum;
                bubblegum.init(h.graph, h.tracks);
                bubblegum::BubblegumRoutingAdapterSourceSyncExample adapter;

                bubblegum.onTrackSelected(sourceA);
                const auto recordsA = adapter.getSendRecords(h.graph, bubblegum.sourceSync, {});
                expectEquals(bubblegum.sourceSync.getSourceTrackId(), sourceA,
                             "selected source A is reflected by the legacy source sync");
                expect(hasDestination(recordsA, busA),
                       "source A selection exposes source A's Send records");

                bubblegum.onTrackSelected(sourceB);
                const auto recordsB = adapter.getSendRecords(h.graph, bubblegum.sourceSync, {});
                expectEquals(bubblegum.sourceSync.getSourceTrackId(), sourceB,
                             "selected source B is reflected by the legacy source sync");
                expect(hasDestination(recordsB, busB),
                       "source B selection exposes source B's Send records");
            }
        }

        beginTest("BubblegumSendAutomationRestoresSavedLevelAndBypassBindings");
        {
            juce::ScopedJuceInitialiser_GUI gui;
            auto& store = apex::automation::AutomationLaneStore::getInstance();
            auto& keys = apex::automation::AutomationParameterKeyRegistry::getInstance();
            auto& sys = apex::automation::AutomationSystem::getInstance();
            const auto previousAutomation = store.getState().createCopy();

            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto result = h.builder.createBatch({ { doubles, 2 } });
            expect(result.ok(), "fixture must contain two real routed tracks");
            if (result.ok())
            {
                const TrackID src = result.created[0].trackId;
                const TrackID dst = result.created[1].trackId;
                auto* originalSend = h.sends.createSend(h.graph, src, dst, 0.75f);
                expect(originalSend != nullptr, "fixture must contain a send");

                if (originalSend != nullptr)
                {
                    const auto routeId = originalSend->id;
                    const auto levelKey = apex::automation::AutomationParameterKeyRegistry::trackSendLevelKey(src, routeId);
                    const auto bypassKey = apex::automation::AutomationParameterKeyRegistry::trackSendBypassKey(src, routeId);
                    const auto levelID = keys.getOrCreateID(levelKey);
                    const auto bypassID = keys.getOrCreateID(bypassKey);
                    store.getOrCreateLane(levelID).replacePoints(
                        {{0.0, 0.25f, apex::automation::CurveType::Linear, 0.0f},
                         {10.0, 0.75f, apex::automation::CurveType::Linear, 0.0f}});
                    store.getOrCreateLane(bypassID).replacePoints(
                        {{0.0, 0.0f, apex::automation::CurveType::Hold, 0.0f},
                         {10.0, 1.0f, apex::automation::CurveType::Hold, 0.0f}});

                    const auto savedRouting = h.graph.getState().createCopy();
                    const auto savedAutomation = store.getState().createCopy();
                    BubblegumV2System bubblegum;
                    bubblegum.init(h.graph, h.tracks);

                    // Recreate the graph and the actual persisted lane state,
                    // then rebind WITHOUT touching static send values.
                    h.graph.restoreState(savedRouting);
                    store.restoreState(savedAutomation);
                    const auto* restoredSend = h.graph.getAllConnections().size() > 0
                        ? h.graph.getAllConnections()[h.graph.getAllConnections().size() - 1] : nullptr;
                    float initialGain = 0.0f;
                    if (restoredSend != nullptr)
                        initialGain = restoredSend->gain.load(std::memory_order_relaxed);

                    bubblegum.rebindPersistedSendAutomation();
                    expectEquals(static_cast<int>(bubblegum.sendAutomationBindings.size()), 2,
                                 "restored saved send level and bypass lanes must have bindings");
                    bubblegum.rebindPersistedSendAutomation();
                    expectEquals(static_cast<int>(bubblegum.sendAutomationBindings.size()), 2,
                                 "rebind must be idempotent");
                    if (restoredSend != nullptr)
                        expectWithinAbsoluteError(restoredSend->gain.load(std::memory_order_relaxed),
                                                  initialGain, 1.0e-6f,
                                                  "rebind does not mutate the saved static send level");

                    auto* levelParam = sys.getRegistry().find(levelID);
                    auto* bypassParam = sys.getRegistry().find(bypassID);
                    expect(levelParam != nullptr && bypassParam != nullptr,
                           "restored send automation must have live parameters");
                    if (levelParam != nullptr && bypassParam != nullptr)
                    {
                        bubblegum.parameterValueChanged(*levelParam, 0.20f,
                                                       apex::automation::ChangeSource::Automation);
                        bubblegum.parameterValueChanged(*bypassParam, 1.0f,
                                                       apex::automation::ChangeSource::Automation);
                        if (restoredSend != nullptr)
                        {
                            expectWithinAbsoluteError(
                                restoredSend->gain.load(std::memory_order_relaxed), 0.40f, 1.0e-6f,
                                "level playback must update the real routed send");
                            expect(!restoredSend->active.load(std::memory_order_relaxed),
                                   "bypass playback must deactivate the real routed send");
                        }
                    }
                    bubblegum.releaseProjectSendAutomationBindings();
                    expect(bubblegum.sendAutomationBindings.empty(),
                           "project teardown must detach obsolete send listeners");
                    expect(sys.getRegistry().find(levelID) == nullptr
                        && sys.getRegistry().find(bypassID) == nullptr,
                           "project teardown must unregister previous send parameters");
                }
            }

            // Do not leak this unit test's fake project into unrelated tests.
            store.restoreState(previousAutomation);
        }

        beginTest("QuickSendTogglePreservesRoutingSemantics");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            auto result = h.builder.createBatch({ { doubles, 2 } });
            expect(result.ok(), "Quick Send fixtures created");
            if (result.ok())
            {
                const TrackID sourceId = result.created[0].trackId;
                const TrackID destinationId = result.created[1].trackId;

                h.sends.toggleSend(h.graph, sourceId, destinationId, 0.75f);
                expect(h.sends.hasSend(h.graph, sourceId, destinationId),
                       "first Quick Send tap creates a Send");
                expect(h.sends.isSendActive(h.graph, sourceId, destinationId),
                       "new Quick Send is active");

                h.sends.toggleSend(h.graph, sourceId, destinationId);
                expect(h.sends.sendExists(h.graph, sourceId, destinationId),
                       "second Quick Send tap keeps the connection identity");
                expect(!h.sends.isSendActive(h.graph, sourceId, destinationId),
                       "second Quick Send tap bypasses instead of deleting");

                h.sends.toggleSend(h.graph, sourceId, destinationId);
                expect(h.sends.isSendActive(h.graph, sourceId, destinationId),
                       "third Quick Send tap re-enables the Send");

                h.sends.deleteSend(h.graph, sourceId, destinationId);
                expect(!h.sends.sendExists(h.graph, sourceId, destinationId),
                       "explicit delete remains the only removal action");

                h.sends.toggleSend(h.graph, sourceId, sourceId);
                expect(!h.sends.sendExists(h.graph, sourceId, sourceId),
                       "Quick Send still rejects self-routing");
            }
        }

        beginTest("FolderProjectedDestinationUsesCollapsedAncestor");
        {
            Harness h;
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            auto result = h.builder.createBatch({ { doubles, 2 } });
            expect(result.ok(), "folder projection fixtures created");
            if (result.ok())
            {
                const TrackID sourceId = result.created[0].trackId;
                const TrackID destinationId = result.created[1].trackId;
                FolderBusCore folders;
                const TrackID folderId = folders.createFolderBus(
                    "Bubblegum Folder", { destinationId }, {},
                    h.graph, h.masterRoute, h.tracks);
                expect(folderId.isNotEmpty(), "folder bus is created");
                if (folderId.isEmpty())
                {
                    expect(false, "folder projection requires a valid folder bus");
                }
                else
                {

                    h.sends.createSend(h.graph, sourceId, destinationId, 0.5f);
                    bubblegum::BubblegumRoutingAdapterSourceSyncExample adapter;
                    FixedSourceSync sync { sourceId };
                    const auto records = adapter.getSendRecords(h.graph, sync, {});
                    expect(hasDestination(records, destinationId),
                           "live Send retains the original destination identity");
                    const auto destinationRecord = std::find_if(
                        records.begin(), records.end(),
                        [&destinationId](const auto& record)
                        {
                            return record.destinationTrackId == destinationId;
                        });

                    const auto ancestors = folders.getAncestorChain(destinationId);
                    expect(std::find(ancestors.begin(), ancestors.end(), folderId) != ancestors.end(),
                           "folder core exposes the destination ancestor chain");

                    std::unordered_set<TrackID, bubblegum::TrackIdHash> collapsed { folderId };
                    TrackID projected = destinationId;
                    for (int i = static_cast<int>(ancestors.size()) - 1; i >= 0; --i)
                    {
                        if (collapsed.count(ancestors[static_cast<size_t>(i)]) != 0)
                        {
                            projected = ancestors[static_cast<size_t>(i)];
                            break;
                        }
                    }

                    expectEquals(projected, folderId,
                                 "collapsed folder projects the visual destination to its folder bus");
                    expect(destinationRecord != records.end(),
                           "folder projection retains a record for the original destination");
                    if (destinationRecord != records.end())
                        expectEquals(destinationRecord->destinationTrackId, destinationId,
                                     "folder projection does not rewrite RoutingGraph identity");
                }
            }
        }

        beginTest("OffscreenFlyoutStaysOutsideMixerAndInsideWorkArea");
        {
            using Endpoint = BubblegumOffscreenEndpointComponent;
            const juce::Rectangle<float> mixer { 300.0f, 180.0f, 500.0f, 320.0f };
            const juce::Rectangle<float> anchor { 780.0f, 210.0f, 22.0f, 22.0f };
            const juce::Rectangle<float> workArea { 0.0f, 0.0f, 1200.0f, 800.0f };
            const auto placement = Endpoint::computeExternalFlyoutPlacement(
                mixer, anchor, { 180.0f, 120.0f }, workArea, 12.0f,
                Endpoint::ExternalFlyoutSide::RightOfMixer);

            expect(placement.valid, "external flyout placement is valid");
            expect(!placement.bounds.intersects(mixer),
                   "external flyout has zero Mixer overlap");
            expect(placement.bounds.getX() >= workArea.getX()
                       && placement.bounds.getY() >= workArea.getY()
                       && placement.bounds.getRight() <= workArea.getRight()
                       && placement.bounds.getBottom() <= workArea.getBottom(),
                   "external flyout remains inside the work area");
            expect(placement.side == Endpoint::ExternalFlyoutSide::RightOfMixer,
                   "right-side placement remains preferred when it fits");
        }
    }
};

static BubblegumRefreshTests bubblegumRefreshTests;

} // namespace
