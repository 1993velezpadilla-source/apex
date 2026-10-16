#pragma once
// RESEARCH ONLY: pre-resolved RoutingSnapshot edge-index -> prepared PDC delay.
// Not used in AudioEngine and not safe to swap while an audio callback reads
// it. Control thread must prepare a replacement, drain readers and retire the
// old dispatch without freeing memory on the audio thread.
//
// This is the boundary for future audio-engine wiring: the RT entrypoint gets
// the already-adopted immutable snapshot VERSION and its edge INDEX, never
// searches a route string, walks the live RoutingGraph, allocates or locks.
#include "ProgrammeRoutePdcJuceResearchBridge.h"
#include "ProgrammeRoutePdcPreparedResearchCore.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace DAW::PdcResearch {
class ProgrammeRoutePdcJuceDispatchResearchCore final {
public:
    static constexpr std::size_t invalidIndex=
        ProgrammeRoutePdcPreparedResearchCore::invalidIndex;
    bool prepare(const RoutingSnapshot& snapshot,
                 const std::map<TrackID,int>* chainLatencies,int channels,
                 const ProgrammeRoutePdcJuceDispatchResearchCore* old=nullptr) {
        ProgrammeRoutePdcPlannerResearchCore plan;
        if(!ProgrammeRoutePdcJuceResearchBridge::build(
                snapshot,chainLatencies,plan))return false;
        auto next=std::make_unique<ProgrammeRoutePdcPreparedResearchCore>();
        // History may be retained ONLY for a same-identity unchanged
        // programme route. A recycled ID, changed endpoint/type/tap, or
        // changed latency gets a NEW line instead of stale audio.
        std::vector<std::string> compatible;
        if(old!=nullptr && old!=this && old->bank_) {
            compatible.reserve(snapshot.edges.size());
            for(const auto& edge:snapshot.edges) {
                if(edge.type==ConnectionType::Sidechain||!edge.active||
                   edge.bypassed)continue;
                if(plan.findEdge(edge.id.toStdString())==nullptr)continue;
                for(const auto& earlier:old->routeMetadata_) {
                    if(earlier.id==edge.id &&
                       earlier.sourceNodeId==edge.sourceNodeId &&
                       earlier.destNodeId==edge.destNodeId &&
                       earlier.type==edge.type &&
                       earlier.tapPoint==edge.tapPoint &&
                       earlier.destinationPluginId==edge.destinationPluginId &&
                       earlier.destinationBusIndex==edge.destinationBusIndex &&
                       earlier.active&&!earlier.bypassed) {
                        compatible.push_back(edge.id.toStdString());
                        break;
                    }
                }
            }
        }
        if(!next->prepare(plan,channels,
                          old!=nullptr&&old!=this?old->bank_.get():nullptr,
                          &compatible))return false;
        std::vector<std::size_t> nextEdges(snapshot.edges.size(),invalidIndex);
        for(std::size_t i=0;i<snapshot.edges.size();++i) {
            const auto& edge=snapshot.edges[i];
            if(edge.type==ConnectionType::Sidechain||!edge.active||
               edge.bypassed)continue;
            // The planner omits edges to/from inactive nodes. Such edges
            // must be rejected by the realtime dispatcher, not sent dry.
            const auto* item=plan.findEdge(edge.id.toStdString());
            if(item==nullptr)continue;
            const auto index=next->findEdgeIndex(item->id);
            if(index==invalidIndex)return false;
            nextEdges[i]=index;
        }
        std::vector<std::size_t> nextLocal(snapshot.nodes.size(),invalidIndex);
        for(std::size_t i=0;i<snapshot.nodes.size();++i) {
            const auto& node=snapshot.nodes[i];
            const auto* planned=plan.findNode(node.id.toStdString());
            if(planned==nullptr)continue;
            if(planned->localInputDelay<=0)continue;
            const auto index=next->findLocalInputIndex(planned->id);
            if(index==invalidIndex)return false;
            nextLocal[i]=index;
        }
        // Copy metadata BEFORE publication, so an allocation exception
        // cannot leave the old bank combined with new index bindings.
        std::vector<RoutingSnapshot::EdgeSnapshot> nextMetadata=snapshot.edges;
        // All swaps are non-throwing. No partial graph generation escapes.
        bank_.swap(next);
        edges_.swap(nextEdges);
        locals_.swap(nextLocal);
        routeMetadata_.swap(nextMetadata);
        version_=snapshot.version;
        return true;
    }
    std::uint64_t version() const noexcept{return version_;}
    int channels() const noexcept{return bank_?bank_->channels():0;}
    // Called once per realtime block before opting into graph-wide PDC.
    // The audio engine must NEVER mix a partly prepared graph plan with
    // legacy/direct routing inside one block: every audible programme edge
    // has to be mapped to its prepared delay (including zero-delay taps).
    // This is a bounded index-only scan: no allocations, locks or hashes.
    bool coversAllActiveProgrammeEdges(const RoutingSnapshot& graph) const noexcept {
        if(!bank_ || graph.version!=version_ ||
           graph.edges.size()!=edges_.size())return false;
        for(std::size_t i=0;i<graph.edges.size();++i) {
            const auto& edge=graph.edges[i];
            if(edge.active && !edge.bypassed &&
               edge.type!=ConnectionType::Sidechain &&
               edges_[i]==invalidIndex)
                return false;
        }
        return true;
    }
    int edgeDelay(std::size_t edgePosition) const noexcept {
        if(!bank_||edgePosition>=edges_.size() ||
           edges_[edgePosition]==invalidIndex)return -1;
        return bank_->edgeDelaySamples(edges_[edgePosition]);
    }
    int localDelay(std::size_t nodePosition) const noexcept {
        if(!bank_||nodePosition>=locals_.size() ||
           locals_[nodePosition]==invalidIndex)return -1;
        return bank_->localDelaySamples(locals_[nodePosition]);
    }
    bool processEdge(std::uint64_t snapshotVersion,std::size_t edgePosition,
                     float* const* data,int channels,int samples) noexcept {
        if(snapshotVersion!=version_||!bank_ ||
           edgePosition>=edges_.size() ||
           edges_[edgePosition]==invalidIndex)return false;
        return bank_->processEdge(edges_[edgePosition],data,channels,samples);
    }
    bool processLocalInput(std::uint64_t snapshotVersion,
                           std::size_t nodePosition,float* const* data,
                           int channels,int samples) noexcept {
        if(snapshotVersion!=version_||!bank_||
           nodePosition>=locals_.size()||
           locals_[nodePosition]==invalidIndex)return false;
        return bank_->processLocalInput(locals_[nodePosition],
                                        data,channels,samples);
    }
    void resetAfterReadersDrain() noexcept {
        if(bank_)bank_->reset();
    }
private:
    std::unique_ptr<ProgrammeRoutePdcPreparedResearchCore> bank_;
    std::vector<std::size_t> edges_;
    std::vector<std::size_t> locals_;
    std::vector<RoutingSnapshot::EdgeSnapshot> routeMetadata_;
    std::uint64_t version_=0;
};
} // namespace DAW::PdcResearch
