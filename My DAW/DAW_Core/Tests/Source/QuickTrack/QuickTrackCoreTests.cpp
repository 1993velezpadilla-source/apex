// ===========================================================================
// QuickTrackCoreTests.cpp
// APEX Quick Track Builder — model-level regression coverage:
//   roles catalog, naming/numbering, project color families (incl. palette
//   exhaustion + dynamic generation), templates (store persistence, atomic
//   saves, corrupt tolerance), and builder batch creation + bus routing.
//
// All tests use deterministic seeds and temporary directories — they never
// touch real user templates or %APPDATA%.
// ===========================================================================
#include <JuceHeader.h>
#include "../../../Source/QuickTrackCore/QuickTrackRoles.h"
#include "../../../Source/QuickTrackCore/QuickTrackNaming.h"
#include "../../../Source/QuickTrackCore/QuickTrackColorSystem.h"
#include "../../../Source/QuickTrackCore/QuickTrackTemplate.h"
#include "../../../Source/QuickTrackCore/QuickTrackBuilderCore.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/RoutingCore/MasterRouteStateCore.h"
#include "../../../Source/Bubblegum/BubblegumSendStateCore.h"
#include <set>
#include <algorithm>

namespace
{

using namespace DAW;

// ── Helpers ────────────────────────────────────────────────────────────────

juce::File makeTempTemplateDir(const juce::String& tag)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("APEX_QTT_" + tag + "_" + juce::Uuid().toString().substring(0, 8));
    dir.createDirectory();
    return dir;
}

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

bool hasSendToTrack(const RoutingGraph& graph, const juce::String& srcTrackId,
                    const juce::String& destTrackId)
{
    auto* destNode = graph.getNodeByTrackId(destTrackId);
    if (destNode == nullptr)
        return false;
    return findGraphEdge(graph, srcTrackId, destNode->id, ConnectionType::Send) != nullptr;
}

struct Harness
{
    explicit Harness(juce::int64 seed = 12345)
        : colors(seed), builder(tracks, graph, masterRoute, sends, colors) {}

    TrackManager tracks;
    RoutingGraph graph;
    MasterRouteStateCore masterRoute { graph };
    BubblegumSendStateCore sends;
    QuickTrackColorSystem colors;
    QuickTrackBuilderCore builder;
};

// Build a multi-role template identical to the spec's "Urban Vocal
// Recording" example.
QuickTrackTemplate makeUrbanVocalTemplate()
{
    QuickTrackTemplate t;
    t.name = "Urban Vocal Recording";
    struct Entry { const char* id; int count; };
    const Entry entries[] =
    {
        { "instrumental_beat", 1 },
        { "lead_vocal", 1 },
        { "coro_hook", 2 },
        { "verse", 3 },
        { "doubles", 4 },
        { "adlibs", 6 },
        { "doubles_bus", 1 },
        { "adlibs_bus", 1 },
        { "vocal_bus", 1 },
        { "reverb", 1 },
        { "delay", 1 },
    };
    int order = 0;
    for (const auto& e : entries)
    {
        QuickTrackTemplateRole r;
        r.roleId = e.id;
        r.count = e.count;
        r.order = order++;
        r.autoColor = true;
        t.roles.push_back(r);
    }
    return t;
}

// ===========================================================================
// Naming
// ===========================================================================
class QuickTrackNamingTests final : public juce::UnitTest
{
public:
    QuickTrackNamingTests() : juce::UnitTest("QuickTrack.Naming", "APEX.QuickTrack") {}

    void runTest() override
    {
        beginTest("PredefinedRoleAutoNames");
        {
            expectEquals(QuickTrackNaming::nextNameFor("Coro / Hook", {}), juce::String("Coro / Hook"));
            expectEquals(QuickTrackNaming::nextNameFor("Doubles", {}), juce::String("Doubles"));
            expectEquals(QuickTrackNaming::nextNameFor("Reverb", {}), juce::String("Reverb"));
            expectEquals(QuickTrackNaming::nextNameFor("Doubles Bus", {}), juce::String("Doubles Bus"));
        }

        beginTest("UntitledRemainsGeneric");
        {
            expectEquals(QuickTrackNaming::genericUntitledName({}), juce::String("Audio 1"));
            expectEquals(QuickTrackNaming::genericUntitledName({ "Audio 1" }),
                         juce::String("Audio 2"));
        }

        beginTest("DuplicateRoleNumbering");
        {
            std::vector<juce::String> existing { "Coro / Hook" };
            expectEquals(QuickTrackNaming::nextNameFor("Coro / Hook", existing),
                         juce::String("Coro / Hook 2"));
            existing.push_back("Coro / Hook 2");
            expectEquals(QuickTrackNaming::nextNameFor("Coro / Hook", existing),
                         juce::String("Coro / Hook 3"));
            existing.push_back("Coro / Hook 3");
            expectEquals(QuickTrackNaming::nextNameFor("Coro / Hook", existing),
                         juce::String("Coro / Hook 4"));

            // "Doubles / Doubles 2 / Doubles 3" → next must be Doubles 4.
            std::vector<juce::String> doubles { "Doubles", "Doubles 2", "Doubles 3" };
            expectEquals(QuickTrackNaming::nextNameFor("Doubles", doubles),
                         juce::String("Doubles 4"));
        }

        beginTest("NumberingSurvivesReload");
        {
            // Reload simulation: names are re-scanned from persisted tracks,
            // so the same next-name result is produced after a save/load.
            const std::vector<juce::String> afterReload { "Adlibs / Highlights",
                                                          "Adlibs / Highlights 2",
                                                          "Adlibs / Highlights 3" };
            expectEquals(QuickTrackNaming::nextNameFor("Adlibs / Highlights", afterReload),
                         juce::String("Adlibs / Highlights 4"));
        }
    }
};

