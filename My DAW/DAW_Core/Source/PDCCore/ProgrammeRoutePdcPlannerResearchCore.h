#pragma once
// APEX programme-routing PDC research. CONTROL-THREAD PLAN ONLY.
// Not wired to AudioEngine; never allocate/build plans in an audio callback.
// APEX currently compensates direct-to-master edges, but arbitrary parallel
// direct/send/pre-send/folder-bus reconvergence needs path-aware alignment.
// This pure C++17 planner operates on a materialized immutable DAG snapshot.
// RoutingGraph sidechains are EXCLUDED: their key/tap timing is a separate
// semantic contract, not an audible parallel path.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace DAW::PdcResearch {

enum class EdgeKind { Direct, Send, PreSend, FolderSum, Sidechain };
struct Node {
    std::string id;
    int ownProcessorLatency=0;
    bool hasIndependentInput=false; // track clips/monitor, not an empty aux bus
    bool active=true;
};
struct Edge {
    std::string id;
    std::string source;
    std::string destination;
    EdgeKind kind=EdgeKind::Direct;
    bool active=true;
    bool bypassed=false;
};
struct NodePlan {
    std::string id;
    int arrivalLatency=0;
    int localInputDelay=0;
    int outputLatency=0;
};
struct EdgePlan {
    std::string id;
    int delaySamples=0;
    int sourceOutputLatency=0;
    int destinationArrivalLatency=0;
};
class ProgrammeRoutePdcPlannerResearchCore final {
public:
    static constexpr int maxLatencySamples=131072;
    bool build(const std::vector<Node>& nodes,const std::vector<Edge>& edges,
               const std::vector<std::string>& topoOrder) {
        clear();
        if(nodes.empty()||topoOrder.size()!=nodes.size())
            return fail("empty graph or non-exhaustive topological order");
        std::unordered_map<std::string,std::size_t> nodeIndex;
        nodeIndex.reserve(nodes.size());
        for(std::size_t i=0;i<nodes.size();++i) {
            if(nodes[i].id.empty()||!nodeIndex.emplace(nodes[i].id,i).second)
                return fail("duplicate or empty node ID");
            if(nodes[i].ownProcessorLatency<0 ||
               nodes[i].ownProcessorLatency>maxLatencySamples)
                return fail("invalid node processor latency");
        }
        std::unordered_map<std::string,bool> edgeIds;
        edgeIds.reserve(edges.size());
        for(const auto& edge:edges) {
            if(edge.id.empty()||!edgeIds.emplace(edge.id,true).second)
                return fail("duplicate or empty route ID");
            if(nodeIndex.find(edge.source)==nodeIndex.end() ||
               nodeIndex.find(edge.destination)==nodeIndex.end())
                return fail("edge references nonexistent node");
        }
        std::unordered_map<std::string,std::size_t> topo;
        topo.reserve(topoOrder.size());
        for(std::size_t pos=0;pos<topoOrder.size();++pos)
            if(nodeIndex.find(topoOrder[pos])==nodeIndex.end() ||
               !topo.emplace(topoOrder[pos],pos).second)
                return fail("topological order contains unknown/duplicate node");
        for(const auto& edge:edges) {
            if(!participates(edge,nodes,nodeIndex))continue;
            if(topo.at(edge.source)>=topo.at(edge.destination))
                return fail("programme route violates topological order/cycle");
        }

        // Index participating incoming programme routes once on the control
        // thread. The previous full edge scan per node was O(nodes * edges),
        // making large send-heavy sessions expensive to prepare.
        std::vector<std::vector<std::size_t>> incoming(nodes.size());
        for(std::size_t i=0;i<edges.size();++i) {
            if(!participates(edges[i],nodes,nodeIndex))continue;
            incoming[nodeIndex.at(edges[i].destination)].push_back(
                nodeIndex.at(edges[i].source));
        }
        nodePlans_.resize(nodes.size());
        for(const auto& name:topoOrder) {
            const auto nodeIdx=nodeIndex.at(name);
            const auto& node=nodes[nodeIdx];
            int incomingMax=0;
            for(const auto src:incoming[nodeIdx])
                incomingMax=std::max(incomingMax,nodePlans_[src].outputLatency);
            auto& result=nodePlans_[nodeIdx];
            result.id=name;
            result.arrivalLatency=incomingMax;
            result.localInputDelay=node.hasIndependentInput?incomingMax:0;
            if(incomingMax>maxLatencySamples-node.ownProcessorLatency)
                return fail("accumulated programme path exceeds delay capacity");
            result.outputLatency=node.active?
                incomingMax+node.ownProcessorLatency:0;
        }
        edgePlans_.reserve(edges.size());
        for(const auto& edge:edges) {
            if(!participates(edge,nodes,nodeIndex))continue;
            const auto src=nodeIndex.at(edge.source);
            const auto dst=nodeIndex.at(edge.destination);
            const int srcLatency=nodePlans_[src].outputLatency;
            const int dstArrival=nodePlans_[dst].arrivalLatency;
            if(dstArrival<srcLatency)
                return fail("negative route compensation: corrupt plan");
            edgePlans_.push_back({
                edge.id,dstArrival-srcLatency,srcLatency,dstArrival});
        }
        valid_=true;
        return true;
    }
    bool valid() const noexcept{return valid_;}
    const std::string& error() const noexcept{return error_;}
    const std::vector<NodePlan>& nodePlans() const noexcept{return nodePlans_;}
    const std::vector<EdgePlan>& edgePlans() const noexcept{return edgePlans_;}
    const NodePlan* findNode(const std::string& id) const noexcept {
        for(const auto& v:nodePlans_)if(v.id==id)return &v;
        return nullptr;
    }
    const EdgePlan* findEdge(const std::string& id) const noexcept {
        for(const auto& v:edgePlans_)if(v.id==id)return &v;
        return nullptr;
    }
private:
    static bool participates(const Edge& edge,const std::vector<Node>& nodes,
             const std::unordered_map<std::string,std::size_t>& index) {
        return edge.kind!=EdgeKind::Sidechain && edge.active && !edge.bypassed &&
            nodes[index.at(edge.source)].active &&
            nodes[index.at(edge.destination)].active;
    }
    bool fail(const char* message) {
        nodePlans_.clear();edgePlans_.clear();error_=message;valid_=false;
        return false;
    }
    void clear() {
        nodePlans_.clear();edgePlans_.clear();error_.clear();valid_=false;
    }
    bool valid_=false;
    std::string error_;
    std::vector<NodePlan> nodePlans_;
    std::vector<EdgePlan> edgePlans_;
};
} // namespace DAW::PdcResearch
