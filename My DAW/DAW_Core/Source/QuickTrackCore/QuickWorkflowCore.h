// ===========================================================================
// QuickWorkflowCore.h
// APEX Quick Workflow — shared Track / Route / Send control-plane model.
//
// The legacy QuickTrackRoleCatalog remains available for existing templates
// and regression fixtures.  This catalog is the deliberately smaller product
// surface for the new workflow: vocal/recording roles only, no beat-making
// presets, and no Master creation card.
// ===========================================================================
#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <set>
#include <vector>

#include "QuickTrackBuilderCore.h"
#include "../Bubblegum/BubblegumSendLevelCore.h"

namespace DAW
{

enum class QuickWorkflowTab
{
    QuickTrack,
    QuickRoute,
    QuickSend
};

// Quick Send remains implemented for compatibility with existing Bubblegum
// routing state, but its dedicated Quick Workflow presentation is currently
// disabled.  Keeping this policy separate from QuickSendModeCore preserves the
// underlying control-plane implementation without exposing a third tab.
inline constexpr bool kQuickSendWorkflowUiEnabled = false;

struct QuickWorkflowRequest
{
    const QuickTrackRole* role = nullptr;
    int count = 0;
    /** Optional display-name override (e.g. a specific Vocal FX chain like
     *  "Auto Pitch"). Empty = the role's own auto-numbered name. Identity and
     *  routing are unaffected — the role still drives creation and routing. */
    juce::String nameOverride;
};

struct QuickWorkflowSendDestination
{
    TrackID trackId;
    juce::String name;
    float level = 0.0f;
    bool exists = false;
    bool active = false;
    bool preFader = false;
};

/** Product-facing Quick Workflow catalog.
 *
 * Role IDs are stable semantic keys.  TrackID and RoutingNodeID are still
 * allocated by the canonical TrackManager/RoutingGraph systems at creation
 * time; neither is stored in this catalog.
 */
class QuickWorkflowRoleCatalog
{
public:
    static const std::vector<QuickTrackRole>& trackRoles()
    {
        static const std::vector<QuickTrackRole> roles =
        {
            { "intro",     "Intro",       "quick_tracks", QuickTrackKind::NormalTrack, "vocal_bus",  "" },
            { "hook_coro", "Hook / Coro", "quick_tracks", QuickTrackKind::NormalTrack, "vocal_bus",  "" },
            { "verse",     "Verso / Verse", "quick_tracks", QuickTrackKind::NormalTrack, "vocal_bus",  "" },
            { "adlibs",    "Adlibs",       "quick_tracks", QuickTrackKind::NormalTrack, "adlibs_bus",  "" },
            { "doubles",   "Doubles",      "quick_tracks", QuickTrackKind::NormalTrack, "doubles_bus", "" },
            { "harmonies", "Harmonies",    "quick_tracks", QuickTrackKind::NormalTrack, "vocal_bus",  "" },
            { "vocal_fx",  "Vocal FX",     "quick_tracks", QuickTrackKind::NormalTrack, "fx_bus",     "" },
            { "vocal_reverb", "Reverb",    "quick_tracks", QuickTrackKind::NormalTrack, "fx_bus",     "" },
            { "vocal_delay",  "Delay",     "quick_tracks", QuickTrackKind::NormalTrack, "fx_bus",     "" },
            { "vocal_pitch",  "Pitch",     "quick_tracks", QuickTrackKind::NormalTrack, "fx_bus",     "" },
            { "vocal_mod",    "Modulation","quick_tracks", QuickTrackKind::NormalTrack, "fx_bus",     "" },
            { "vocal_char",   "Character", "quick_tracks", QuickTrackKind::NormalTrack, "fx_bus",     "" },
            { "vocal_stereo", "Stereo",    "quick_tracks", QuickTrackKind::NormalTrack, "fx_bus",     "" },
            { "untitled",  "Untitled",     "quick_tracks", QuickTrackKind::NormalTrack, "",           "" }
        };
        return roles;
    }