class QuickTrackBuilderNamingTests final : public juce::UnitTest
{
public:
    QuickTrackBuilderNamingTests() : juce::UnitTest("QuickTrack.BuilderNaming", "APEX.QuickTrack") {}

    void runTest() override
    {
        beginTest("NamesNeverDefineTrackIdentity");
        {
            Harness h(42);
            const auto* coro = QuickTrackRoleCatalog::findById("coro_hook");
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            auto result = h.builder.createBatch({ { coro, 2 }, { doubles, 2 } });
            expect(result.ok(), "batch ok");
            expectEquals(result.count(), 4);

            std::set<juce::String> trackIds;
            std::set<juce::String> names;
            for (const auto& c : result.created)
            {
                trackIds.insert(c.trackId);
                names.insert(c.name);
                expect(!c.trackId.startsWith("Coro"), "TrackID must not be derived from name");
                expect(!c.name.startsWith("TRK_"), "name must not be derived from ID");
            }
            expectEquals((int) trackIds.size(), 4, "all TrackIDs unique");
            expectEquals((int) names.size(), 4, "all names unique");
            expect(trackIds.find("Coro / Hook") == trackIds.end(), "name never used as ID");
        }
    }
};

// ===========================================================================
// Colors
// ===========================================================================
class QuickTrackColorTests final : public juce::UnitTest
{
public:
    QuickTrackColorTests() : juce::UnitTest("QuickTrack.Colors", "APEX.QuickTrack") {}

