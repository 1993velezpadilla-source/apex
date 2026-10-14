// ===========================================================================
// QuickTrackBuilderCore.h
// APEX Quick Track Builder — batch track/bus creation service.
//
// Uses ONLY canonical APEX systems:
//   * TrackManager::createTrack           (model authority)
//   * RoutingGraph::addNode/connect       (routing authority)
//   * MasterRouteStateCore::deleteMasterRoute (single master-route owner)
//   * QuickTrackColorSystem               (project-scoped color families)
//
// Batch creation contract:
//   * every created track gets a unique TrackID (IDGenerator) and a unique
//     RoutingNodeID (RoutingGraph allocator — the restored-allocator rebase
//     in RoutingGraph::restoreState is untouched),
//   * names come from QuickTrackNaming (never identity),
//   * recording tracks default to Mono 1 (TrackManager change),
//   * buses are real RoutingNodeType::Bus destinations with NO hardware
//     input, feeding the existing master path,
//   * smart routing replaces a track's Direct→master edge with a
//     Direct→bus edge (NO duplicate audible path, NO feedback loops —
//     RoutingGraph::connect cycle-checks every non-sidechain edge).
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <vector>
#include "../TrackCore/Track.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../RoutingCore/MasterRouteStateCore.h"
#include "../Bubblegum/BubblegumSendStateCore.h"
#include "../QuickTrackCore/QuickTrackRoles.h"
#include "../QuickTrackCore/QuickTrackNaming.h"
#include "../QuickTrackCore/QuickTrackColorSystem.h"

namespace DAW {

struct QuickTrackRequest
{
    const QuickTrackRole* role = nullptr;
    int count = 0;
    /** Optional display-name override (a specific Vocal FX chain such as
     *  "Auto Pitch"). Empty = the role's own auto-numbered name. Identity,
     *  colour family and routing are unaffected. */
    juce::String nameOverride;
};

struct QuickTrackCreation
{
    TrackID trackId;
    juce::String name;
    const QuickTrackRole* role = nullptr;
    bool isBus = false;
    juce::Colour color;
};

struct QuickTrackBatchResult
{
    std::vector<QuickTrackCreation> created;
    /** The actual Bubblegum route pairs applied (source → bus), recorded so
     *  undo/redo can reverse exactly what was routed. */
    std::vector<std::pair<TrackID, TrackID>> routePairs;
    juce::String error;
    bool ok() const noexcept { return error.isEmpty(); }
    int count() const noexcept { return (int) created.size(); }
};

class QuickTrackBuilderCore
{
public:
    QuickTrackBuilderCore(TrackManager& tracks,
                          RoutingGraph& graph,
                          MasterRouteStateCore& masterRoute,
                          BubblegumSendStateCore& sends,
                          QuickTrackColorSystem& colors)
        : tracks_(tracks), graph_(graph), masterRoute_(masterRoute), sends_(sends), colors_(colors)
    {
    }

    // ── Naming ────────────────────────────────────────────────────────────

    std::vector<juce::String> existingTrackNames() const
    {
        std::vector<juce::String> names;
        if (tracks_.hasMasterTrack())
            if (auto* master = tracks_.getMasterTrack())
                names.push_back(master->getName());
        for (int i = 0; i < tracks_.getNumTracks(); ++i)
            if (auto* track = tracks_.getTrack(i))
                names.push_back(track->getName());
        return names;
    }

    /** Next visible name for a role. Untitled uses the canonical generic
     *  "Audio N" naming; predefined roles keep their display base. */
    juce::String nextTrackNameFor(const QuickTrackRole& role) const
    {
        const auto existing = existingTrackNames();
        if (juce::String(role.roleId) == "untitled")
            return QuickTrackNaming::genericUntitledName(existing);
        return QuickTrackNaming::nextNameFor(role.displayName, existing);
    }

    // ── Colors ────────────────────────────────────────────────────────────

    /** Stable project color-family key for a role. */
    static juce::String colorKeyForRole(const QuickTrackRole& role)
    {
        return QuickTrackColorSystem::normalizeKey(role.roleId);
    }

    /** Key used for the generated-name family of Untitled tracks. */
    static juce::String colorKeyForGeneratedName(const juce::String& generatedName)
    {
        return QuickTrackColorSystem::normalizeKey(generatedName);
    }

    juce::Colour getRoleColor(const QuickTrackRole& role) const
    {
        return colors_.getColorForRole(colorKeyForRole(role));
    }

    bool isRoleColorManual(const QuickTrackRole& role) const
    {
        return colors_.isManual(colorKeyForRole(role));
    }

    void setManualRoleColor(const QuickTrackRole& role, const juce::Colour& colour)
    {
        colors_.setManualRoleColor(colorKeyForRole(role), colour);
    }