    static const std::vector<QuickTrackRole>& busRoles()
    {
        static const std::vector<QuickTrackRole> roles =
        {
            { "vocal_bus",   "Vocal Bus",   "quick_buses", QuickTrackKind::Bus, "", "mix_bus" },
            { "doubles_bus", "Doubles Bus", "quick_buses", QuickTrackKind::Bus, "", "vocal_bus" },
            { "adlibs_bus",  "Adlibs Bus",  "quick_buses", QuickTrackKind::Bus, "", "vocal_bus" },
            { "fx_bus",      "FX Bus",      "quick_buses", QuickTrackKind::Bus, "", "mix_bus" },
            { "music_bus",   "Music Bus",   "quick_buses", QuickTrackKind::Bus, "", "mix_bus" },
            { "mix_bus",     "Mix Bus",     "quick_buses", QuickTrackKind::Bus, "", "" },
            { "untitled_bus", "Untitled Bus", "quick_buses", QuickTrackKind::Bus, "", "" }
        };
        return roles;
    }

    static const std::vector<const QuickTrackRole*>& allRoles()
    {
        static const std::vector<const QuickTrackRole*> roles = []
        {
            std::vector<const QuickTrackRole*> result;
            for (const auto& role : trackRoles()) result.push_back(&role);
            for (const auto& role : busRoles())   result.push_back(&role);
            return result;
        }();
        return roles;
    }

    static const QuickTrackRole* findById(const juce::String& roleId)
    {
        for (const auto& role : trackRoles())
            if (roleId == role.roleId)
                return &role;
        for (const auto& role : busRoles())
            if (roleId == role.roleId)
                return &role;
        return nullptr;
    }

    static const QuickTrackRole* findByDisplayName(const juce::String& name)
    {
        for (const auto* role : allRoles())
            if (role != nullptr && name == role->displayName)
                return role;
        return nullptr;
    }

    static bool isBusRole(const QuickTrackRole& role) noexcept
    {
        return role.kind == QuickTrackKind::Bus || role.kind == QuickTrackKind::Return;
    }

    /** Vocal FX variant catalog — the common vocal chains grouped by family.
     *
     *  Each entry belongs to an owning card role:
     *   - the family cards (Reverb / Delay / Pitch / Modulation / Character /
     *     Stereo) offer their own entries, grouped into submenus when a family
     *     has more than one group (the Delay card separates the by-source
     *     delays from the delay types);
     *   - the generic Vocal FX card offers every family as submenus.
     *
     *  Choosing an entry creates the same track as the owning role —
     *  identical colour family and routing to the FX bus — with the chain's
     *  own display name. Every card keeps its own plus / minus / create /
     *  clear controls; the dropdown only picks the chain name. */
    struct FxVariant
    {
        const char* roleId;    // owning card role
        const char* category;  // submenu label
        const char* name;      // created track name
    };

    static const std::vector<FxVariant>& vocalFxVariantCatalog()
    {
        static const std::vector<FxVariant> items =
        {
            // Reverb family — by source first, then by type (same two-family
            // structure as the Delay card).
            { "vocal_reverb", "Reverb by Source", "Vocal Reverb" },
            { "vocal_reverb", "Reverb by Source", "Doubles Reverb" },
            { "vocal_reverb", "Reverb by Source", "Adlib / HL Reverb" },
            { "vocal_reverb", "Reverb by Source", "Harmonies Reverb" },
            { "vocal_reverb", "Reverb Types", "Plate Reverb" },
            { "vocal_reverb", "Reverb Types", "Hall Reverb" },
            { "vocal_reverb", "Reverb Types", "Shimmer Reverb" },
            { "vocal_reverb", "Reverb Types", "Room Reverb" },
            { "vocal_reverb", "Reverb Types", "Chamber Reverb" },
            { "vocal_reverb", "Reverb Types", "Spring Reverb" },
            // Delay family — by source first, then by type
            { "vocal_delay", "Delay by Source", "Delay Vocal" },
            { "vocal_delay", "Delay by Source", "Delay Doubles" },
            { "vocal_delay", "Delay by Source", "Delay Adlibs / HL" },
            { "vocal_delay", "Delay by Source", "Delay Harmonies" },
            { "vocal_delay", "Delay Types", "Slap Delay" },
            { "vocal_delay", "Delay Types", "1/8 Delay" },
            { "vocal_delay", "Delay Types", "1/4 Delay" },
            { "vocal_delay", "Delay Types", "Dotted 1/8 Delay" },
            { "vocal_delay", "Delay Types", "Ping-Pong Delay" },
            // Pitch family
            { "vocal_pitch", "Pitch", "Auto Pitch" },
            { "vocal_pitch", "Pitch", "Harmonizer" },
            { "vocal_pitch", "Pitch", "Vocal Doubler" },
            // Modulation family
            { "vocal_mod", "Modulation", "Chorus" },
            { "vocal_mod", "Modulation", "Flanger" },
            { "vocal_mod", "Modulation", "Phaser" },
            // Character family
            { "vocal_char", "Character", "Telephone / Lo-Fi" },
            { "vocal_char", "Character", "Bitcrush" },
            // Stereo family
            { "vocal_stereo", "Stereo", "Stereo Widener" },
            { "vocal_stereo", "Stereo", "Mid / Side Widener" }
        };
        return items;
    }