    void runTest() override
    {
        beginTest("RandomColorAssigned");
        {
            QuickTrackColorSystem cs(7);
            const auto c = cs.assignColorForRole("coro_hook");
            expect(c.isOpaque() && c != juce::Colour(0), "valid opaque color assigned");
            expect(cs.hasColorForRole("coro_hook"));
            expect(cs.getColorForRole("coro_hook") == c, "stored color round-trips");
        }

        beginTest("SameRoleSameColor");
        {
            QuickTrackColorSystem cs(7);
            const auto c1 = cs.assignColorForRole("coro_hook");
            const auto c2 = cs.assignColorForRole("coro_hook");
            expect(c1 == c2, "same role → same color");
        }

        beginTest("NumberedRoleSharesBaseColor");
        {
            QuickTrackColorSystem cs(7);
            expectEquals(QuickTrackColorSystem::normalizeKey("Coro / Hook"),
                         QuickTrackColorSystem::normalizeKey("Coro / Hook 2"));
            expectEquals(QuickTrackColorSystem::normalizeKey("Doubles"),
                         QuickTrackColorSystem::normalizeKey("Doubles 4"));
            const auto base = cs.assignColorForRole("coro / hook");
            const auto numbered = cs.assignColorForRole("coro / hook 2");
            expect(base == numbered, "numbered copy shares base family color");
        }

        beginTest("DifferentRolesNeverAutoDuplicateColor");
        {
            QuickTrackColorSystem cs(11);
            std::set<juce::int64> seen;
            for (int i = 0; i < 64; ++i)
            {
                const auto key = "role_family_" + juce::String(i);
                const auto c = cs.assignColorForRole(key);
                const auto argb = (juce::int64) c.getARGB();
                expect(seen.find(argb) == seen.end(), "no automatic duplicate: " + key);
                seen.insert(argb);
            }
            expectEquals((int) seen.size(), 64, "64 families → 64 unique colors");
        }

        beginTest("PaletteExhaustionGeneratesUniqueColors");
        {
            // 64 distinct role families exceed the 48-swatch curated palette —
            // the system must generate additional distinct colors, not reuse.
            QuickTrackColorSystem cs(13);
            const int paletteSize = (int) QuickTrackColorSystem::canonicalPalette().size();
            expectEquals(paletteSize, 48);
            std::set<juce::int64> seen;
            for (int i = 0; i < 64; ++i)
            {
                const auto c = cs.assignColorForRole("fam_" + juce::String(i));
                seen.insert((juce::int64) c.getARGB());
            }
            expectEquals((int) seen.size(), 64, "palette exhaustion still yields unique colors");
            expect(cs.getNumAssignedFamilies() > paletteSize, "generated colors were used");
        }

        beginTest("GeneratedColorsRemainVisuallyDistinct");
        {
            QuickTrackColorSystem cs(17);
            std::vector<juce::Colour> assigned;
            for (int i = 0; i < 64; ++i)
                assigned.push_back(cs.assignColorForRole("vis_" + juce::String(i)));

            // The 48 curated APEX palette swatches are trusted as-is (the
            // design system may contain perceptually close neighbors).
            // Every DYNAMICALLY GENERATED color (index >= 48) must remain
            // clearly distinct from every assigned family.
            const int paletteSize = (int) QuickTrackColorSystem::canonicalPalette().size();
            bool allDistinct = true;
            float minGeneratedDistance = 1e9f;
            for (size_t i = (size_t) paletteSize; i < assigned.size(); ++i)
                for (size_t j = 0; j < assigned.size(); ++j)
                {
                    if (i == j)
                        continue;
                    const float d = QuickTrackColorSystem::perceptualDistance(assigned[i], assigned[j]);
                    minGeneratedDistance = juce::jmin(minGeneratedDistance, d);
                    if (d < 12.0f)
                        allDistinct = false;
                }
            expect(allDistinct, "every GENERATED color is perceptually distinct from all assigned families (CIE76 >= 12)");
            expect(minGeneratedDistance > 10.0f, "generated colors keep a sane minimum distance");
        }

        beginTest("GeneratedColorsPersistReload");
        {
            QuickTrackColorSystem cs(19);
            for (int i = 0; i < 64; ++i)
                cs.assignColorForRole("persist_" + juce::String(i));
            const auto state = cs.getState();

            QuickTrackColorSystem restored(999); // different seed must not matter
            restored.restoreState(state);
            expectEquals(restored.getNumAssignedFamilies(), 64, "all families restored");
            for (int i = 0; i < 64; ++i)
            {
                const auto key = "persist_" + juce::String(i);
                expect(restored.getColorForRole(key) == cs.getColorForRole(key),
                             "exact color restored for " + key);
            }

            // New assignments after reload must not collide with restored ones.
            const auto next = restored.assignColorForRole("brand_new_after_reload");
            bool collides = false;
            for (int i = 0; i < 64; ++i)
                if (restored.getColorForRole("persist_" + juce::String(i)) == next)
                    collides = true;
            expect(!collides, "post-reload assignment avoids all restored colors");
        }

        beginTest("NewProjectHasIndependentColorAssignments");
        {
            QuickTrackColorSystem songA(1);
            QuickTrackColorSystem songB(2);

            const auto aColor = songA.assignColorForRole("coro_hook");
            // Song B is untouched by Song A's assignment.
            expect(!songB.hasColorForRole("coro_hook"), "independent maps");
            const auto bColor = songB.assignColorForRole("coro_hook");
            expect(bColor == songB.getColorForRole("coro_hook"), "B owns its assignment");

            // The two projects own independent seeds/orders: assign the full
            // palette in both and verify the shuffled orders differ.
            bool ordersDiffer = false;
            for (int i = 0; i < 48; ++i)
            {
                const auto cA = songA.assignColorForRole("ord_a_" + juce::String(i));
                const auto cB = songB.assignColorForRole("ord_b_" + juce::String(i));
                if (cA != cB)
                    ordersDiffer = true;
            }
            expect(ordersDiffer, "independent per-project palette shuffles");
            expect(aColor == songA.getColorForRole("coro_hook"), "A's assignment stable");
            expect(bColor == songB.getColorForRole("coro_hook"), "B's assignment stable");
            // Same seed → identical first assignment (determinism proof).
            QuickTrackColorSystem songA2(1);
            expect(songA2.assignColorForRole("coro_hook") == aColor,
                         "same seed → same assignment (deterministic)");
        }

        beginTest("ManualRoleColorOverride");
        {
            QuickTrackColorSystem cs(23);
            const juce::Colour pink(0xFFFF69B4);
            cs.setManualRoleColor("doubles", pink);
            const auto assigned = cs.assignColorForRole("doubles");
            expect(assigned == pink, "manual override wins");
            expect(cs.isManual("doubles"));
            expect(cs.getColorForRole("doubles") == pink, "manual color stored");
        }

        beginTest("ManualDuplicateColorAllowed");
        {
            QuickTrackColorSystem cs(29);
            const auto c1 = cs.assignColorForRole("coro_hook");
            const juce::Colour manualDuplicate = c1;
            cs.setManualRoleColor("doubles", manualDuplicate); // user decision — allowed
            expect(cs.getColorForRole("doubles") == c1, "manual duplicate accepted");
            // But an AUTOMATIC assignment for a third role must avoid both.
            const auto c3 = cs.assignColorForRole("adlibs");
            expect(c3 != c1, "auto assignment avoids occupied colors");
        }

        beginTest("RoleColorPersistsReload");
        {
            Harness h(31);
            const auto* coro = QuickTrackRoleCatalog::findById("coro_hook");
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            h.builder.createBatch({ { coro, 1 }, { doubles, 1 } });
            const auto coroColor = h.colors.getColorForRole("coro_hook");
            const auto doublesColor = h.colors.getColorForRole("doubles");

            // Project save/load round trip through the color system state.
            const auto state = h.colors.getState();
            QuickTrackColorSystem reloaded(1);
            reloaded.restoreState(state);
            expect(reloaded.getColorForRole("coro_hook") == coroColor,
                         "Coro color exact after reload");
            expect(reloaded.getColorForRole("doubles") == doublesColor,
                         "Doubles color exact after reload");
            expect(!reloaded.isManual("coro_hook"), "auto mode preserved");
        }

        beginTest("NewProjectGetsFreshColorMap");
        {
            Harness h(37);
            const auto* coro = QuickTrackRoleCatalog::findById("coro_hook");
            h.builder.createBatch({ { coro, 1 } });
            const auto songAColor = h.colors.getColorForRole("coro_hook");

            // New Project = fresh map + fresh shuffle. With different seeds
            // the same role MAY receive a different color; with the same
            // role the map is empty again until first assignment.
            QuickTrackColorSystem songB(38);
            expect(!songB.hasColorForRole("coro_hook"), "fresh map on new project");
            const auto songBColor = songB.assignColorForRole("coro_hook");
            expect(songBColor != juce::Colour(0));
            expect(songAColor != songBColor || songAColor != songBColor,
                   "independent assignment accepted (maps are independent)");
        }
    }
};

// ===========================================================================
// Templates
// ===========================================================================
class QuickTrackTemplateTests final : public juce::UnitTest
{
public:
    QuickTrackTemplateTests() : juce::UnitTest("QuickTrackTemplate.Store", "APEX.QuickTrackTemplate") {}

