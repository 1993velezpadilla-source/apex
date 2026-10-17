#pragma once
#include <JuceHeader.h>
#include <map>
#include <atomic>
#include <unordered_map>
#include <vector>
#include "../RoutingCore/RoutingSnapshot.h"
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 *  Master-path Plugin Delay Compensation.
 *
 *  sync() is a CONTROL/TEST-only allocation-permitted prewarm.
 *  syncRealtime() is the audio-callback fallback and NEVER creates or
 *  grows delay lines: an unprepared edge explicitly renders uncompensated
 *  until the control plane can stage a safely prepared bank. Both use ONLY
 *  the adopted immutable RoutingSnapshot and published chain latencies:
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

    // No realtime callback may call this allocation-permitted method.
    void sync(const RoutingSnapshot& snapshot,
              const std::map<TrackID, int>* latencies,
              int numSamples)
    {
        syncImpl(snapshot,latencies,numSamples,false);
    }

    // Recompute already-prepared lines without allocating or resetting their
    // buffers. Missing/new/too-long edges remain uncompensated for this block;
    // the fallback deliberately prioritizes glitch-free live monitoring.
    void syncRealtime(const RoutingSnapshot& snapshot,
                      const std::map<TrackID,int>* latencies,
                      int numSamples) noexcept
    {
        syncImpl(snapshot,latencies,numSamples,true);
    }

    std::uint64_t getRealtimeUnpreparedEdgeCount() const noexcept
    {
        return realtimeUnpreparedEdges_.load(std::memory_order_relaxed);
    }

private:
    void syncImpl(const RoutingSnapshot& snapshot,
                  const std::map<TrackID,int>* latencies,
                  int numSamples,bool realtime)
    {
        // Mark active compensation edges without dropping any delay's
        // history yet. A route that stops being audible will be invalidated
        // AFTER the new plan is known. Otherwise a bypassed route could
        // later replay audio from before it was deactivated.
        for (auto& [id, line] : edgeDelays_)
            line.usedInCurrentSync = false;

        // Compute the target in two allocation-free passes over the
        // immutable routing snapshot. The former desired_ vector grew in
        // sync() on the audio callback when a larger graph was published.
        // Do not copy edge IDs or allocate a temporary edge plan here.
        const auto latencyOfMasterEdge=[&](const RoutingSnapshot::EdgeSnapshot& e) -> int
        {
            if(e.type!=ConnectionType::Direct || e.destNodeId!="master"
                || !e.active || e.bypassed)
                return -1;
            const auto nodeIt=snapshot.nodeIndexById.find(e.sourceNodeId);
            if(nodeIt==snapshot.nodeIndexById.end() ||
               nodeIt->second>=snapshot.nodes.size() ||
               !snapshot.nodes[nodeIt->second].active)
                return -1;
            if(latencies==nullptr)return 0;
            const auto found=latencies->find(snapshot.nodes[nodeIt->second].trackId);
            return found!=latencies->end()?juce::jmax(0,found->second):0;
        };
        int maxLatency=0;
        for(const auto& e:snapshot.edges)
            if(const int latency=latencyOfMasterEdge(e);latency>=0)
                maxLatency=juce::jmax(maxLatency,latency);

        // Recompute only the edges that actually need a master delay.
        // Ring creation/growth for a *new* route still needs a prepared
        // control-thread owner before this fallback can be RT-certified.
        const int safeBlock=juce::jmax(juce::jmax(blockSize_,8192),numSamples);
        for(const auto& e:snapshot.edges)
        {
            const int latency=latencyOfMasterEdge(e);
            if(latency<0)continue;
            const int delay=juce::jmax(0,maxLatency-latency);
            if(delay<=0)continue;
            // std::unordered_map insertion and AudioBuffer::setSize can
            // allocate: both are forbidden in the callback. The RT path
            // must only access an existing prewarmed ring. A previously
            // prepared ring needs delay+1 frames; processEdge chunks larger
            // callback blocks against its existing scratch capacity.
            auto it=edgeDelays_.find(e.id);
            if(it==edgeDelays_.end())
            {
                if(realtime) {
                    realtimeUnpreparedEdges_.fetch_add(1,std::memory_order_relaxed);
                    continue;
                }
                it=edgeDelays_.try_emplace(e.id).first;
            }
            auto& line=it->second;
            const int capacity=juce::jmax(safeBlock*4,delay+safeBlock*2);
            if(line.buf.getNumSamples()< (realtime ? delay+2 : capacity))
            {
                if(realtime) {
                    realtimeUnpreparedEdges_.fetch_add(1,std::memory_order_relaxed);
                    continue;
                }
                line.prepare(capacity);
            }
            line.setCrossfadeSamples(pdcFadeSamples_);
            line.delaySamples=delay;
            line.usedInCurrentSync=true;
        }

        // Inactive/bypassed or now zero-compensation paths stop advancing.
        // Invalidate only their freshness; clearing a large JUCE AudioBuffer
        // in the callback would be both unnecessary and expensive. Preserve
        // active nonzero paths so latency edits retain their smooth crossfade.
        for (auto& [id, line] : edgeDelays_)
            if (!line.usedInCurrentSync)
            {
                if (line.delaySamples > 0)
                {
                    line.primed = 0;
                    line.currentReadDelay = -1;
                    line.previousReadDelay = 0;
                    line.fadeRemaining = 0;
                }
                line.delaySamples = 0;
            }
    }

