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
                 const std::map<TrackID,int>* chainLatencies,int channels) {
        ProgrammeRoutePdcPlannerResearchCore plan;
        if(!ProgrammeRoutePdcJuceResearchBridge::build(
                snapshot,chainLatencies,plan))return false;
        auto next=std::make_unique<ProgrammeRoutePdcPreparedResearchCore>();
        if(!next->prepare(plan,channels))return false;
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
        // Commit only after EVERYTHING prepared. A failed replan keeps
        // the prior bank and exact route order intact.
        bank_.swap(next);
        edges_.swap(nextEdges);
        locals_.swap(nextLocal);
        version_=snapshot.version;
        return true;
    }
    std::uint64_t version() const noexcept{return version_;}
    int channels() const noexcept{return bank_?bank_->channels():0;}
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
    std::uint64_t version_=0;
};
} // namespace DAW::PdcResearch