    static bool roleHasVariants(const juce::String& roleId)
    {
        if (roleId == "vocal_fx")
            return true;
        for (const auto& item : vocalFxVariantCatalog())
            if (juce::String(item.roleId) == roleId)
                return true;
        return false;
    }

    /** Build the variant dropdown for a role. Item ids are sequential
     *  (1-based) and `idToName` receives the matching display names so the
     *  caller can map a menu result back to the chain to create. */
    static void buildVariantMenu(juce::PopupMenu& menu,
                                 const juce::String& roleId,
                                 juce::StringArray& idToName)
    {
        idToName.clear();

        const auto& catalog = vocalFxVariantCatalog();

        // Entries this role offers; the generic Vocal FX card offers all.
        std::vector<const FxVariant*> entries;
        for (const auto& item : catalog)
            if (juce::String(item.roleId) == roleId)
                entries.push_back(&item);
        if (entries.empty() && roleId == "vocal_fx")
            for (const auto& item : catalog)
                entries.push_back(&item);
        if (entries.empty())
            return;

        juce::StringArray orderedCategories;
        for (const auto* entry : entries)
        {
            const juce::String category(entry->category);
            if (! orderedCategories.contains(category))
                orderedCategories.add(category);
        }

        if (orderedCategories.size() <= 1)
        {
            for (const auto* entry : entries)
            {
                idToName.add(entry->name);
                menu.addItem(idToName.size(), entry->name);
            }
            return;
        }

        for (const auto& category : orderedCategories)
        {
            juce::PopupMenu sub;
            for (const auto* entry : entries)
            {
                if (juce::String(entry->category) != category)
                    continue;
                idToName.add(entry->name);
                sub.addItem(idToName.size(), entry->name);
            }
            menu.addSubMenu(category, sub);
        }
    }
};

class QuickWorkflowCore
{
public:
    QuickWorkflowCore(TrackManager& tracks,
                      RoutingGraph& graph,
                      MasterRouteStateCore& masterRoute,
                      BubblegumSendStateCore& sends,
                      QuickTrackColorSystem& colors)
        : builder_(tracks, graph, masterRoute, sends, colors),
          tracks_(tracks),
          graph_(graph),
          masterRoute_(masterRoute),
          sends_(sends),
          sendLevels_()
    {
    }

    QuickTrackBuilderCore& builder() noexcept { return builder_; }
    const QuickTrackBuilderCore& builder() const noexcept { return builder_; }

    juce::String getTrackName(const TrackID& trackId) const
    {
        if (auto* track = tracks_.getTrack(trackId))
            return track->getName();
        return {};
    }

    QuickTrackBatchResult apply(QuickWorkflowTab tab,
                                const std::vector<QuickWorkflowRequest>& requests)
    {
        if (tab == QuickWorkflowTab::QuickSend)
        {
            QuickTrackBatchResult result;
            result.error = "Quick Send is not a track-creation operation";
            return result;
        }

        // Quick Track is intentionally create-only.  Quick Route uses the
        // same creation primitive, then applies routing to both the newly
        // created matches and pre-existing canonical matches.
        auto creationRequests = requests;
        if (tab == QuickWorkflowTab::QuickTrack || tab == QuickWorkflowTab::QuickRoute)
        {
            // Named product buses are single canonical destinations.  Both
            // tabs therefore reuse an existing one and cap a fresh request
            // at one creation.  Untitled Bus remains intentionally generic
            // and duplicable.
            for (auto& request : creationRequests)
            {
                if (request.role == nullptr
                    || !QuickWorkflowRoleCatalog::isBusRole(*request.role)
                    || juce::String(request.role->roleId) == "untitled_bus"
                    || request.count < 0)
                    continue;

                request.count = findCanonicalBus(*request.role).isNotEmpty()
                    ? 0 : juce::jmin(request.count, 1);
            }
        }

        auto result = builder_.createOnlyBatch(toBuilderRequests(creationRequests));
        if (!result.ok() || tab != QuickWorkflowTab::QuickRoute)
            return result;

        std::set<juce::String> routedRoles;
        for (const auto& request : requests)
        {
            if (request.role == nullptr || request.count < 0)
                continue;
            const juce::String roleId(request.role->roleId);
            if (routedRoles.insert(roleId).second)
                routeMatchingRole(*request.role, result);
        }
        return result;
    }

