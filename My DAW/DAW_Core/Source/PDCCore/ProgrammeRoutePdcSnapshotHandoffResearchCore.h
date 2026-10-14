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
#include <cmath>
#include <map>
#include <memory>

namespace DAW::PdcResearch {
class ProgrammeRoutePdcSnapshotHandoffResearchCore final {
public:
    struct PreparedGeneration final {
        RoutingSnapshot graph;
        ProgrammeRoutePdcJuceDispatchResearchCore dispatch;
        std::uint64_t publicationGeneration=0;
        double sampleRate=0.0;
    };
    using Reader=ProgrammePdcRcuPublisherResearchCore<PreparedGeneration>::Reader;
    // Called by ONE serialized control thread only. Graph copy, validation,
    // all DSP allocation, mapping, and publication happen off audio thread.
    bool publishPrepared(const RoutingSnapshot& graph,
                         const std::map<TrackID,int>* chainLatencies,
                         int channels,double sampleRate=48000.0) {
        if(!std::isfinite(sampleRate)||sampleRate<8000.0||
           sampleRate>384000.0)return false;
        auto next=std::make_unique<PreparedGeneration>();
        next->sampleRate=sampleRate;
        next->graph=graph;
        // Serialized control thread reads the currently owned generation
        // without acquiring an artificial realtime Reader. Otherwise each
        // publication would prevent its own retirement collection, eventually
        // exhausting the 64-generation budget even when audio is stopped.
        // Metadata stays immutable; DSP state has a single realtime writer.
        // A sample-rate reprepare is a different timeline. Retaining the
        // previous ring across 44.1 <-> 48 kHz would replay stale samples,
        // even if the route ID and integer delay both remained unchanged.
        const auto* previous=publisher_.activeStateControlOnly();
        const auto* reusable=previous && previous->sampleRate==sampleRate
            ? &previous->dispatch : nullptr;
        if(!next->dispatch.prepare(
                next->graph,chainLatencies,channels,reusable))
            return false;
        if(publicationGeneration_==UINT64_MAX)return false;
        next->publicationGeneration=publicationGeneration_+1;
        if(!publisher_.publishPrepared(std::move(next)))return false;
        ++publicationGeneration_;
        return true;
    }
    // RT read-only, no allocations: RoutingGraph::publishSnapshotOnly() can
    // REPUBLISH active/bypass/tap/gain changes WITHOUT bumping graph.version.
    // Comparing version and counts alone would silently mix a new signal
    // graph with stale PDC taps. Gain is deliberately ignored: it changes
    // amplitude but not path timing. A mutated programme topology or tap is
    // forbidden until its corresponding control-prepared bank is published.
    static bool sameProgrammeRoutingRealtime(
        const RoutingSnapshot& live,const RoutingSnapshot& prepared) noexcept {
        if(live.version!=prepared.version ||
           live.nodes.size()!=prepared.nodes.size() ||
           live.edges.size()!=prepared.edges.size() ||
           live.processingOrder.size()!=prepared.processingOrder.size())
            return false;
        for(std::size_t i=0;i<live.processingOrder.size();++i)
            if(live.processingOrder[i]!=prepared.processingOrder[i])
                return false;
        for(std::size_t i=0;i<live.nodes.size();++i) {
            const auto& a=live.nodes[i];
            const auto& b=prepared.nodes[i];
            if(a.id!=b.id || a.trackId!=b.trackId ||
               a.type!=b.type || a.active!=b.active)
                return false;
        }
        for(std::size_t i=0;i<live.edges.size();++i) {
            const auto& a=live.edges[i];
            const auto& b=prepared.edges[i];
            if(a.id!=b.id || a.sourceNodeId!=b.sourceNodeId ||
               a.destNodeId!=b.destNodeId || a.type!=b.type ||
               a.active!=b.active || a.bypassed!=b.bypassed ||
               a.tapPoint!=b.tapPoint ||
               a.destinationPluginId!=b.destinationPluginId ||
               a.destinationBusIndex!=b.destinationBusIndex)
                return false;
        }
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