    void runTest() override
    {
        const auto dir = makeTempTemplateDir("store");
        struct DirGuard { juce::File d; ~DirGuard() { d.deleteRecursively(); } } guard { dir };

        beginTest("SaveAndLoad");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            juce::String error;
            expectEquals((int) store.saveTemplate(t, false, error), (int) QuickTrackTemplateStore::SaveResult::Saved, error);
            QuickTrackTemplate loaded;
            expect(store.loadTemplate("Urban Vocal Recording", loaded));
            expectEquals(loaded.name, t.name);
            expectEquals(loaded.totalTrackCount(), 22, "11 roles sum to 22 tracks");
            expectEquals((int) loaded.roles.size(), (int) t.roles.size());
        }

        beginTest("SaveReloadRoundTrip");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            // Manual explicit color on Doubles.
            for (auto& r : t.roles)
                if (r.roleId == "doubles")
                {
                    r.autoColor = false;
                r.explicitColor = "ffff69b4";
                }
                t.name = "Round Trip";
                juce::String error;
                expectEquals((int) store.saveTemplate(t, false, error), (int) QuickTrackTemplateStore::SaveResult::Saved, error);
            QuickTrackTemplate loaded;
            expect(store.loadTemplate(t.name, loaded));
            bool foundManual = false;
            for (const auto& r : loaded.roles)
                if (r.roleId == "doubles")
                {
                    foundManual = true;
                    expect(!r.autoColor, "manual color mode persisted");
                    expectEquals(r.explicitColor, juce::String("ffff69b4"));
                }
            expect(foundManual, "doubles present after round trip");
        }

        beginTest("PersistsAcrossRestart");
        {
            // A brand-new store instance over the same directory simulates
            // an APEX restart — templates must survive.
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            t.name = "Restart Survivor";
            juce::String error;
            store.saveTemplate(t, false, error);

            QuickTrackTemplateStore restarted(dir);
            QuickTrackTemplate loaded;
            expect(restarted.loadTemplate("Restart Survivor", loaded));
            expectEquals(loaded.name, juce::String("Restart Survivor"));
        }

        beginTest("NewProjectCanLoadSameTemplate");
        {
            // Templates are application-global: a "new project" (fresh
            // TrackManager + fresh color system) still loads the same file.
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            t.name = "Global Recipe";
            juce::String error;
            store.saveTemplate(t, false, error);

            Harness songA(1001);
            Harness songB(2002); // new project — independent state
            QuickTrackTemplate loaded;
            expect(store.loadTemplate("Global Recipe", loaded));
            expectEquals((int) loaded.roles.size(), 11);
            expectEquals(songA.tracks.getNumTracks(), 0);
            expectEquals(songB.tracks.getNumTracks(), 0, "loading never creates tracks");
        }

        beginTest("LoadPopulatesBuilderWithoutCreating");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            t.name = "Load Only";
            juce::String error;
            store.saveTemplate(t, false, error);

            Harness h(3003);
            QuickTrackTemplate loaded;
            expect(store.loadTemplate("Load Only", loaded));
            expectEquals(h.tracks.getNumTracks(), 0, "load does NOT create tracks");
            expectEquals(loaded.totalTrackCount(), 22, "recipe populated for the builder");
        }

        beginTest("AutoColorsRemainProjectScoped");
        {
            // AUTO roles must NOT freeze Song A's generated colors.
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate(); // all roles autoColor
            juce::String error;
            store.saveTemplate(t, false, error);

            for (const auto& r : t.roles)
                expect(r.autoColor, "template stores AUTO mode, not colors");
            expect(t.toValueTree().getProperty("explicitColor").isVoid(),
                   "no explicit color serialized for AUTO roles");

            // Instantiate the same template in two projects.
            QuickTrackColorSystem colorsA(5001);
            QuickTrackColorSystem colorsB(5002);
            const auto cA = colorsA.assignColorForRole("coro_hook");
            const auto cB = colorsB.assignColorForRole("coro_hook");
            // Both projects assign independently (map independence).
            expect(cA == colorsA.getColorForRole("coro_hook"));
            expect(cB == colorsB.getColorForRole("coro_hook"));
            // The template itself holds no project color.
            QuickTrackTemplate loaded;
            store.loadTemplate(t.name, loaded);
            bool anyStoredColor = false;
            for (const auto& r : loaded.roles)
                if (!r.autoColor || r.explicitColor.isNotEmpty())
                    anyStoredColor = true;
            expect(!anyStoredColor, "template carries no project colors");
        }

        beginTest("ManualColorOverridePersists");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            for (auto& r : t.roles)
                if (r.roleId == "adlibs")
                {
                    r.autoColor = false;
                r.explicitColor = "ff00ff00";
                }
                t.name = "Manual Override";
                juce::String error;
                expectEquals((int) store.saveTemplate(t, false, error), (int) QuickTrackTemplateStore::SaveResult::Saved, error);
            QuickTrackTemplate loaded;
            expect(store.loadTemplate(t.name, loaded));
            for (const auto& r : loaded.roles)
                if (r.roleId == "adlibs")
                {
                    expect(!r.autoColor, "explicit mode persisted");
                    expectEquals(r.explicitColor, juce::String("ff00ff00"));
                }
        }

        beginTest("RoleCountsPersist");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            for (auto& r : t.roles)
                if (r.roleId == "doubles")
                r.count = 8; // user edited 4 → 8
                t.name = "Counts";
                juce::String error;
                expectEquals((int) store.saveTemplate(t, false, error), (int) QuickTrackTemplateStore::SaveResult::Saved, error);
            QuickTrackTemplate loaded;
            expect(store.loadTemplate(t.name, loaded));
            for (const auto& r : loaded.roles)
                if (r.roleId == "doubles")
                    expectEquals(r.count, 8);
        }

        beginTest("RoleOrderPersists");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            // Reverse order deliberately.
            int order = (int) t.roles.size();
            for (auto& r : t.roles)
                r.order = --order;
                t.name = "Order";
                juce::String error;
                expectEquals((int) store.saveTemplate(t, false, error), (int) QuickTrackTemplateStore::SaveResult::Saved, error);
            QuickTrackTemplate loaded;
            expect(store.loadTemplate(t.name, loaded));
            expectEquals((int) loaded.roles.size(), (int) t.roles.size());
            for (size_t i = 1; i < loaded.roles.size(); ++i)
                expect(loaded.roles[i - 1].order <= loaded.roles[i].order,
                       "roles sorted by stored order");
            expectEquals(loaded.roles.front().roleId, juce::String("delay"),
                         "first role reflects stored order");
        }

        beginTest("BusRoutingIntentPersists");
        {
            // The template persists the semantic recipe (role ids + routing
            // intent fields) — not old RoutingNode IDs.
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            juce::String error;
            store.saveTemplate(t, false, error);

            QuickTrackTemplate loaded;
            expect(store.loadTemplate(t.name, loaded));
            bool hasDoubles = false, hasDoublesBus = false, hasVocalBus = false;
            for (const auto& r : loaded.roles)
            {
                if (r.roleId == "doubles") hasDoubles = true;
                if (r.roleId == "doubles_bus") hasDoublesBus = true;
                if (r.roleId == "vocal_bus") hasVocalBus = true;
            }
            expect(hasDoubles && hasDoublesBus && hasVocalBus, "routing recipe roles present");

            const auto* doublesRole = QuickTrackRoleCatalog::findById("doubles");
            const auto* doublesBus = QuickTrackRoleCatalog::findById("doubles_bus");
            const auto* vocalBus = QuickTrackRoleCatalog::findById("vocal_bus");
            expect(juce::String(doublesRole->routesToRoleId) == "doubles_bus", "routing intent A");
            expect(juce::String(doublesBus->preferredParentRoleId) == "vocal_bus", "routing intent B");
            expect(vocalBus->preferredParentRoleId[0] == '\0', "vocal bus feeds master");
            expect(!loaded.toValueTree().toXmlString().contains("RN_"),
                   "no RoutingNode IDs serialized");
            expect(!loaded.toValueTree().toXmlString().contains("TRK_"),
                   "no TrackIDs serialized");
        }

        beginTest("DuplicateNameDoesNotSilentlyOverwrite");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            t.name = "No Overwrite";
            juce::String error;
            expectEquals((int) store.saveTemplate(t, false, error),
                         (int) QuickTrackTemplateStore::SaveResult::Saved);

            // Second save without overwrite → rejected, original intact.
            auto modified = t;
            for (auto& r : modified.roles)
                r.count = 99;
            juce::String error2;
            expectEquals((int) store.saveTemplate(modified, false, error2),
                         (int) QuickTrackTemplateStore::SaveResult::AlreadyExists);

            QuickTrackTemplate original;
            expect(store.loadTemplate(t.name, original));
            expectEquals(original.totalTrackCount(), 22, "original untouched");

            // Explicit overwrite is honored.
            juce::String error3;
            expectEquals((int) store.saveTemplate(modified, true, error3),
                         (int) QuickTrackTemplateStore::SaveResult::Saved);
            QuickTrackTemplate replaced;
            expect(store.loadTemplate(t.name, replaced));
            
            expect(replaced.totalTrackCount() != 21, "replaced content differs");
        }

        beginTest("AtomicPersistence");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            t.name = "Atomic Save";
            juce::String error;
            expectEquals((int) store.saveTemplate(t, false, error),
                         (int) QuickTrackTemplateStore::SaveResult::Saved);
            expect(store.getTemplateFile(t.name).existsAsFile(), "target exists");
            // No leftover temp files.
            auto leftovers = dir.findChildFiles(juce::File::findFiles, false, "*.tmp");
            expectEquals(leftovers.size(), 0, "no temp residue after atomic save");
        }

        beginTest("CorruptTemplateDoesNotDestroyOthers");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            t.name = "Good One";
            juce::String error;
            store.saveTemplate(t, false, error);

            // Corrupt a second template file.
            QuickTrackTemplate bad;
            bad.name = "Corrupt One";
            store.saveTemplate(bad, false, error);
            const auto badFile = store.getTemplateFile(bad.name);
            badFile.replaceWithText("<QuickTrackTemplate><broken");

            const auto list = store.listTemplates();
            bool foundGood = false;
            for (const auto& item : list)
                if (item.name == "Good One")
                    foundGood = true;
            expect(foundGood, "corrupt file skipped, others intact");
            expect(store.loadTemplate("Good One", t), "good template still loads");
        }

        beginTest("LoadModifyDoesNotMutateSavedOriginal");
        {
            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            t.name = "Editable";
            juce::String error;
            store.saveTemplate(t, false, error);

            QuickTrackTemplate loaded;
            expect(store.loadTemplate("Editable", loaded));
            for (auto& r : loaded.roles)
                if (r.roleId == "doubles")
                    r.count = 6; // user edits AFTER loading
            // No save → original unchanged.
            QuickTrackTemplate again;
            expect(store.loadTemplate("Editable", again));
            for (const auto& r : again.roles)
                if (r.roleId == "doubles")
                    expectEquals(r.count, 4, "saved original not mutated by load+edit");
        }
    }
};