    /** Route all existing matches for a role without creating anything. */
    QuickTrackBatchResult routeExisting(const QuickTrackRole& role)
    {
        QuickTrackBatchResult result;
        routeMatchingRole(role, result);
        return result;
    }

    std::vector<QuickWorkflowSendDestination> getSendDestinations(const TrackID& sourceId) const
    {
        std::vector<QuickWorkflowSendDestination> result;
        for (int i = 0; i < tracks_.getNumTracks(); ++i)
        {
            auto* track = tracks_.getTrack(i);
            if (track == nullptr || track->getID() == sourceId || !isBusTrack(*track))
                continue;

            QuickWorkflowSendDestination destination;
            destination.trackId = track->getID();
            destination.name = track->getName();
            destination.exists = sends_.sendExists(graph_, sourceId, destination.trackId);
            destination.level = sendLevels_.getLevel(graph_, sourceId, destination.trackId);
            destination.active = sends_.isSendActive(graph_, sourceId, destination.trackId);
            destination.preFader = sends_.isSendPreFader(graph_, sourceId, destination.trackId);
            result.push_back(std::move(destination));
        }
        return result;
    }

    bool addSend(const TrackID& sourceId,
                 const TrackID& targetId,
                 float level,
                 bool preFader)
    {
        if (!validSendEndpoints(sourceId, targetId))
            return false;

        sends_.createSend(graph_, sourceId, targetId, juce::jlimit(0.0f, 2.0f, level));
        sendLevels_.setLevel(graph_, sourceId, targetId, level);
        sends_.setSendPreFader(graph_, sourceId, targetId, preFader);
        graph_.notifyGraphChanged();
        return true;
    }

    bool removeSend(const TrackID& sourceId, const TrackID& targetId)
    {
        if (sourceId.isEmpty() || targetId.isEmpty())
            return false;
        if (!sends_.sendExists(graph_, sourceId, targetId))
            return false;
        sends_.deleteSend(graph_, sourceId, targetId);
        graph_.notifyGraphChanged();
        return true;
    }

    bool setSendLevel(const TrackID& sourceId, const TrackID& targetId, float level)
    {
        if (!sends_.sendExists(graph_, sourceId, targetId))
            return false;
        sendLevels_.setLevel(graph_, sourceId, targetId, level);
        graph_.notifyGraphChanged();
        return true;
    }

    bool setSendPreFader(const TrackID& sourceId, const TrackID& targetId, bool preFader)
    {
        if (!sends_.sendExists(graph_, sourceId, targetId))
            return false;
        sends_.setSendPreFader(graph_, sourceId, targetId, preFader);
        graph_.notifyGraphChanged();
        return true;
    }

    juce::Colour getRoleColor(const QuickTrackRole& role) const
    {
        return builder_.getRoleColor(role);
    }

    bool isRoleColorManual(const QuickTrackRole& role) const
    {
        return builder_.isRoleColorManual(role);
    }

    void setManualRoleColor(const QuickTrackRole& role, const juce::Colour& color)
    {
        builder_.setManualRoleColor(role, color);
    }

    void clearManualRoleColor(const QuickTrackRole& role)
    {
        builder_.clearManualRoleColor(role);
    }

private:
    static std::vector<QuickTrackRequest> toBuilderRequests(const std::vector<QuickWorkflowRequest>& requests)
    {
        std::vector<QuickTrackRequest> result;
        result.reserve(requests.size());
        for (const auto& request : requests)
            result.push_back({ request.role, request.count, request.nameOverride });
        return result;
    }

    bool isBusTrack(const Track& track) const
    {
        if (track.getRole() == TrackRole::Bus)
            return true;
        if (auto* node = graph_.getNodeByTrackId(track.getID()))
            return node->type == RoutingNodeType::Bus;
        return false;
    }

