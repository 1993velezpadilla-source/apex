#pragma once
#include <JuceHeader.h>
#include <map>
#include <unordered_map>
#include <vector>
#include "../RoutingCore/RoutingSnapshot.h"
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 *  Master-path Plugin Delay Compensation.
 *
 *  sync() runs on the audio thread when the master-PDC plan is dirty
 *  (topology change or plugin-chain latency change). It consumes ONLY
 *  published state — the adopted RoutingSnapshot plus the atomically
 *  published per-track latency map — and updates delay lines in place:
 *
 *   - no iteration of the live RoutingGraph or pluginChains_ map
 *     (previously a data race with message-thread mutation);
 *   - no mass clear()/free of delay lines (previously every sync freed
 *     and zeroed all line buffers inside the audio callback — an
 *     allocation burst AND a guaranteed hard discontinuity);
 *   - line buffers grow only when a larger delay actually requires it;
 *     capacity is derived from the engine worst-case block (8192) so a
 *     line created during a small-block session can never under-serve a
 *     later larger block (previously a latent out-of-bounds read when
 *     delay+numSamples exceeded 4x capacity, producing a negative ring
 *     offset).
 */
class MasterPdcCore
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        blockSize_ = juce::jmax(1, blockSize);
        pdcFadeSamples_ = juce::jmax(1, (int) (0.005 * juce::jmax(1.0, sampleRate)));
        scratchL_.assign((size_t)blockSize_, 0.0f);
        scratchR_.assign((size_t)blockSize_, 0.0f);
    }

    void sync(const RoutingSnapshot& snapshot,
              const std::map<TrackID, int>* latencies,
              int numSamples)
    {
        // Disable every existing line (keeps buffers alive for reuse —
        // processEdge() early-outs on delaySamples <= 0).
        for (auto& [id, line] : edgeDelays_)
            line.delaySamples = 0;

        // Compute the required per-edge latencies (desired_ is a reused
        // member — no allocation after the first sync at a given graph size).
        desired_.clear();
        int maxLatency = 0;
        for (const auto& e : snapshot.edges)
        {
            if (e.type != ConnectionType::Direct || e.destNodeId != "master")
                continue;
            int latency = 0;
            if (latencies != nullptr)
            {
                const auto nodeIt = snapshot.nodeIndexById.find(e.sourceNodeId);
                if (nodeIt != snapshot.nodeIndexById.end()
                    && nodeIt->second < snapshot.nodes.size())
                {
                    const auto latIt = latencies->find(snapshot.nodes[nodeIt->second].trackId);
                    if (latIt != latencies->end())
                        latency = juce::jmax(0, latIt->second);
                }
            }
            desired_.push_back({ e.id, latency });
            maxLatency = juce::jmax(maxLatency, latency);
        }

        // Apply in place. Lines are created only for edges that actually
        // need compensation; existing lines keep their history when the
        // capacity still fits.
        const int safeBlock = juce::jmax(juce::jmax(blockSize_, 8192), numSamples);
        for (const auto& d : desired_)
        {
            const int delay = juce::jmax(0, maxLatency - d.latency);
            if (delay <= 0)
                continue;
            auto& line = edgeDelays_[d.id];
            const int capacity = juce::jmax(safeBlock * 4, delay + safeBlock * 2);
            if (line.buf.getNumSamples() < capacity)
                line.prepare(capacity);   // rare grow: reallocates + clears THIS line only
            line.setCrossfadeSamples(pdcFadeSamples_);
            line.delaySamples = delay;
        }
    }

    void reset()
    {
        for (auto& [id, line] : edgeDelays_)
            line.prepare(juce::jmax(1, line.buf.getNumSamples()));
        std::fill(scratchL_.begin(), scratchL_.end(), 0.0f);
        std::fill(scratchR_.begin(), scratchR_.end(), 0.0f);
    }

    /** Returns the stored compensation delay for an edge (0 if none). */
    int getEdgeDelay(const RouteID& id) const noexcept
    {
        auto it = edgeDelays_.find(id);
        return it != edgeDelays_.end() ? it->second.delaySamples : 0;
    }

    /** Process one master-bound edge through its PDC delay line.
     *  effectiveDelayOverride >= 0 overrides the stored delay for THIS block
     *  only (monitoring-PDC Bypass/Reduced); the ring is always fed, so
     *  history stays continuous, and any effective-delay change crossfades
     *  (~5 ms, C7) instead of stepping. */
    void processEdge(const RouteID& id, float* L, float* R, int numSamples, int effectiveDelayOverride = -1)
    {
        auto it = edgeDelays_.find(id);
        if (it == edgeDelays_.end()
            || (effectiveDelayOverride < 0 && it->second.delaySamples <= 0))
            return;
        if ((int)scratchL_.size() < numSamples)
        {
            scratchL_.assign((size_t)numSamples, 0.0f);
            scratchR_.assign((size_t)numSamples, 0.0f);
        }
        const int effectiveDelay = effectiveDelayOverride >= 0
            ? effectiveDelayOverride : it->second.delaySamples;
        it->second.push(L, R, numSamples);
        it->second.read(scratchL_.data(), scratchR_.data(), numSamples, effectiveDelay);
        std::memcpy(L, scratchL_.data(), sizeof(float) * (size_t)numSamples);
        std::memcpy(R, scratchR_.data(), sizeof(float) * (size_t)numSamples);
    }

