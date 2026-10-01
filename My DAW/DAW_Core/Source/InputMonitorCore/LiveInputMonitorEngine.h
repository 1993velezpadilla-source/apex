#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../TransportCore/TransportController.h"
#include "TrackMonitoringStateModel.h"
#include "../RecordingCore/RecordingInputValidityCore.h"

namespace DAW {

/**
 * LiveInputMonitorEngine — per-track input trim and metering.
 *
 * Pre-allocates scratch buffers for the worst-case block size (8192 samples)
 * so that processBlock() never heap-allocates. This prevents crashes when
 * the audio device changes buffer size (e.g. ASIO4ALL switching to 2048).
 *
 * Audio output for live monitoring is handled ENTIRELY by
 * AudioEngine::addLiveInputToTrackBuffer(). This class only handles:
 *
 *   1. Input trim (gain staging before metering)
 *   2. Input VU metering (visual feedback)
 */
class LiveInputMonitorEngine
{
public:
    /** Maximum supported block size — pre-allocated in prepare(). */
    static constexpr int maxBlockSize = 8192;

    void setSubsystems(TrackManager* tracks, TransportController* transport)
    {
        tracks_    = tracks;
        transport_ = transport;
    }

    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
        const int allocSize = juce::jmax(blockSize, maxBlockSize);
        scratchL_.assign((size_t) allocSize, 0.0f);
        scratchR_.assign((size_t) allocSize, 0.0f);
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

    void processBlock(const juce::AudioBuffer<float>& input,
                      int numSamples,
                      int validInputChannels)
    {
        juce::ScopedNoDenormals noDenormals;

        if (!tracks_ || !transport_) return;
        if (numSamples <= 0) return;

        // Guard: if a racing device reconfiguration gives us samples beyond
        // our pre-allocated capacity, clamp to prevent buffer overrun.
        if (numSamples > (int) scratchL_.size())
        {
            jassertfalse;
            numSamples = (int) scratchL_.size();
        }

        const int validChannels = juce::jmin(
            input.getNumChannels(), juce::jmax(0, validInputChannels));
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

            const bool feedMeterOnly = !shouldMonitor && track->isArmed();
            if (!shouldMonitor && !feedMeterOnly) continue;

            const int firstCh = track->getInputFirstChannel() > 0
                ? track->getInputFirstChannel()
                : getTrackInputChannel(track->getID());
            const bool mono   = track->isInputMono();
            if (! RecordingInputValidityCore::isRouteAvailable(firstCh, mono, validChannels))
                continue;

            const float* hwL = input.getReadPointer(firstCh);
            const float* hwR = mono ? hwL : input.getReadPointer(firstCh + 1);

            if (feedMeterOnly)
            {
                // Armed but NOT monitoring: the live input is not part of the
                // track buffer, so meter it here (post-trim) so the trim panel
                // VU still shows the incoming signal level.
                std::memcpy(scratchL_.data(), hwL, sizeof(float) * (size_t) numSamples);
                std::memcpy(scratchR_.data(), hwR, sizeof(float) * (size_t) numSamples);

                track->getInputTrim().applyToStereoBuffer(scratchL_.data(), scratchR_.data(), numSamples);
                track->getInputMeter().processBlock(scratchL_.data(), scratchR_.data(), numSamples);
                continue;
            }

            // Monitoring: the engine adds the live input to the track buffer
            // (AudioEngine::addLiveInputToTrackBuffer) and TrackInputProcessorCore
            // meters that buffer exactly once per block (post-trim, pre-FX).
            // Feeding the meter here as well would double-count the signal and
            // make the trim-panel VU over-read by ~6 dB. Nothing else is done
            // in this loop, so monitoring tracks simply fall through.
            juce::ignoreUnused(hwL, hwR);
        }
    }

private:
    TrackManager*        tracks_    = nullptr;
    TransportController* transport_ = nullptr;

    double sampleRate_ { 44100.0 };
    int    blockSize_  { 512 };

    std::unordered_map<TrackID, int> inputChannelMap_;
    std::vector<float>               scratchL_;
    std::vector<float>               scratchR_;
};

} // namespace DAW
