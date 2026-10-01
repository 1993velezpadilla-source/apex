// ===========================================================================
// QuickWorkflowTests.cpp
// Focused Quick Track / Quick Route / Quick Send regressions.
// ===========================================================================
#include <JuceHeader.h>
#include <algorithm>
#include <set>
#include <vector>

#include "../../../Source/QuickTrackCore/QuickWorkflowPopup.h"
#include "../../../Source/RoutingCore/MasterRouteStateCore.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/Bubblegum/BubblegumSendStateCore.h"

namespace
{

using namespace DAW;

struct Harness
{
    TrackManager tracks;
    RoutingGraph graph;
    MasterRouteStateCore masterRoute { graph };
    BubblegumSendStateCore sends;
    QuickTrackColorSystem colors { 0xA9E12026 };
    QuickWorkflowCore workflow;

    Harness()
        : workflow(tracks, graph, masterRoute, sends, colors)
    {
        tracks.createMasterTrack();
    }
};

const QuickTrackRole* role(const char* id)
{
    return QuickWorkflowRoleCatalog::findById(id);
}

TrackID findCreated(const QuickTrackBatchResult& result, const char* roleId)
{
    for (const auto& created : result.created)
        if (created.role != nullptr && juce::String(created.role->roleId) == roleId)
            return created.trackId;
    return {};
}

void pressButton(juce::Button& button)
{
    juce::MessageManager::getInstance();
    button.triggerClick();
    if (auto* manager = juce::MessageManager::getInstance())
        manager->runDispatchLoopUntil(50);
}

const RoutingConnection* findEdge(const RoutingGraph& graph,
                                  const TrackID& sourceId,
                                  const TrackID& targetId,
                                  ConnectionType type)
{
    auto* source = graph.getNodeByTrackId(sourceId);
    auto* target = graph.getNodeByTrackId(targetId);
    if (source == nullptr || target == nullptr)
        return nullptr;
    for (auto* edge : graph.getAllConnections())
        if (edge != nullptr && edge->sourceNodeId == source->id
            && edge->destNodeId == target->id && edge->type == type)
            return edge;
    return nullptr;
}

int countDirectMasterEdges(const RoutingGraph& graph, const TrackID& sourceId)
{
    auto* source = graph.getNodeByTrackId(sourceId);
    if (source == nullptr)
        return 0;
    int count = 0;
    for (auto* edge : graph.getAllConnections())
        if (edge != nullptr && edge->sourceNodeId == source->id
            && edge->destNodeId == "master" && edge->type == ConnectionType::Direct)
            ++count;
    return count;
}

std::vector<QuickWorkflowRequest> allRolesWithNoCreation()
{
    std::vector<QuickWorkflowRequest> requests;
    for (const auto* item : QuickWorkflowRoleCatalog::allRoles())
        requests.push_back({ item, 0 });
    return requests;
}

class QuickWorkflowTests final : public juce::UnitTest
{
public:
    QuickWorkflowTests() : juce::UnitTest("QuickWorkflow.CoreAndPopup", "APEX.QuickWorkflow") {}

