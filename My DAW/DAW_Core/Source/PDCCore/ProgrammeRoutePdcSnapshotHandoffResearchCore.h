#pragma once
// RESEARCH ONLY: publish one CONTROL-PREPARED immutable RoutingSnapshot
// together with its matching mutable edge-delay bank as one coherent pointer.
// Readers MUST process using the snapshot and bank acquired from the SAME
// Reader scope. Do not independently load the application's existing Routing
// snapshot and this research plan: this would reintroduce torn-generation
// routing, especially when plugin latency changes without graph version bump.
//
// No APEX AudioEngine code uses this experimental owner. RT Reader acquisition
// has two seq_cst atomics and zero allocation/locking/destructor callbacks.
// Retired banks and graph copies are freed only in collectRetired() on control.
// Exactly ONE realtime writer may mutate each acquired DSP bank.
#include "ProgrammePdcRcuPublisherResearchCore.h"
#include "ProgrammeRoutePdcJuceDispatchResearchCore.h"
#include <cstdint>
#include <map>
#include <memory>

namespace DAW::PdcResearch {
class ProgrammeRoutePdcSnapshotHandoffResearchCore final {
public:
    struct PreparedGeneration final {
        RoutingSnapshot graph;
        ProgrammeRoutePdcJuceDispatchResearchCore dispatch;
        std::uint64_t publicationGeneration=0;
    };
    using Reader=ProgrammePdcRcuPublisherResearchCore<PreparedGeneration>::Reader;
    // Called by ONE serialized control thread only. Graph copy, validation,
    // all DSP allocation, mapping, and publication happen off audio thread.
    bool publishPrepared(const RoutingSnapshot& graph,
                         const std::map<TrackID,int>* chainLatencies,
                         int channels) {
        auto next=std::make_unique<PreparedGeneration>();
        next->graph=graph;
        // Control retains an RCU Reader for the duration of preparation,
        // including publication. DSP objects from unchanged route IDs can
        // share delay history with the prior generation safely: the route
        // metadata never mutates, and audio has only ONE DSP writer.
        auto previous=publisher_.readRealtime();
        if(!next->dispatch.prepare(
                next->graph,chainLatencies,channels,
                previous?&previous->dispatch:nullptr))
            return false;
        if(publicationGeneration_==UINT64_MAX)return false;
        next->publicationGeneration=publicationGeneration_+1;
        if(!publisher_.publishPrepared(std::move(next)))return false;
        ++publicationGeneration_;
        return true;
    }
    Reader readRealtime() noexcept{return publisher_.readRealtime();}
    std::size_t collectRetiredOnControlThread() {
        return publisher_.collectRetired();
    }
    std::size_t retainedGenerationsOnControlThread() const noexcept {
        return publisher_.retiredCountControlOnly();
    }
    std::uint64_t latestPublicationOnControlThread() const noexcept {
        return publicationGeneration_;
    }
private:
    ProgrammePdcRcuPublisherResearchCore<PreparedGeneration> publisher_;
    std::uint64_t publicationGeneration_=0;
};
} // namespace DAW::PdcResearch
