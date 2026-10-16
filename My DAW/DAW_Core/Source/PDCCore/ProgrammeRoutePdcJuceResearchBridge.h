#pragma once
// APEX RoutingSnapshot -> research programme PDC plan.
// CONTROL PLANE ONLY: caller must publish an immutable plan, then separately
// prepare/retire the required per-edge delay lines outside realtime callback.
// This adapter intentionally does not change AudioEngine or MasterPdcCore.
#include "ProgrammeRoutePdcPlannerResearchCore.h"
#include "../RoutingCore/RoutingSnapshot.h"
#include "../UtilityCore/Types.h"
#include <map>

namespace DAW::PdcResearch {
class ProgrammeRoutePdcJuceResearchBridge final {
public:
    static bool build(const RoutingSnapshot& snapshot,
                      const std::map<TrackID,int>* chainLatencies,
                      ProgrammeRoutePdcPlannerResearchCore& planner) {
        std::vector<Node> nodes;
        std::vector<Edge> edges;
        std::vector<std::string> ordered;
        nodes.reserve(snapshot.nodes.size());
        edges.reserve(snapshot.edges.size());
        ordered.reserve(snapshot.processingOrder.size());
        for(const auto& node:snapshot.nodes) {
            int latency=0;
            if(chainLatencies != nullptr) {
                const auto it=chainLatencies->find(node.trackId);
                if(it!=chainLatencies->end())latency=it->second;
            }
            nodes.push_back({
                node.id.toStdString(),latency,
                node.type==RoutingNodeType::Track,
                node.active});
        }
        for(const auto& edge:snapshot.edges) {
            const auto kind=[](ConnectionType type) {
                switch(type) {
                    case ConnectionType::Direct: return EdgeKind::Direct;
                    case ConnectionType::Send: return EdgeKind::Send;
                    case ConnectionType::PreSend: return EdgeKind::PreSend;
                    case ConnectionType::FolderSum: return EdgeKind::FolderSum;
                    case ConnectionType::Sidechain: return EdgeKind::Sidechain;
                }
                return EdgeKind::Sidechain;
            }(edge.type);
            edges.push_back({
                edge.id.toStdString(),
                edge.sourceNodeId.toStdString(),
                edge.destNodeId.toStdString(),kind,edge.active,
                edge.bypassed});
        }
        for(const auto& id:snapshot.processingOrder)
            ordered.push_back(id.toStdString());
        return planner.build(nodes,edges,ordered);
    }
};
} // namespace DAW::PdcResearch