public:
    void reset()
    {
        for (auto& [id, line] : edgeDelays_)
            line.prepare(juce::jmax(1, line.buf.getNumSamples()));
        std::fill(scratchL_.begin(), scratchL_.end(), 0.0f);
        std::fill(scratchR_.begin(), scratchR_.end(), 0.0f);
    }

    /** A research V2->legacy handoff starts a new legacy sample timeline.
     *  A sync() recomputes compensation but deliberately retains ring history;
     *  therefore sync() alone cannot safely revive a ring that was dormant
     *  while V2 rendered. This invalidates freshness WITHOUT clearing or
     *  reallocating potentially huge JUCE buffers in the audio callback.
     *  read() silences taps until newly pushed samples have filled the delay.
     *  Only call at a coherent whole-block authority switch.
     */
    void invalidateStreamingHistoryForTimelineSwitch() noexcept
    {
        for (auto& [id, line] : edgeDelays_)
        {
            line.primed = 0;
            line.currentReadDelay = -1;
            line.previousReadDelay = 0;
            line.fadeRemaining = 0;
        }
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
        if(numSamples<=0 || L==nullptr || R==nullptr ||
           scratchL_.empty() || scratchR_.empty() ||
           it->second.buf.getNumSamples()<2)
            return;
        const int effectiveDelay=effectiveDelayOverride>=0
            ?effectiveDelayOverride:it->second.delaySamples;
        // The RT scratch bank was prepared on the control thread. Split a
        // larger render into sequential chunks rather than reallocating it
        // inside the callback. Push/read per chunk preserves delayed samples
        // across chunk AND callback boundaries without overwriting the ring.
        const int chunkLimit=juce::jmin(
            juce::jmin((int)scratchL_.size(),(int)scratchR_.size()),
            it->second.buf.getNumSamples()-1);
        for(int offset=0;offset<numSamples;)
        {
            const int frames=juce::jmin(chunkLimit,numSamples-offset);
            it->second.push(L+offset,R+offset,frames);
            it->second.read(scratchL_.data(),scratchR_.data(),frames,effectiveDelay);
            std::memcpy(L+offset,scratchL_.data(),sizeof(float)*(size_t)frames);
            std::memcpy(R+offset,scratchR_.data(),sizeof(float)*(size_t)frames);
            offset+=frames;
        }
    }

private:
    struct DelayLine
    {
        juce::AudioBuffer<float> buf;
        int writePos = 0;
        int delaySamples = 0;
        bool usedInCurrentSync = false;
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
            // push() already wrote the current block. The number of samples
            // which were known-fresh BEFORE this block is primed - n, clamped
            // for the fully wrapped ring. A tap older than that + i must
            // return silence: physical ring bytes may belong to a previous
            // V2/legacy timeline. This check is allocation-free and O(1).
            const int freshBeforeBlock = juce::jmax(0, primed - n);
            for (int i = 0; i < n; ++i)
            {
                const int posNew = (writePos - currentReadDelay - n + i + cap * 4) % cap;
                if (fadeRemaining > 0)
                {
                    const int posOld = (writePos - previousReadDelay - n + i + cap * 4) % cap;
                    const float t = 1.0f - (float) fadeRemaining / (float) fadeSamples;
                    const bool oldValid = freshBeforeBlock + i >= previousReadDelay;
                    const bool newValid = freshBeforeBlock + i >= currentReadDelay;
                    L[i] = (oldValid ? rL[posOld] : 0.0f) * (1.0f - t)
                         + (newValid ? rL[posNew] : 0.0f) * t;
                    R[i] = (oldValid ? rR[posOld] : 0.0f) * (1.0f - t)
                         + (newValid ? rR[posNew] : 0.0f) * t;
                    --fadeRemaining;
                }
                else
                {
                    const bool newValid = freshBeforeBlock + i >= currentReadDelay;
                    L[i] = newValid ? rL[posNew] : 0.0f;
                    R[i] = newValid ? rR[posNew] : 0.0f;
                }
            }
        }
        void read(float* L, float* R, int n) noexcept
        {
            read(L, R, n, delaySamples);
        }
    };

    struct StringHash { size_t operator()(const juce::String& s) const noexcept { return (size_t)s.hashCode64(); } };
    int blockSize_ = 512;
    int pdcFadeSamples_ = 0;
    std::unordered_map<RouteID, DelayLine, StringHash> edgeDelays_;
    std::vector<float> scratchL_, scratchR_;
    std::atomic<std::uint64_t> realtimeUnpreparedEdges_{0}; // RT increments; control may read
};

} // namespace DAW