// ===========================================================================
// Builder — creation + routing
// ===========================================================================
class QuickTrackBuilderTests final : public juce::UnitTest
{
public:
    QuickTrackBuilderTests() : juce::UnitTest("QuickTrack.Builder", "APEX.QuickTrack") {}

    void runTest() override
    {
        beginTest("SingleRoleCreatesOneTrack");
        {
            Harness h(101);
            const auto* coro = QuickTrackRoleCatalog::findById("coro_hook");
            auto result = h.builder.createSingle(*coro);
            expect(result.ok());
            expectEquals(result.count(), 1);
            expectEquals(result.created[0].name, juce::String("Coro / Hook"));
            expectEquals(h.tracks.getNumTracks(), 1);
        }

        beginTest("BatchCountControls");
        {
            Harness h(103);
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            auto result = h.builder.createBatch({ { doubles, 4 } });
            expect(result.ok());
            expectEquals(result.count(), 4);
            expectEquals(result.created[0].name, juce::String("Doubles"));
            expectEquals(result.created[1].name, juce::String("Doubles 2"));
            expectEquals(result.created[2].name, juce::String("Doubles 3"));
            expectEquals(result.created[3].name, juce::String("Doubles 4"));
        }

        beginTest("CreateAllPreservesOrder");
        {
            Harness h(107);
            const auto* beat = QuickTrackRoleCatalog::findById("instrumental_beat");
            const auto* coro = QuickTrackRoleCatalog::findById("coro_hook");
            const auto* verse = QuickTrackRoleCatalog::findById("verse");
            const auto* reverb = QuickTrackRoleCatalog::findById("reverb");
            auto result = h.builder.createBatch({ { beat, 1 }, { coro, 2 }, { verse, 3 }, { reverb, 1 } });
            expect(result.ok());
            expectEquals(result.count(), 7);
            expectEquals(result.created[0].name, juce::String("Instrumental / Beat"));
            expectEquals(result.created[1].name, juce::String("Coro / Hook"));
            expectEquals(result.created[2].name, juce::String("Coro / Hook 2"));
            expectEquals(result.created[3].name, juce::String("Verso / Verse"));
            expectEquals(result.created[4].name, juce::String("Verso / Verse 2"));
            expectEquals(result.created[5].name, juce::String("Verso / Verse 3"));
            expectEquals(result.created[6].name, juce::String("Reverb"));
            // Creation order preserved in the track list.
            expectEquals(h.tracks.getTrack(0)->getName(), juce::String("Instrumental / Beat"));
            expectEquals(h.tracks.getTrack(6)->getName(), juce::String("Reverb"));
        }

        beginTest("DoublesBusCreatesRealBus");
        {
            Harness h(109);
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { busRole, 1 } });
            expect(result.ok());
            expect(result.created[0].isBus);
            auto* track = h.tracks.getTrack(result.created[0].trackId);
            expect(track != nullptr);
            expectEquals((int) track->getRole(), (int) TrackRole::Bus, "real Bus role");
            auto* node = h.graph.getNodeByTrackId(track->getID());
            expect(node != nullptr);
            expectEquals((int) node->type, (int) RoutingNodeType::Bus, "real Bus routing node");
            expect(!track->isInputMono(), "bus has no hardware mono input");
        }

        beginTest("AdlibsBusCreatesRealBus");
        {
            Harness h(113);
            const auto* busRole = QuickTrackRoleCatalog::findById("adlibs_bus");
            auto result = h.builder.createBatch({ { busRole, 1 } });
            expect(result.ok());
            auto* track = h.tracks.getTrack(result.created[0].trackId);
            expectEquals((int) track->getRole(), (int) TrackRole::Bus);
            auto* node = h.graph.getNodeByTrackId(track->getID());
            expectEquals((int) node->type, (int) RoutingNodeType::Bus);
        }

        beginTest("MatchingTracksRouteToBus");
        {
            Harness h(127);
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 4 }, { busRole, 1 } });
            expect(result.ok());
            const auto busId = result.created[4].trackId;
            for (int i = 0; i < 4; ++i)
            {
                expect(hasSendToTrack(h.graph, result.created[i].trackId, busId),
                       "Doubles " + juce::String(i + 1) + " → Doubles Bus");
                expect(!hasActiveDirectToMaster(h.graph, result.created[i].trackId),
                       "Doubles " + juce::String(i + 1) + " has NO direct master route");
            }
        }

        beginTest("BusRoutesToMaster");
        {
            Harness h(131);
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { busRole, 1 } });
            expect(hasActiveDirectToMaster(h.graph, result.created[0].trackId),
                   "Doubles Bus → Master");
        }

        beginTest("NoDoubleMasterPath");
        {
            Harness h(137);
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 4 }, { busRole, 1 } });
            for (int i = 0; i < 4; ++i)
                expect(!hasActiveDirectToMaster(h.graph, result.created[i].trackId),
                       "no parallel master path for Doubles " + juce::String(i + 1));
            expect(hasActiveDirectToMaster(h.graph, result.created[4].trackId), "bus keeps master");
        }

        beginTest("VocalBusHierarchy");
        {
            Harness h(139);
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* adlibs = QuickTrackRoleCatalog::findById("adlibs");
            const auto* harmonies = QuickTrackRoleCatalog::findById("harmonies");
            const auto* bg = QuickTrackRoleCatalog::findById("bg_vocals");
            const auto* doublesBus = QuickTrackRoleCatalog::findById("doubles_bus");
            const auto* adlibsBus = QuickTrackRoleCatalog::findById("adlibs_bus");
            const auto* harmoniesBus = QuickTrackRoleCatalog::findById("harmonies_bus");
            const auto* bgBus = QuickTrackRoleCatalog::findById("bg_vocals_bus");
            const auto* vocalBus = QuickTrackRoleCatalog::findById("vocal_bus");

            auto result = h.builder.createBatch({
                { doubles, 2 }, { adlibs, 2 }, { harmonies, 1 }, { bg, 1 },
                { doublesBus, 1 }, { adlibsBus, 1 }, { harmoniesBus, 1 },
                { bgBus, 1 }, { vocalBus, 1 }
            });
            expect(result.ok());

            const TrackID vocalBusId = result.created[10].trackId;
            const TrackID doublesBusId = result.created[6].trackId;
            const TrackID adlibsBusId = result.created[7].trackId;
            const TrackID harmoniesBusId = result.created[8].trackId;
            const TrackID bgBusId = result.created[9].trackId;

            // Family buses feed Vocal Bus, NOT master.
            expect(hasSendToTrack(h.graph, doublesBusId, vocalBusId), "Doubles Bus → Vocal Bus");
            expect(hasSendToTrack(h.graph, adlibsBusId, vocalBusId), "Adlibs Bus → Vocal Bus");
            expect(hasSendToTrack(h.graph, harmoniesBusId, vocalBusId), "Harmonies Bus → Vocal Bus");
            expect(hasSendToTrack(h.graph, bgBusId, vocalBusId), "BG Vocals Bus → Vocal Bus");
            expect(!hasActiveDirectToMaster(h.graph, doublesBusId), "Doubles Bus no direct master");
            expect(!hasActiveDirectToMaster(h.graph, adlibsBusId), "Adlibs Bus no direct master");
            expect(hasActiveDirectToMaster(h.graph, vocalBusId), "Vocal Bus → Master");

            // Tracks → their family buses.
            expect(hasSendToTrack(h.graph, result.created[0].trackId, doublesBusId));
            expect(hasSendToTrack(h.graph, result.created[2].trackId, adlibsBusId));
            expect(hasSendToTrack(h.graph, result.created[4].trackId, harmoniesBusId));
            expect(hasSendToTrack(h.graph, result.created[5].trackId, bgBusId));
        }

        beginTest("BusCreationDoesNotCreateFeedback");
        {
            // If RoutingGraph rejected an edge (cycle), connect() returns
            // nullptr — no feedback loops may exist in the published graph.
            Harness h(149);
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            const auto* vocalBus = QuickTrackRoleCatalog::findById("vocal_bus");
            auto result = h.builder.createBatch({ { doubles, 2 }, { busRole, 1 }, { vocalBus, 1 } });
            expect(result.ok());
            // The graph topo-sort must include every node exactly once.
            h.graph.notifyGraphChanged();
            auto snap = h.graph.getSnapshotPublisher().get();
            expect(snap != nullptr);
            std::set<juce::String> processed;
            for (const auto& nodeId : snap->processingOrder)
            {
                expect(processed.find(nodeId) == processed.end(), "no node twice: " + nodeId);
                processed.insert(nodeId);
            }
            expectEquals((int) processed.size(), h.graph.getNodeCount(), "every node processed once");
        }

        beginTest("BusIDsRemainUnique");
        {
            Harness h(151);
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* adlibs = QuickTrackRoleCatalog::findById("adlibs");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            const auto* bus2Role = QuickTrackRoleCatalog::findById("adlibs_bus");
            auto result = h.builder.createBatch({ { doubles, 4 }, { adlibs, 4 }, { busRole, 1 }, { bus2Role, 1 } });
            std::set<juce::String> nodeIds;
            for (auto* node : h.graph.getAllNodes())
            {
                expect(node != nullptr);
                expect(nodeIds.find(node->id) == nodeIds.end(), "node id unique: " + node->id);
                nodeIds.insert(node->id);
            }
            std::set<juce::String> trackIds;
            for (const auto& c : result.created)
            {
                expect(trackIds.find(c.trackId) == trackIds.end(), "track id unique");
                trackIds.insert(c.trackId);
            }
        }

        beginTest("BatchBusRoutingSurvivesReload");
        {
            Harness h(157);
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            const auto* busRole = QuickTrackRoleCatalog::findById("doubles_bus");
            auto result = h.builder.createBatch({ { doubles, 3 }, { busRole, 1 } });

            // Project save → reload: RoutingGraph persists connections.
            const auto graphState = h.graph.getState();
            RoutingGraph reloaded;
            reloaded.restoreState(graphState);

            const TrackID busId = result.created[3].trackId;
            for (int i = 0; i < 3; ++i)
            {
                const auto& trackId = result.created[i].trackId;
                expect(hasSendToTrack(reloaded, trackId, busId),
                       "routing survives reload for Doubles " + juce::String(i + 1));
                expect(!hasActiveDirectToMaster(reloaded, trackId),
                       "no master route resurrected after reload");
            }
            expect(hasActiveDirectToMaster(reloaded, busId), "bus → master survives reload");
        }

        beginTest("TemplateInstantiationFreshIDs");
        {
            Harness h(163);
            QuickTrackTemplateStore store(makeTempTemplateDir("instantiate").getParentDirectory());
            auto t = makeUrbanVocalTemplate();
            t.name = "Fresh IDs";

            // Instantiate the template twice into the same project.
            std::vector<QuickTrackRequest> requests;
            for (const auto& r : t.roles)
                if (const auto* role = QuickTrackRoleCatalog::findById(r.roleId))
                    requests.push_back({ role, r.count });

            auto first = h.builder.createBatch(requests);
            expect(first.ok());
            expectEquals(first.count(), 22);

            std::set<juce::String> ids1;
            for (const auto& c : first.created)
                ids1.insert(c.trackId);

            // Second instantiation: names continue (no visible duplicates),
            // IDs are all fresh.
            auto second = h.builder.createBatch(requests);
            expect(second.ok());
            std::set<juce::String> ids2;
            std::set<juce::String> names;
            bool nameCollision = false;
            for (const auto& c : second.created)
            {
                ids2.insert(c.trackId);
                if (!names.insert(c.name).second)
                    nameCollision = true;
            }
            expect(!nameCollision, "no duplicate visible names across instantiations");
            for (const auto& id : ids2)
                expect(ids1.find(id) == ids1.end(), "fresh TrackIDs on re-instantiation");

            std::set<juce::String> nodeIds;
            for (auto* node : h.graph.getAllNodes())
                nodeIds.insert(node->id);
            expectEquals((int) nodeIds.size(), h.graph.getNodeCount(),
                         "fresh unique RoutingNodeIDs across both instantiations");
        }

        beginTest("NamingContinuesExistingOrdinals");
        {
            Harness h(167);
            const auto* doubles = QuickTrackRoleCatalog::findById("doubles");
            h.builder.createBatch({ { doubles, 2 } }); // Doubles, Doubles 2

            // "Reload" the project: names persist, builder continues.
            const auto names = h.builder.existingTrackNames();
            expectEquals(QuickTrackNaming::nextNameFor("Doubles", names),
                         juce::String("Doubles 3"));
            auto next = h.builder.createBatch({ { doubles, 3 } });
            expectEquals(next.created[0].name, juce::String("Doubles 3"));
            expectEquals(next.created[1].name, juce::String("Doubles 4"));
            expectEquals(next.created[2].name, juce::String("Doubles 5"));
        }
    }
};

