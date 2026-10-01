// ===========================================================================
// TrackPeakMeterManagerCore.h
// Manages per-track meter state.
// Audio thread writes via processTrackBlock().
// UI thread reads via getMeterSnapshot() / resetPeakHold().
// No locks. No allocations in processTrackBlock().
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "TrackPeakMeterStateCore.h"
#include "TruePeakMeterCore.h"
#include <unordered_map>
#include <memory>
#include <string>

namespace DAW {

class TrackPeakMeterManagerCore
{
public:
    // ── Setup (message thread) ───────────────────────────────────────────
    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        blockSize_  = juce::jmax(1, blockSize);
        for (auto& kv : truePeakMeters_)
            kv.second->prepare(sampleRate_, blockSize_);
    }

    void registerTrack(const juce::String& trackId)
    {
        if (states_.count(trackId) == 0)
            states_[trackId] = std::make_unique<TrackPeakMeterStateCore>();
        if (truePeakMeters_.count(trackId) == 0)
        {
            auto tp = std::make_unique<TruePeakMeterCore>();
            tp->prepare(sampleRate_, blockSize_);
            truePeakMeters_[trackId] = std::move(tp);
        }
    }

    void removeTrack(const juce::String& trackId)
    {
        states_.erase(trackId);
        truePeakMeters_.erase(trackId);
    }

    void clearAll()
    {
        states_.clear();
        truePeakMeters_.clear();
    }

    // ── Audio thread ─────────────────────────────────────────────────────
    // Call after track plugins + volume/pan have been applied to the buffer.
    // L/R are post-fader interleaved stereo pointers (or mono with R==L).
    void processTrackBlock(const juce::String& trackId,
                           const float* L, const float* R,
                           int numSamples) noexcept
    {
        auto sit = states_.find(trackId);
        if (sit == states_.end()) return;
        auto tit = truePeakMeters_.find(trackId);
        if (tit == truePeakMeters_.end()) return;

        auto& state = *sit->second;
        auto& tp    = *tit->second;

        // Sample peak (stereo max)
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float s = juce::jmax(std::abs(L[i]), std::abs(R[i]));
            if (s > peak) peak = s;
        }
        const float spDb = linearToDb(peak);
        state.samplePeakDb.store(spDb, std::memory_order_relaxed);

        // True peak (L channel — acceptable proxy for metering display)
        tp.process(L, numSamples);
        const float tpDb = tp.getPeakDb();
        state.truePeakDb.store(tpDb, std::memory_order_relaxed);

        // Peak hold: keep highest of sample/true peak
        const float best = juce::jmax(spDb, tpDb);
        const float prev = state.peakHoldDb.load(std::memory_order_relaxed);
        if (best > prev)
            state.peakHoldDb.store(best, std::memory_order_relaxed);

        // Clip: sample peak >= 0 dBFS or true peak >= 0 dBTP
        if (best >= 0.0f)
            state.clipped.store(true, std::memory_order_relaxed);
    }

    // Convenience overload for juce::AudioBuffer<float>
    void processTrackBlock(const juce::String& trackId,
                           const juce::AudioBuffer<float>& buf,
                           int numSamples) noexcept
    {
        if (buf.getNumChannels() < 1) return;
        const float* L = buf.getReadPointer(0);
        const float* R = buf.getReadPointer(juce::jmin(1, buf.getNumChannels() - 1));
        processTrackBlock(trackId, L, R, numSamples);
    }

    // ── UI thread ────────────────────────────────────────────────────────
    TrackPeakMeterStateCore::Snapshot getMeterSnapshot(const juce::String& trackId) const noexcept
    {
        auto it = states_.find(trackId);
        if (it == states_.end()) return {};
        return it->second->read();
    }

    void resetPeakHold(const juce::String& trackId) noexcept
    {
        auto it = states_.find(trackId);
        if (it != states_.end())
            it->second->resetHold();
    }

    bool hasTrack(const juce::String& trackId) const noexcept
    {
        return states_.count(trackId) > 0;
    }

private:
    static float linearToDb(float v) noexcept
    {
        return v <= 0.000001f ? -120.0f
             : juce::Decibels::gainToDecibels(v, -120.0f);
    }

    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;

    // Both maps keyed by TrackID string.
    // Insertions happen on message thread (registerTrack/removeTrack),
    // so audio thread must not insert — only look up by existing keys.
    std::unordered_map<juce::String, std::unique_ptr<TrackPeakMeterStateCore>> states_;
    std::unordered_map<juce::String, std::unique_ptr<TruePeakMeterCore>>       truePeakMeters_;
};

} // namespace DAW
