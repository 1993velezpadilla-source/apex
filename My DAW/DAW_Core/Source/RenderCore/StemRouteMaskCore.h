#pragma once

#include "ExportSettingsCore.h"
#include "../RoutingCore/RoutingSnapshot.h"
#include <memory>
#include <unordered_set>

namespace DAW {

/** Immutable control-plane route policy adopted only by an isolated offline
    render pass. This type intentionally has no AudioEngine dependency so route
    planning remains pure, lightweight, and directly regression-testable. */
struct OfflineStemRenderMask
{
    std::unordered_set<RouteID> programmeEdges;
    std::unordered_set<RouteID> sidechainEdges;
    TrackID targetTrackId;
};

class StemRouteMaskCore final
{
public:
    static std::shared_ptr<OfflineStemRenderMask> build(const RoutingSnapshot& snapshot,
                                                         const StemExportTarget& target)
    {
        auto mask = std::make_shared<OfflineStemRenderMask>();
        mask->targetTrackId = target.trackId;

        std::unordered_set<juce::String, RoutingSnapshot::StringHash> programmeNodes;
        for (const auto& sourceTrackId : target.sourceTrackIds)
            for (const auto& node : snapshot.nodes)
                if (node.active && node.trackId == sourceTrackId)
                    programmeNodes.insert(node.id);
        if (programmeNodes.empty())
            return {};

        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& edge : snapshot.edges)
            {
                if (!edge.active || edge.bypassed || edge.type == ConnectionType::Sidechain)
                    continue;
                if (programmeNodes.count(edge.sourceNodeId) == 0)
                    continue;
                mask->programmeEdges.insert(edge.id);
                if (programmeNodes.insert(edge.destNodeId).second)
                    changed = true;
            }
        }

        // Retain key paths required by selected/downstream processors while
        // excluding the key's unrelated programme path from the stem.
        auto dependencyNodes = programmeNodes;
        changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& edge : snapshot.edges)
            {
                if (!edge.active || edge.bypassed)
                    continue;
                if (edge.type == ConnectionType::Sidechain
                    && dependencyNodes.count(edge.destNodeId) > 0)
                {
                    if (mask->sidechainEdges.insert(edge.id).second)
                        changed = true;
                    if (dependencyNodes.insert(edge.sourceNodeId).second)
                        changed = true;
                }
                else if (edge.type != ConnectionType::Sidechain
                         && dependencyNodes.count(edge.destNodeId) > 0
                         && programmeNodes.count(edge.destNodeId) == 0)
                {
                    if (mask->programmeEdges.insert(edge.id).second)
                        changed = true;
                    if (dependencyNodes.insert(edge.sourceNodeId).second)
                        changed = true;
                }
            }
        }
        return mask;
    }
};

} // namespace DAW