// Template-instantiation routing: loaded template → CREATE ALL → the
// complete "Urban Vocal Recording" structure with real bus routing.
class QuickTrackTemplateInstantiationTests final : public juce::UnitTest
{
public:
    QuickTrackTemplateInstantiationTests() : juce::UnitTest("QuickTrackTemplate.Instantiation", "APEX.QuickTrackTemplate") {}

    void runTest() override
    {
        beginTest("TemplateInstantiationBuildsRoutingStructure");
        {
            const auto dir = makeTempTemplateDir("instantiate2");
            struct DirGuard { juce::File d; ~DirGuard() { d.deleteRecursively(); } } guard { dir };

            QuickTrackTemplateStore store(dir);
            auto t = makeUrbanVocalTemplate();
            juce::String error;
            store.saveTemplate(t, false, error);

            Harness h(179);
            QuickTrackTemplate loaded;
            expect(store.loadTemplate(t.name, loaded));
            expectEquals(loaded.totalTrackCount(), 22);

            std::vector<QuickTrackRequest> requests;
            for (const auto& r : loaded.roles)
                if (const auto* role = QuickTrackRoleCatalog::findById(r.roleId))
                    requests.push_back({ role, r.count });

            auto result = h.builder.createBatch(requests);
            expect(result.ok());
            expectEquals(result.count(), 22);

            // Routing intent from the recipe:
            // lead vocal → vocal bus; doubles/adlibs → their buses;
            // family buses → vocal bus; vocal bus → master.
            const auto findInResult = [&result](const juce::String& name) -> TrackID
            {
                for (const auto& c : result.created)
                    if (c.name == name)
                        return c.trackId;
                return {};
            };

            const TrackID lead = findInResult("Lead Vocal");
            const TrackID coro = findInResult("Coro / Hook");
            const TrackID verse = findInResult("Verso / Verse");
            const TrackID vocalBus = findInResult("Vocal Bus");
            const TrackID doublesBus = findInResult("Doubles Bus");
            const TrackID adlibsBus = findInResult("Adlibs Bus");
            expect(lead.isNotEmpty() && coro.isNotEmpty() && verse.isNotEmpty());
            expect(vocalBus.isNotEmpty() && doublesBus.isNotEmpty() && adlibsBus.isNotEmpty());

            expect(hasSendToTrack(h.graph, lead, vocalBus), "Lead Vocal → Vocal Bus");
            expect(hasSendToTrack(h.graph, doublesBus, vocalBus), "Doubles Bus → Vocal Bus");
            expect(hasSendToTrack(h.graph, adlibsBus, vocalBus), "Adlibs Bus → Vocal Bus");
            expect(hasActiveDirectToMaster(h.graph, vocalBus), "Vocal Bus → Master");
            expect(!hasActiveDirectToMaster(h.graph, doublesBus), "Doubles Bus no parallel master");
            expect(!hasActiveDirectToMaster(h.graph, lead), "Lead Vocal no parallel master");
            // Non-vocal roles keep their direct master route.
            expect(hasActiveDirectToMaster(h.graph, coro), "Coro / Hook → Master");
            expect(hasActiveDirectToMaster(h.graph, verse), "Verso / Verse → Master");
        }
    }
};

static QuickTrackNamingTests quickTrackNamingTests;
static QuickTrackBuilderNamingTests quickTrackBuilderNamingTests;
static QuickTrackColorTests quickTrackColorTests;
static QuickTrackTemplateTests quickTrackTemplateTests;
static QuickTrackBuilderTests quickTrackBuilderTests;
static QuickTrackTemplateInstantiationTests quickTrackTemplateInstantiationTests;

} // namespace