    void runTest() override
    {
        beginTest("CanonicalVocalCatalogAndNoBeatContent");
        {
            const std::vector<juce::String> expectedTracks
            {
                "Intro", "Hook / Coro", "Verso / Verse", "Adlibs", "Doubles",
                "Harmonies", "Vocal FX", "Reverb", "Delay", "Pitch",
                "Modulation", "Character", "Stereo", "Untitled"
            };
            const std::vector<juce::String> expectedBuses
            {
                "Vocal Bus", "Doubles Bus", "Adlibs Bus", "FX Bus", "Music Bus",
                "Mix Bus", "Untitled Bus"
            };
            expectEquals((int) QuickWorkflowRoleCatalog::trackRoles().size(), 14);
            expectEquals((int) QuickWorkflowRoleCatalog::busRoles().size(), 7);
            for (size_t i = 0; i < expectedTracks.size(); ++i)
                expectEquals(juce::String(QuickWorkflowRoleCatalog::trackRoles()[i].displayName), expectedTracks[i]);
            for (size_t i = 0; i < expectedBuses.size(); ++i)
                expectEquals(juce::String(QuickWorkflowRoleCatalog::busRoles()[i].displayName), expectedBuses[i]);

            const juce::String forbidden[] =
            {
                "Drums", "Percs", "Kick", "Snare", "Hi-Hat", "808", "Sampler",
                "Pattern", "Sequencer", "Drum Bus", "Beat Bus", "Instrumental / Beat"
            };
            for (const auto* item : QuickWorkflowRoleCatalog::allRoles())
                for (const auto& name : forbidden)
                    expect(juce::String(item->displayName) != name, "beat-making role absent: " + name);
        }

        beginTest("UntitledAndMasterCreationPolicy");
        {
            Harness h;
            expect(role("untitled") != nullptr, "Untitled track is available");
            expect(role("untitled_bus") != nullptr, "Untitled Bus is available");
            expect(QuickWorkflowRoleCatalog::findById("master") == nullptr,
                   "Master is not a creatable workflow role");
        }

        beginTest("QuickTrackPopupExposesCanonicalBusPresets");
        {
            Harness h;
            QuickWorkflowPopup popup(h.workflow);
            popup.setVisible(true);
            const std::vector<juce::String> expectedBuses
            {
                "Vocal Bus", "Doubles Bus", "Adlibs Bus", "FX Bus", "Music Bus",
                "Mix Bus", "Untitled Bus"
            };
            expectEquals(popup.getRoleList().getNumCards(), 21,
                         "Quick Track exposes tracks plus buses");
            for (size_t i = 0; i < expectedBuses.size(); ++i)
            {
                const auto* busRole = QuickWorkflowRoleCatalog::busRoles().data() + i;
                expectEquals(juce::String(busRole->displayName), expectedBuses[i]);
                expect(popup.getRoleList().getCardForRoleId(busRole->roleId) != nullptr,
                       "canonical bus card is visible");
            }
            expect(popup.getRoleList().getCardForRoleId("master") == nullptr,
                   "Master is not a creatable Quick Track card");
        }

        beginTest("QuickTrackBusCardsKeepPendingAndCreateOnlySemantics");
        {
            Harness h;
            QuickWorkflowPopup popup(h.workflow);
            popup.setVisible(true);
            auto* doublesBus = popup.getRoleList().getCardForRoleId("doubles_bus");
            auto* vocalBus = popup.getRoleList().getCardForRoleId("vocal_bus");
            expect(doublesBus != nullptr && vocalBus != nullptr);
            if (doublesBus == nullptr || vocalBus == nullptr)
                return;

            doublesBus->setCount(1);
            vocalBus->setCount(1);
            pressButton(doublesBus->getCreateButton());
            expectEquals(h.tracks.getNumTracks(), 1, "individual bus Create affects one row");
            expectEquals(doublesBus->getCount(), 0, "created bus row is consumed");
            expectEquals(vocalBus->getCount(), 1, "other bus row remains pending");

            pressButton(popup.getFooterActionButton());
            expectEquals(h.tracks.getNumTracks(), 2, "global Create includes the pending bus");
            expectEquals(vocalBus->getCount(), 0, "global Create consumes pending bus count");
        }

        beginTest("QuantityBoundsAndTrashOnlyResetPendingCount");
        {
            Harness h;
            QuickWorkflowPopup popup(h.workflow);
            popup.setVisible(true);
            auto* card = popup.getRoleList().getCardForRoleId("doubles");
            expect(card != nullptr);
            if (card == nullptr)
                return;
            for (int i = 0; i < 120; ++i)
                pressButton(card->getPlusButton());
            expectEquals(card->getCount(), 99, "plus is bounded");
            for (int i = 0; i < 120; ++i)
                pressButton(card->getMinusButton());
            expectEquals(card->getCount(), 0, "minus is bounded");
            card->setCount(4);
            pressButton(card->getTrashButton());
            expectEquals(card->getCount(), 0, "trash resets pending count");
            expectEquals(h.tracks.getNumTracks(), 0, "trash never deletes project tracks");
        }

        beginTest("IndividualCreateConsumesOnlySelectedRow");
        {
            Harness h;
            QuickWorkflowPopup popup(h.workflow);
            popup.setVisible(true);
            auto* doubles = popup.getRoleList().getCardForRoleId("doubles");
            auto* intro = popup.getRoleList().getCardForRoleId("intro");
            expect(doubles != nullptr && intro != nullptr);
            if (doubles == nullptr || intro == nullptr)
                return;
            doubles->setCount(4);
            intro->setCount(1);
            pressButton(doubles->getCreateButton());
            expectEquals(h.tracks.getNumTracks(), 4, "only Doubles were created");
            expectEquals(doubles->getCount(), 0, "Doubles pending count consumed");
            expectEquals(intro->getCount(), 1, "Intro remains pending");
        }

        beginTest("GlobalCreateConsumesAllQueuedRows");
        {
            Harness h;
            QuickWorkflowPopup popup(h.workflow);
            popup.setVisible(true);
            auto* intro = popup.getRoleList().getCardForRoleId("intro");
            auto* adlibs = popup.getRoleList().getCardForRoleId("adlibs");
            expect(intro != nullptr && adlibs != nullptr);
            if (intro == nullptr || adlibs == nullptr)
                return;
            intro->setCount(1);
            adlibs->setCount(2);
            pressButton(popup.getFooterActionButton());
            expectEquals(h.tracks.getNumTracks(), 3, "global create consumes all queued rows");
            expectEquals(intro->getCount(), 0);
            expectEquals(adlibs->getCount(), 0);
        }

        beginTest("QuickTrackIsCreateOnlyForExistingMatches");
        {
            Harness h;
            const auto* doubles = role("doubles");
            const auto* doublesBus = role("doubles_bus");
            auto tracks = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doubles, 4 } });
            auto buses = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doublesBus, 1 } });
            expect(tracks.ok() && buses.ok());
            expectEquals((int) tracks.routePairs.size(), 0, "Quick Track has no auto routes");
            for (const auto& created : tracks.created)
            {
                expect(findEdge(h.graph, created.trackId, buses.created.front().trackId,
                                ConnectionType::Send) == nullptr,
                       "existing matching tracks remain unrouted");
                expect(findEdge(h.graph, created.trackId, "master", ConnectionType::Direct) == nullptr
                       || findEdge(h.graph, created.trackId, "master", ConnectionType::Direct)->active.load(),
                       "direct Master remains active");
            }
        }

        beginTest("QuickTrackReusesNamedCanonicalBusesWithoutRouting");
        {
            Harness h;
            const auto* doubles = role("doubles");
            const auto* doublesBus = role("doubles_bus");
            auto tracks = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doubles, 4 } });
            auto firstBus = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doublesBus, 1 } });
            const auto before = h.tracks.getNumTracks();
            auto secondBus = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doublesBus, 1 } });
            expect(tracks.ok() && firstBus.ok() && secondBus.ok());
            expectEquals(h.tracks.getNumTracks(), before, "named bus is reused");
            expectEquals((int) secondBus.created.size(), 0, "no Doubles Bus 2 is created");
            for (const auto& created : tracks.created)
                expect(findEdge(h.graph, created.trackId, firstBus.created.front().trackId,
                                ConnectionType::Send) == nullptr,
                       "Quick Track reuse does not auto-route existing tracks");

            for (const char* busId : { "vocal_bus", "adlibs_bus", "fx_bus",
                                       "music_bus", "mix_bus" })
            {
                const auto* bus = role(busId);
                auto first = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { bus, 1 } });
                const auto count = h.tracks.getNumTracks();
                auto second = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { bus, 1 } });
                expect(first.ok() && second.ok(), "named bus ensure operations succeed");
                expectEquals(h.tracks.getNumTracks(), count, "canonical bus remains unique");
                expectEquals((int) second.created.size(), 0, "canonical duplicate is not created");
            }

            auto generic = h.workflow.apply(QuickWorkflowTab::QuickTrack,
                                             { { role("untitled_bus"), 2 } });
            expect(generic.ok());
            expectEquals((int) generic.created.size(), 2, "Untitled Bus remains generic");
        }

        beginTest("QuickRouteRoutesExistingMatchesAndReusesCanonicalBus");
        {
            Harness h;
            const auto* doubles = role("doubles");
            const auto* doublesBus = role("doubles_bus");
            auto trackResult = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doubles, 2 } });
            auto busResult = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doublesBus, 1 } });
            const auto beforeCount = h.tracks.getNumTracks();
            auto routeResult = h.workflow.apply(QuickWorkflowTab::QuickRoute, { { doubles, 0 }, { doublesBus, 1 } });
            expect(routeResult.ok());
            expectEquals(h.tracks.getNumTracks(), beforeCount, "canonical bus is reused");
            for (const auto& created : trackResult.created)
            {
                expect(findEdge(h.graph, created.trackId, busResult.created.front().trackId,
                                ConnectionType::Send) != nullptr,
                       "Quick Route routes an existing matching track");
                auto* master = findEdge(h.graph, created.trackId, "master", ConnectionType::Direct);
                expect(master == nullptr || !master->active.load(), "no duplicate active Master path");
            }
        }

        beginTest("QuickRouteBusRequestRoutesMatchingTracks");
        {
            Harness h;
            const auto* doubles = role("doubles");
            const auto* doublesBus = role("doubles_bus");
            auto trackResult = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doubles, 4 } });
            auto busResult = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doublesBus, 1 } });
            const auto before = h.tracks.getNumTracks();
            auto routeResult = h.workflow.apply(QuickWorkflowTab::QuickRoute, { { doublesBus, 1 } });
            expect(trackResult.ok() && busResult.ok() && routeResult.ok());
            expectEquals(h.tracks.getNumTracks(), before, "Quick Route reuses Doubles Bus");
            for (const auto& created : trackResult.created)
                expect(findEdge(h.graph, created.trackId, busResult.created.front().trackId,
                                ConnectionType::Send) != nullptr,
                       "Quick Route bus request routes matching Doubles");
        }

        beginTest("CanonicalVocalRoutingAndBusHierarchy");
        {
            Harness h;
            auto creation = h.workflow.apply(QuickWorkflowTab::QuickTrack,
                                             { { role("intro"), 1 }, { role("hook_coro"), 1 },
                                               { role("verse"), 1 }, { role("adlibs"), 1 },
                                               { role("doubles"), 1 }, { role("harmonies"), 1 },
                                               { role("vocal_fx"), 1 }, { role("vocal_bus"), 1 },
                                               { role("doubles_bus"), 1 }, { role("adlibs_bus"), 1 },
                                               { role("fx_bus"), 1 }, { role("music_bus"), 1 },
                                               { role("mix_bus"), 1 } });
            expect(creation.ok());
            auto routed = h.workflow.apply(QuickWorkflowTab::QuickRoute, allRolesWithNoCreation());
            expect(routed.ok());

            const auto vocalBus = findCreated(creation, "vocal_bus");
            const auto doublesBus = findCreated(creation, "doubles_bus");
            const auto adlibsBus = findCreated(creation, "adlibs_bus");
            const auto fxBus = findCreated(creation, "fx_bus");
            const auto mixBus = findCreated(creation, "mix_bus");
            expect(vocalBus.isNotEmpty() && doublesBus.isNotEmpty() && adlibsBus.isNotEmpty()
                   && fxBus.isNotEmpty() && mixBus.isNotEmpty());

            for (const auto* trackRole : { role("intro"), role("hook_coro"), role("verse"), role("harmonies") })
                expect(findEdge(h.graph, findCreated(creation, trackRole->roleId), vocalBus,
                                ConnectionType::Send) != nullptr,
                       juce::String(trackRole->displayName) + " routes to Vocal Bus");
            expect(findEdge(h.graph, findCreated(creation, "vocal_fx"), fxBus, ConnectionType::Send) != nullptr,
                   "Vocal FX routes to FX Bus");
            expect(findEdge(h.graph, findCreated(creation, "doubles"), doublesBus, ConnectionType::Send) != nullptr,
                   "Doubles routes to Doubles Bus");
            expect(findEdge(h.graph, findCreated(creation, "adlibs"), adlibsBus, ConnectionType::Send) != nullptr,
                   "Adlibs routes to Adlibs Bus");
            expect(findEdge(h.graph, doublesBus, vocalBus, ConnectionType::Send) != nullptr,
                   "Doubles Bus preserves Vocal hierarchy");
            expect(findEdge(h.graph, adlibsBus, vocalBus, ConnectionType::Send) != nullptr,
                   "Adlibs Bus preserves Vocal hierarchy");
            expect(findEdge(h.graph, vocalBus, mixBus, ConnectionType::Send) != nullptr,
                   "Vocal Bus routes to Mix Bus");
            expect(findEdge(h.graph, fxBus, mixBus, ConnectionType::Send) != nullptr,
                   "FX Bus routes to Mix Bus");
            expect(findEdge(h.graph, mixBus, "master", ConnectionType::Direct) != nullptr,
                   "Mix Bus has the canonical Master path");
        }

        beginTest("UntitledHasNoForcedRoute");
        {
            Harness h;
            const auto* untitled = role("untitled");
            const auto* vocalBus = role("vocal_bus");
            auto result = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { untitled, 1 }, { vocalBus, 1 } });
            auto routed = h.workflow.apply(QuickWorkflowTab::QuickRoute, { { untitled, 0 } });
            expect(routed.ok());
            expectEquals((int) routed.routePairs.size(), 0, "Untitled has no forced route");
            expect(findEdge(h.graph, result.created[0].trackId, result.created[1].trackId,
                            ConnectionType::Send) == nullptr);
        }

        beginTest("AutoAndExplicitColorsDoNotRecolorExistingTracks");
        {
            Harness h;
            const auto* doubles = role("doubles");
            auto first = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doubles, 1 } });
            const auto oldColor = h.tracks.getTrack(first.created.front().trackId)->getColor();
            const juce::Colour explicitColor(0xFFFF69B4);
            h.workflow.setManualRoleColor(*doubles, explicitColor);
            auto second = h.workflow.apply(QuickWorkflowTab::QuickTrack, { { doubles, 1 } });
            expect(h.tracks.getTrack(first.created.front().trackId)->getColor() == oldColor,
                   "explicit override does not mutate an existing track");
            expect(h.tracks.getTrack(second.created.front().trackId)->getColor() == explicitColor,
                   "explicit override applies to new instances");
        }

        beginTest("QuickSendUsesRoutingGraphAndHandlesLevelPrePost");
        {
            Harness h;
            auto created = h.workflow.apply(QuickWorkflowTab::QuickTrack,
                                            { { role("intro"), 1 }, { role("vocal_bus"), 1 } });
            const auto source = findCreated(created, "intro");
            const auto target = findCreated(created, "vocal_bus");
            const auto destinations = h.workflow.getSendDestinations(source);
            expect(std::any_of(destinations.begin(), destinations.end(),
                               [&target](const auto& d) { return d.trackId == target; }),
                   "actual bus appears as Quick Send destination");
            expect(!std::any_of(destinations.begin(), destinations.end(),
                                [](const auto& d) { return d.name == "Master"; }),
                   "Master is not a Quick Send bus destination");

            expect(h.workflow.addSend(source, target, 0.65f, false));
            auto* post = findEdge(h.graph, source, target, ConnectionType::Send);
            expect(post != nullptr && std::fabs(post->gain.load(std::memory_order_relaxed) - 0.65f) < 0.001f,
                   "post-fader send level uses the graph connection");
            expect(h.workflow.setSendPreFader(source, target, true));
            auto* pre = findEdge(h.graph, source, target, ConnectionType::PreSend);
            expect(pre != nullptr && std::fabs(pre->gain.load(std::memory_order_relaxed) - 0.65f) < 0.001f,
                   "PreSend state preserves the canonical send level");
            expect(h.sends.sendExists(h.graph, source, target), "PreSend remains a send-family connection");
            expect(h.workflow.setSendPreFader(source, target, false));
            expect(findEdge(h.graph, source, target, ConnectionType::Send) != nullptr,
                   "post-fader state restores ConnectionType::Send");
        }

        beginTest("QuickSendUsesTheInstalledMutationAuthority");
        {
            Harness h;
            auto created = h.workflow.apply(QuickWorkflowTab::QuickTrack,
                                            { { role("intro"), 1 }, { role("vocal_bus"), 1 } });
            const auto source = findCreated(created, "intro");
            const auto target = findCreated(created, "vocal_bus");
            QuickWorkflowPopup popup(h.workflow, source);
            popup.setVisible(true);
            popup.showTab(QuickWorkflowTab::QuickSend);

            bool authorityCalled = false;
            popup.onSendMutation = [&authorityCalled](std::function<bool()> mutation,
                                                       juce::String)
            {
                authorityCalled = true;
                return mutation ? mutation() : false;
            };
            auto* card = popup.getSendList().getCardForTarget(target);
            expect(card != nullptr, "send card exists for the actual bus");
            if (card != nullptr)
                pressButton(card->getAddButton());
            expect(authorityCalled, "Quick Send uses the installed mutation authority");
            expect(h.sends.sendExists(h.graph, source, target), "Quick Send mutation reaches the graph");
        }

        beginTest("PopupTabsAndDeferredRefreshAreSafeToClose");
        {
            Harness h;
            auto created = h.workflow.apply(QuickWorkflowTab::QuickTrack,
                                            { { role("intro"), 1 }, { role("vocal_bus"), 1 } });
            const auto source = findCreated(created, "intro");
            const auto target = findCreated(created, "vocal_bus");
            auto popup = std::make_unique<QuickWorkflowPopup>(h.workflow, source);
            popup->setVisible(true);
            popup->showTab(QuickWorkflowTab::QuickRoute);
            expect(popup->getTab() == QuickWorkflowTab::QuickRoute, "Route tab opens in shared shell");
            popup->showTab(QuickWorkflowTab::QuickSend);
            expect(popup->getTab() == QuickWorkflowTab::QuickSend, "Send tab opens in shared shell");
            auto* sendCard = popup->getSendList().getCardForTarget(target);
            expect(sendCard != nullptr, "send row exists for actual bus");
            if (sendCard != nullptr)
            {
                pressButton(sendCard->getAddButton());
                popup.reset();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            }
            expect(true, "closing while a deferred send refresh is pending is safe");
        }
    }
};

static QuickWorkflowTests quickWorkflowTests;

} // namespace