    void clearManualRoleColor(const QuickTrackRole& role)
    {
        colors_.clearManualOverride(colorKeyForRole(role));
    }

    // ── Creation ──────────────────────────────────────────────────────────

    /** Create one track of the role immediately (fast single-create flow). */
    QuickTrackBatchResult createSingle(const QuickTrackRole& role)
    {
        QuickTrackRequest req { &role, 1 };
        return createBatch({ req });
    }

    /** Create a whole queue in one transaction. Returns every created
     *  track/bus; on failure the error is set and no partial result is
     *  reported as success (already-created tracks remain — the canonical
     *  TrackManager/RoutingGraph publication is per-track). */
    QuickTrackBatchResult createBatch(const std::vector<QuickTrackRequest>& requests)
    {
        auto result = createOnlyBatch(requests);
        if (!result.ok())
            return result;

        applySmartRouting(result);
        return result;
    }

    /** Create tracks/buses without changing any existing routing.  This is
     *  the control-plane primitive used by Quick Track: Route is a separate
     *  user intent and must not be silently applied by the create-only tab. */
    QuickTrackBatchResult createOnlyBatch(const std::vector<QuickTrackRequest>& requests)
    {
        QuickTrackBatchResult result;
        std::vector<TrackID> existingOrder;
        existingOrder.reserve((size_t) tracks_.getNumTracks());
        for (int i = 0; i < tracks_.getNumTracks(); ++i)
            if (auto* existing = tracks_.getTrack(i))
                existingOrder.push_back(existing->getID());

        for (const auto& req : requests)
        {
            if (req.role == nullptr || req.count < 0)
            {
                result.error = "Invalid Quick Track request";
                return result;
            }
            for (int i = 0; i < req.count; ++i)
            {
                auto* track = createOne(*req.role, result, req.nameOverride);
                if (track == nullptr)
                {
                    if (result.error.isEmpty())
                        result.error = "Track creation failed for " + juce::String(req.role->displayName);
                    return result;
                }
            }
        }

        applyRoleAwareInsertion(existingOrder, result);
        return result;
    }

private:
    void applyRoleAwareInsertion(const std::vector<TrackID>& existingOrder,
                                 const QuickTrackBatchResult& result)
    {
        if (result.created.empty())
            return;

        std::vector<TrackID> orderedIds = existingOrder;
        orderedIds.reserve(existingOrder.size() + result.created.size());

        for (const auto& created : result.created)
        {
            const auto roleId = created.role != nullptr
                ? juce::String(created.role->roleId) : juce::String();

            // Untitled is intentionally generic: retain the normal append
            // behavior instead of forcing a semantic group.
            if (roleId.isEmpty() || roleId == "untitled")
            {
                orderedIds.push_back(created.trackId);
                continue;
            }

            int lastMatchingIndex = -1;
            for (int i = 0; i < (int) orderedIds.size(); ++i)
            {
                if (auto* existing = tracks_.getTrack(orderedIds[(size_t) i]))
                    if (existing->getQuickRoleId() == roleId)
                        lastMatchingIndex = i;
            }

            // A role with no existing match follows normal append semantics.
            // Compute the integer insertion position before doing iterator
            // arithmetic; begin() + (-1) is invalid even if followed by +1.
            const auto insertionIndex = lastMatchingIndex >= 0
                ? (size_t) (lastMatchingIndex + 1)
                : orderedIds.size();
            orderedIds.insert(orderedIds.begin() + (std::ptrdiff_t) insertionIndex,
                              created.trackId);
        }

        bool changed = orderedIds.size() != existingOrder.size() + result.created.size();
        if (!changed)
            for (size_t i = 0; i < orderedIds.size(); ++i)
                if (i >= existingOrder.size() || orderedIds[i] != existingOrder[i])
                {
                    changed = true;
                    break;
                }

        if (changed)
            tracks_.applyTrackOrder(orderedIds);
    }

    // ── Per-track creation ────────────────────────────────────────────────