    std::vector<TrackID> matchingTrackIds(const QuickTrackRole& role) const
    {
        std::vector<TrackID> result;
        for (int i = 0; i < tracks_.getNumTracks(); ++i)
        {
            auto* track = tracks_.getTrack(i);
            if (track == nullptr || track->getQuickRoleId() != juce::String(role.roleId))
                continue;
            if (QuickWorkflowRoleCatalog::isBusRole(role) != isBusTrack(*track))
                continue;
            result.push_back(track->getID());
        }
        return result;
    }

    TrackID findCanonicalBus(const QuickTrackRole& role) const
    {
        TrackID familyMatch;
        for (int i = 0; i < tracks_.getNumTracks(); ++i)
        {
            auto* track = tracks_.getTrack(i);
            if (track == nullptr || !isBusTrack(*track))
                continue;
            if (track->getQuickRoleId() == juce::String(role.roleId))
                return track->getID();
            if (familyMatch.isEmpty() && track->getQuickRoleId() == juce::String(role.roleId))
                familyMatch = track->getID();
        }
        return familyMatch;
    }

    bool routeSubgroup(const TrackID& sourceId, const TrackID& targetId,
                       QuickTrackBatchResult& result)
    {
        if (sourceId.isEmpty() || targetId.isEmpty() || sourceId == targetId)
            return false;
        if (!graph_.getNodeByTrackId(sourceId) || !graph_.getNodeByTrackId(targetId))
            return false;

        sends_.createSend(graph_, sourceId, targetId, 1.0f);
        masterRoute_.setMasterRouteState(sourceId, RouteState::ExistsInactive);

        const auto pair = std::make_pair(sourceId, targetId);
        if (std::find(result.routePairs.begin(), result.routePairs.end(), pair) == result.routePairs.end())
            result.routePairs.push_back(pair);
        return true;
    }

    void routeMatchingRole(const QuickTrackRole& role, QuickTrackBatchResult& result)
    {
        const char* targetRoleId = role.routesToRoleId;
        if (targetRoleId != nullptr && targetRoleId[0] != '\0')
        {
            if (const auto* targetRole = QuickWorkflowRoleCatalog::findById(targetRoleId))
            {
                const auto targetId = findCanonicalBus(*targetRole);
                for (const auto& sourceId : matchingTrackIds(role))
                    routeSubgroup(sourceId, targetId, result);
            }
        }

        const char* parentRoleId = role.preferredParentRoleId;
        if (parentRoleId != nullptr && parentRoleId[0] != '\0')
        {
            if (const auto* parentRole = QuickWorkflowRoleCatalog::findById(parentRoleId))
            {
                const auto parentId = findCanonicalBus(*parentRole);
                for (const auto& sourceId : matchingTrackIds(role))
                    routeSubgroup(sourceId, parentId, result);
            }
        }

        // A Quick Route request for a family bus is also an explicit request
        // to route the matching track family into that existing/reused bus.
        // Quick Track never enters this function, so creation remains
        // strictly create/ensure-only there.
        if (QuickWorkflowRoleCatalog::isBusRole(role))
        {
            const auto targetId = findCanonicalBus(role);
            if (targetId.isNotEmpty())
            {
                for (const auto& trackRole : QuickWorkflowRoleCatalog::trackRoles())
                {
                    if (trackRole.routesToRoleId == nullptr
                        || juce::String(trackRole.routesToRoleId) != role.roleId)
                        continue;
                    for (const auto& sourceId : matchingTrackIds(trackRole))
                        routeSubgroup(sourceId, targetId, result);
                }
            }
        }
    }

    bool validSendEndpoints(const TrackID& sourceId, const TrackID& targetId) const
    {
        if (sourceId.isEmpty() || targetId.isEmpty() || sourceId == targetId)
            return false;
        auto* source = tracks_.getTrack(sourceId);
        auto* target = tracks_.getTrack(targetId);
        return source != nullptr && target != nullptr && isBusTrack(*target);
    }

    QuickTrackBuilderCore builder_;
    TrackManager& tracks_;
    RoutingGraph& graph_;
    MasterRouteStateCore& masterRoute_;
    BubblegumSendStateCore& sends_;
    BubblegumSendLevelCore sendLevels_;
};

} // namespace DAW
