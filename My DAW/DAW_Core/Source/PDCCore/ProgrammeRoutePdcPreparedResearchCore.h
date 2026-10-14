#pragma once
// APEX programme-route latency research: prepared per-edge audio delays.
// NOT wired into AudioEngine or shipping MasterPdcCore.
// Control plane owns prepare(), publication and quiescence. The realtime
// reader only uses pre-resolved integer indices, never strings or maps.
// Delay lines allocate during prepare(); processEdge/processLocalInput NEVER
// allocate, lock, throw, publish, or reconfigure themselves.
#include "ProgrammeRoutePdcPlannerResearchCore.h"
#include "../PluginHostCore/PluginParallelDryDelayResearchCore.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace DAW::PdcResearch {
class ProgrammeRoutePdcPreparedResearchCore final {
public:
    static constexpr std::size_t invalidIndex=static_cast<std::size_t>(-1);
    bool prepare(const ProgrammeRoutePdcPlannerResearchCore& plan,int channels) {
        if(!plan.valid()||channels<1||channels>32)return false;
        std::vector<EdgeDelay> newEdges;
        std::vector<LocalInputDelay> newInputs;
        newEdges.reserve(plan.edgePlans().size());
        newInputs.reserve(plan.nodePlans().size());
        for(const auto& edge:plan.edgePlans()) {
            if(edge.delaySamples<0 ||
               edge.delaySamples>ProgrammeRoutePdcPlannerResearchCore::maxLatencySamples)
                return false;
            newEdges.emplace_back();
            auto& target=newEdges.back();
            target.id=edge.id;
            target.delaySamples=edge.delaySamples;
            if(!target.line.prepare(channels,edge.delaySamples))return false;
        }
        for(const auto& node:plan.nodePlans()) {
            if(node.localInputDelay<0 ||
               node.localInputDelay>ProgrammeRoutePdcPlannerResearchCore::maxLatencySamples)
                return false;
            if(node.localInputDelay==0)continue;
            newInputs.emplace_back();
            auto& target=newInputs.back();
            target.id=node.id;
            target.delaySamples=node.localInputDelay;
            if(!target.line.prepare(channels,node.localInputDelay))return false;
        }
        // Atomic with respect to the *control-thread* transaction: on
        // failed preparation, existing banks remain untouched. Caller MUST
        // drain audio readers before swapping or publish a separately owned
        // bank via immutable snapshot; this class is not a concurrency API.
        edges_.swap(newEdges);
        localInputs_.swap(newInputs);
        channels_=channels;
        return true;
    }
    void reset() noexcept {
        for(auto& e:edges_)e.line.reset();
        for(auto& n:localInputs_)n.line.reset();
    }
    std::size_t edgeCount() const noexcept {return edges_.size();}
    int channels() const noexcept {return channels_;}
    std::size_t findEdgeIndex(const std::string& id) const noexcept {
        for(std::size_t i=0;i<edges_.size();++i)
            if(edges_[i].id==id)return i;
        return invalidIndex;
    }
    std::size_t findLocalInputIndex(const std::string& id) const noexcept {
        for(std::size_t i=0;i<localInputs_.size();++i)
            if(localInputs_[i].id==id)return i;
        return invalidIndex;
    }
    int edgeDelaySamples(std::size_t index) const noexcept {
        return index<edges_.size()?edges_[index].delaySamples:-1;
    }
    int localDelaySamples(std::size_t index) const noexcept {
        return index<localInputs_.size()?localInputs_[index].delaySamples:-1;
    }
    // Note: cannot process the same edge twice in one callback, or reuse one
    // line for two sources. Exactly one ring/history is owned PER EDGE ID.
    bool processEdge(std::size_t index,float* const* data,
                     int channels,int samples) noexcept {
        if(index>=edges_.size()||!validBlock(data,channels,samples))return false;
        edges_[index].line.process(data,channels,samples);
        return true;
    }
    bool processLocalInput(std::size_t index,float* const* data,
                           int channels,int samples) noexcept {
        if(index>=localInputs_.size()||!validBlock(data,channels,samples))return false;
        localInputs_[index].line.process(data,channels,samples);
        return true;
    }
private:
    bool validBlock(float* const* data,int channels,int samples) const noexcept {
        if(!data||channels!=channels_||samples<=0)return false;
        for(int ch=0;ch<channels;++ch)if(!data[ch])return false;
        return true;
    }
    struct EdgeDelay {
        std::string id;
        int delaySamples=0;
        PluginParallelDryDelayResearchCore line;
    };
    struct LocalInputDelay {
        std::string id;
        int delaySamples=0;
        PluginParallelDryDelayResearchCore line;
    };
    std::vector<EdgeDelay> edges_;
    std::vector<LocalInputDelay> localInputs_;
    int channels_=0;
};
} // namespace DAW::PdcResearch
