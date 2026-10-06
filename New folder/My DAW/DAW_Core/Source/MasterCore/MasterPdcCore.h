#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include "../RoutingCore/RoutingGraph.h"
#include "../PluginHostCore/PluginChainCore.h"

namespace DAW {

class MasterPdcCore
{
public:
    using ChainMap = std::map<TrackID, std::unique_ptr<PluginChainCore>>;

    void prepare(double, int blockSize)
    {
        blockSize_ = juce::jmax(1, blockSize);
        scratchL_.assign((size_t)blockSize_, 0.0f);
        scratchR_.assign((size_t)blockSize_, 0.0f);
    }

    void sync(const RoutingGraph& graph, const ChainMap* chains)
    {
        edgeDelays_.clear();
        int maxLatency = 0;
        struct EdgeLatency { RouteID id; int latency = 0; };
        std::vector<EdgeLatency> edges;

        for (auto* conn : graph.getAllConnections())
        {
            if (conn == nullptr || conn->type != ConnectionType::Direct || conn->destNodeId != "master")
                continue;
            auto* src = graph.getNode(conn->sourceNodeId);
            if (src == nullptr) continue;
            int latency = 0;
            if (chains != nullptr)
            {
                auto it = chains->find(src->trackId);
                if (it != chains->end() && it->second)
                    latency = it->second->totalLatencySamples();
            }
            edges.push_back({ conn->id, latency });
            maxLatency = juce::jmax(maxLatency, latency);
        }

        for (auto& e : edges)
        {
            const int delay = juce::jmax(0, maxLatency - e.latency);
            if (delay > 0)
            {
                auto& line = edgeDelays_[e.id];
                line.prepare(juce::jmax(blockSize_ * 4, delay + blockSize_ * 2));
                line.delaySamples = delay;
            }
        }
    }

    void reset()
    {
        for (auto& [id, line] : edgeDelays_)
            line.prepare(juce::jmax(1, line.buf.getNumSamples()));
        std::fill(scratchL_.begin(), scratchL_.end(), 0.0f);
        std::fill(scratchR_.begin(), scratchR_.end(), 0.0f);
    }

    void processEdge(const RouteID& id, float* L, float* R, int numSamples)
    {
        auto it = edgeDelays_.find(id);
        if (it == edgeDelays_.end() || it->second.delaySamples <= 0) return;
        if ((int)scratchL_.size() < numSamples)
        {
            scratchL_.assign((size_t)numSamples, 0.0f);
            scratchR_.assign((size_t)numSamples, 0.0f);
        }
        it->second.push(L, R, numSamples);
        it->second.read(scratchL_.data(), scratchR_.data(), numSamples);
        std::memcpy(L, scratchL_.data(), sizeof(float) * (size_t)numSamples);
        std::memcpy(R, scratchR_.data(), sizeof(float) * (size_t)numSamples);
    }

private:
    struct DelayLine
    {
        juce::AudioBuffer<float> buf;
        int writePos = 0;
        int delaySamples = 0;
        void prepare(int capacity)
        {
            buf.setSize(2, juce::jmax(1, capacity));
            buf.clear();
            writePos = 0;
        }
        void push(const float* L, const float* R, int n)
        {
            const int cap = buf.getNumSamples();
            auto* wL = buf.getWritePointer(0);
            auto* wR = buf.getWritePointer(1);
            for (int i = 0; i < n; ++i)
            {
                wL[writePos] = L[i];
                wR[writePos] = R[i];
                writePos = (writePos + 1) % cap;
            }
        }
        void read(float* L, float* R, int n) const
        {
            const int cap = buf.getNumSamples();
            const auto* rL = buf.getReadPointer(0);
            const auto* rR = buf.getReadPointer(1);
            for (int i = 0; i < n; ++i)
            {
                const int pos = (writePos - delaySamples - n + i + cap * 4) % cap;
                L[i] = rL[pos];
                R[i] = rR[pos];
            }
        }
    };

    struct StringHash { size_t operator()(const juce::String& s) const noexcept { return (size_t)s.hashCode64(); } };
    int blockSize_ = 512;
    std::unordered_map<RouteID, DelayLine, StringHash> edgeDelays_;
    std::vector<float> scratchL_, scratchR_;
};

} // namespace DAW
