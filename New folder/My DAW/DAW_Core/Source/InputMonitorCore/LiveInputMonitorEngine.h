#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../TransportCore/TransportController.h"
#include "RecordInputRouter.h"
#include "TrackMonitoringStateModel.h"
#include "LiveInputBusManagerCore.h"

namespace DAW {

/** Orchestrates per-track live input monitoring. */
class LiveInputMonitorEngine
{
public:
    void setSubsystems(TrackManager* tracks, TransportController* transport)
    {
        tracks_    = tracks;
        transport_ = transport;
    }

    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
        mixBus_.prepare(blockSize);
        scratchL_.assign((size_t) blockSize, 0.0f);
        scratchR_.assign((size_t) blockSize, 0.0f);
    }

    void releaseResources() {}

    void setTrackInputChannels(const TrackID& trackId, int firstChannel)
    {
        inputChannelMap_[trackId] = juce::jmax(0, firstChannel);
    }

    int getTrackInputChannel(const TrackID& trackId) const
    {
        auto it = inputChannelMap_.find(trackId);
        return (it != inputChannelMap_.end()) ? it->second : 0;
    }

    void processBlock(const juce::AudioBuffer<float>& input, int numSamples)
    {
        ensureScratchCapacity(numSamples);
        mixBus_.clear(numSamples);

        if (!tracks_ || !transport_) return;
        if (numSamples <= 0) return;

        for (auto& [trackId, cache] : wetMonitorCache_)
        {
            juce::ignoreUnused(trackId);
            cache.valid = false;
            cache.numSamples = 0;
        }

        const int inputChans   = input.getNumChannels();
        const bool isStopped   = !transport_->isPlaying();
        const bool isRecording =  transport_->isRecording();

        for (int i = 0; i < tracks_->getNumTracks(); ++i)
        {
            auto* track = tracks_->getTrack(i);
            if (!track || track->isMaster()) continue;

            auto& monState = track->getMonitoringState();
            const auto mode = monState.getMode();

            bool shouldMonitor = false;
            switch (mode)
            {
                case InputMonitorMode::Off:
                    shouldMonitor = false;
                    break;
                case InputMonitorMode::On:
                    shouldMonitor = true;
                    break;
                case InputMonitorMode::Auto:
                    shouldMonitor = track->isArmed() && (isStopped || isRecording);
                    break;
            }

            // Armed tracks always feed trim + VU meter (pro-DAW: input meters
            // stay alive on armed tracks) even when not audibly monitoring.
            const bool feedMeterOnly = !shouldMonitor && track->isArmed();
            if (!shouldMonitor && !feedMeterOnly) continue;

            // Track is the source of truth for input routing (set from the
            // per-track input selector). The legacy map is a fallback for
            // callers that still push channel offsets directly.
            const int firstCh = track->getInputFirstChannel() > 0
                ? track->getInputFirstChannel()
                : getTrackInputChannel(track->getID());
            const bool mono   = track->isInputMono();
            const int srcL    = firstCh;
            const int srcR    = mono ? firstCh : firstCh + 1;

            if (srcL >= inputChans) continue;

            const float* hwL = input.getReadPointer(srcL);
            const float* hwR = (srcR < inputChans) ? input.getReadPointer(srcR) : hwL;

            std::memcpy(scratchL_.data(), hwL, sizeof(float) * (size_t) numSamples);
            std::memcpy(scratchR_.data(), hwR, sizeof(float) * (size_t) numSamples);

            track->getInputTrim().applyToStereoBuffer(scratchL_.data(), scratchR_.data(), numSamples);
            track->getInputMeter().processBlock(scratchL_.data(), scratchR_.data(), numSamples);

            if (feedMeterOnly)
                continue; // metering only — nothing audible, no wet cache
            const auto recMode = monState.getRecordingMode();
            if (recMode == RecordInputRouter::Mode::MonitorWetRecordDry
             || recMode == RecordInputRouter::Mode::MonitorWetRecordWet)
            {
                bool processed = false;
                if (auto* chain = track->getPluginChain())
                {
                    juce::AudioBuffer<float> wetView(2, numSamples);
                    wetView.copyFrom(0, 0, scratchL_.data(), numSamples);
                    wetView.copyFrom(1, 0, scratchR_.data(), numSamples);
                    chain->processBlock(wetView, numSamples);
                    juce::FloatVectorOperations::copy(scratchL_.data(), wetView.getReadPointer(0), numSamples);
                    juce::FloatVectorOperations::copy(scratchR_.data(), wetView.getReadPointer(1), numSamples);
                    processed = true;
                }

                if (!processed && !track->getInputFxChain().isBypassed())
                    track->getInputFxChain().process(scratchL_.data(), scratchR_.data(), numSamples);

                if (recMode == RecordInputRouter::Mode::MonitorWetRecordWet)
                {
                    auto& cache = wetMonitorCache_[track->getID()];
                    if (cache.buffer.getNumChannels() < 2 || cache.buffer.getNumSamples() < numSamples)
                        cache.buffer.setSize(2, numSamples, false, false, true);
                    cache.buffer.copyFrom(0, 0, scratchL_.data(), numSamples);
                    cache.buffer.copyFrom(1, 0, scratchR_.data(), numSamples);
                    cache.numSamples = numSamples;
                    cache.valid = true;
                }
            }

            mixBus_.addTrackContribution(scratchL_.data(), scratchR_.data(), numSamples);
        }
    }

    bool copyCachedWetBlock(const TrackID& trackId, float* destL, float* destR, int numSamples) const noexcept
    {
        auto it = wetMonitorCache_.find(trackId);
        if (it == wetMonitorCache_.end())
            return false;

        const auto& cache = it->second;
        if (!cache.valid || cache.numSamples != numSamples || cache.buffer.getNumChannels() < 2)
            return false;

        if (destL)
            juce::FloatVectorOperations::copy(destL, cache.buffer.getReadPointer(0), numSamples);
        if (destR)
            juce::FloatVectorOperations::copy(destR, cache.buffer.getReadPointer(1), numSamples);
        return true;
    }

    void sumIntoOutput(float* outL, float* outR, int numSamples) const noexcept
    {
        mixBus_.sumIntoOutput(outL, outR, numSamples);
    }

    int getActiveMonitorCount() const noexcept
    {
        return mixBus_.getContributorCount();
    }

private:
    void ensureScratchCapacity(int numSamples)
    {
        if (numSamples > (int) scratchL_.size())
        {
            scratchL_.assign((size_t) numSamples, 0.0f);
            scratchR_.assign((size_t) numSamples, 0.0f);
        }
    }

    struct WetMonitorCacheEntry
    {
        juce::AudioBuffer<float> buffer;
        int numSamples = 0;
        bool valid = false;
    };

    TrackManager*        tracks_    = nullptr;
    TransportController* transport_ = nullptr;

    double sampleRate_ { 44100.0 };
    int    blockSize_  { 512 };

    std::unordered_map<TrackID, int> inputChannelMap_;
    std::unordered_map<TrackID, WetMonitorCacheEntry> wetMonitorCache_;
    LiveInputBusManagerCore          mixBus_;
    std::vector<float>               scratchL_;
    std::vector<float>               scratchR_;
};

} // namespace DAW