private:
    struct DelayLine
    {
        juce::AudioBuffer<float> buf;
        int writePos = 0;
        int delaySamples = 0;
        int primed = 0;
        int currentReadDelay = -1;
        int previousReadDelay = 0;
        int fadeRemaining = 0;
        int fadeSamples = 0;

        void prepare(int capacity)
        {
            buf.setSize(2, juce::jmax(1, capacity));
            buf.clear();
            writePos = 0;
            primed = 0;
            currentReadDelay = -1;
            previousReadDelay = 0;
            fadeRemaining = 0;
        }
        void setCrossfadeSamples(int n) noexcept { fadeSamples = juce::jmax(0, n); }
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
            primed = juce::jmin(cap, primed + n);
        }
        /** C7: read at an explicit effective delay; any effective-delay
         *  change after the line has history arms a ~5 ms equal-gain
         *  crossfade (covers PDC resyncs and monitoring bypass toggles). */
        void read(float* L, float* R, int n, int effectiveDelay) noexcept
        {
            const int cap = buf.getNumSamples();
            effectiveDelay = juce::jlimit(0, juce::jmax(0, cap - n - 1), effectiveDelay);

            if (currentReadDelay < 0)
            {
                currentReadDelay = effectiveDelay;
            }
            else if (effectiveDelay != currentReadDelay)
            {
                if (primed > 0 && fadeSamples > 0)
                {
                    previousReadDelay = currentReadDelay;
                    fadeRemaining = fadeSamples;
                }
                currentReadDelay = effectiveDelay;
            }

            const auto* rL = buf.getReadPointer(0);
            const auto* rR = buf.getReadPointer(1);
            for (int i = 0; i < n; ++i)
            {
                const int posNew = (writePos - currentReadDelay - n + i + cap * 4) % cap;
                if (fadeRemaining > 0)
                {
                    const int posOld = (writePos - previousReadDelay - n + i + cap * 4) % cap;
                    const float t = 1.0f - (float) fadeRemaining / (float) fadeSamples;
                    L[i] = rL[posOld] * (1.0f - t) + rL[posNew] * t;
                    R[i] = rR[posOld] * (1.0f - t) + rR[posNew] * t;
                    --fadeRemaining;
                }
                else
                {
                    L[i] = rL[posNew];
                    R[i] = rR[posNew];
                }
            }
        }
        void read(float* L, float* R, int n) noexcept
        {
            read(L, R, n, delaySamples);
        }
    };

    struct EdgeLatency
    {
        RouteID id;
        int     latency = 0;
    };

    struct StringHash { size_t operator()(const juce::String& s) const noexcept { return (size_t)s.hashCode64(); } };
    int blockSize_ = 512;
    int pdcFadeSamples_ = 0;
    std::unordered_map<RouteID, DelayLine, StringHash> edgeDelays_;
    std::vector<EdgeLatency> desired_;
    std::vector<float> scratchL_, scratchR_;
};

} // namespace DAW