    Track* createOne(const QuickTrackRole& role, QuickTrackBatchResult& result,
                     const juce::String& nameOverride = {})
    {
        const bool isBus = QuickTrackRoleCatalog::isBusKind(role);

        // 1) Name — display only, never identity.
        // A variant override (e.g. "Auto Pitch" on the Vocal FX role) keeps
        // the same role, colour family and routing; uniquified like any name.
        const juce::String name = nameOverride.isNotEmpty()
            ? QuickTrackNaming::nextNameFor(nameOverride, existingTrackNames())
            : nextTrackNameFor(role);

        // 2) Color family — project-scoped; manual override wins, otherwise
        //    AUTO assignment (reuse family color or assign a new distinct one).
        juce::Colour colour;
        const juce::String colorKey = colorKeyForRole(role);
        if (colors_.isManual(colorKey))
            colour = colors_.getColorForRole(colorKey);
        else
            colour = colors_.assignColorForRole(colorKey);

        // 3) Model creation. Recording tracks get the Mono 1 default inside
        //    TrackManager::createTrack; buses are switched to internal
        //    routing (no hardware input) below.
        auto* track = tracks_.createTrack(name);
        if (track == nullptr)
            return nullptr;

        track->setQuickRoleId(juce::String(role.roleId));
        track->setColor(colour);

        if (isBus)
        {
            track->setRole(TrackRole::Bus);
            track->setInputSource(0, false); // internal routing — no hardware input
        }

        // 4) Routing node. In the running app ApplicationCore::trackAdded
        //    already registered the node (as Track type) synchronously during
        //    createTrack; in headless/test contexts the node is missing and
        //    we register it here — mirroring FolderBusCore::createFolderBus.
        auto* node = graph_.getNodeByTrackId(track->getID());
        if (node == nullptr)
        {
            graph_.addNode(name,
                           isBus ? RoutingNodeType::Bus : RoutingNodeType::Track,
                           track->getID());
        }
        else if (isBus)
        {
            node->type = RoutingNodeType::Bus;
            graph_.publishSnapshotOnly();
        }

        result.created.push_back({ track->getID(), name, &role, isBus, colour });
        return track;
    }

    // ── Smart routing ─────────────────────────────────────────────────────

    void applySmartRouting(QuickTrackBatchResult& result)
    {
        // Tracks → their family bus (batch-created first, then pre-existing).
        for (const auto& c : result.created)
        {
            if (c.isBus || c.role == nullptr)
                continue;
            const char* targetRoleId = c.role->routesToRoleId;
            if (targetRoleId == nullptr || targetRoleId[0] == '\0')
                continue;
            const TrackID busId = findBusTrackId(targetRoleId, result.created);
            if (busId.isEmpty())
                continue;
            routeTrackTo(c.trackId, busId);
            result.routePairs.emplace_back(c.trackId, busId);
        }

        // Family buses → preferred parent bus (e.g. Doubles Bus → Vocal Bus),
        // replacing their Direct→master edge so there is exactly one path.
        for (const auto& c : result.created)
        {
            if (!c.isBus || c.role == nullptr)
                continue;
            const char* parentRoleId = c.role->preferredParentRoleId;
            if (parentRoleId == nullptr || parentRoleId[0] == '\0')
                continue;
            const TrackID parentId = findBusTrackId(parentRoleId, result.created);
            if (parentId.isEmpty() || parentId == c.trackId)
                continue;
            routeTrackTo(c.trackId, parentId);
            result.routePairs.emplace_back(c.trackId, parentId);
        }
    }

    TrackID findBusTrackId(const juce::String& roleId,
                           const std::vector<QuickTrackCreation>& batchCreated) const
    {
        // 1) Buses created in this batch take priority.
        for (const auto& c : batchCreated)
            if (c.isBus && c.role != nullptr && roleId == c.role->roleId)
                return c.trackId;

        // 2) Pre-existing bus with the canonical bus name.
        const auto* busRole = QuickTrackRoleCatalog::findById(roleId);
        if (busRole == nullptr)
            return {};
        for (int i = 0; i < tracks_.getNumTracks(); ++i)
        {
            auto* track = tracks_.getTrack(i);
            if (track == nullptr || track->getRole() != TrackRole::Bus)
                continue;
            if (track->getQuickRoleId() == roleId)
                return track->getID();
        }
        return {};
    }

    /** Canonical Bubblegum subgroup route: creates a REAL Bubblegum send
     *  (ConnectionType::Send — the same edge the Bubblegum UI renders as a
     *  cable, identical to a manual Quick Send) and deactivates the source's
     *  Direct→master edge (ExistsInactive — reversible, no double audible
     *  path). The master edge is NOT deleted: Undo restores it by simply
     *  re-activating, and the engine skips inactive edges. */
    void routeTrackTo(const TrackID& sourceTrackId, const TrackID& destTrackId)
    {
        if (sourceTrackId == destTrackId)
            return;
        sends_.createSend(graph_, sourceTrackId, destTrackId, 1.0f);
        masterRoute_.setMasterRouteState(sourceTrackId, RouteState::ExistsInactive);
    }

    TrackManager& tracks_;
    RoutingGraph& graph_;
    MasterRouteStateCore& masterRoute_;
    BubblegumSendStateCore& sends_;
    QuickTrackColorSystem& colors_;

    // Detached colour callouts may finish after their control-plane owner.
    // Access and destruction stay on the JUCE message thread.
    JUCE_DECLARE_WEAK_REFERENCEABLE(QuickTrackBuilderCore)
};

} // namespace DAW
