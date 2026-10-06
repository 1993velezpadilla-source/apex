#pragma once
#include <JuceHeader.h>
#include "PluginTransportSnapshotCore.h"
#include <atomic>
#include <functional>

namespace DAW {

class HostPlayheadCore final : public juce::AudioPlayHead
{
public:
    HostPlayheadCore() = default;

    void setTransportControlCallbacks(std::function<void(bool)> playCallback,
                                      std::function<void(bool)> recordCallback,
                                      std::function<void()> rewindCallback)
    {
        playCallback_ = std::move(playCallback);
        recordCallback_ = std::move(recordCallback);
        rewindCallback_ = std::move(rewindCallback);
    }

    void updateSnapshot(const PluginTransportSnapshot& snapshot) noexcept
    {
        snapshot_.timeInSamples.store(snapshot.timeInSamples, std::memory_order_relaxed);
        snapshot_.timeInSeconds.store(snapshot.timeInSeconds, std::memory_order_relaxed);
        snapshot_.bpm.store(snapshot.bpm, std::memory_order_relaxed);
        snapshot_.timeSigNumerator.store(snapshot.timeSigNumerator, std::memory_order_relaxed);
        snapshot_.timeSigDenominator.store(snapshot.timeSigDenominator, std::memory_order_relaxed);
        snapshot_.ppqPosition.store(snapshot.ppqPosition, std::memory_order_relaxed);
        snapshot_.ppqLastBarStart.store(snapshot.ppqLastBarStart, std::memory_order_relaxed);
        snapshot_.isPlaying.store(snapshot.isPlaying, std::memory_order_relaxed);
        snapshot_.isRecording.store(snapshot.isRecording, std::memory_order_relaxed);
        snapshot_.isLooping.store(snapshot.isLooping, std::memory_order_relaxed);
        snapshot_.loopStartSamples.store(snapshot.loopStartSamples, std::memory_order_relaxed);
        snapshot_.loopEndSamples.store(snapshot.loopEndSamples, std::memory_order_relaxed);
        snapshot_.sampleRate.store(snapshot.sampleRate, std::memory_order_relaxed);
    }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        PluginTransportSnapshot snapshot;
        snapshot.timeInSamples = snapshot_.timeInSamples.load(std::memory_order_relaxed);
        snapshot.timeInSeconds = snapshot_.timeInSeconds.load(std::memory_order_relaxed);
        snapshot.bpm = snapshot_.bpm.load(std::memory_order_relaxed);
        snapshot.timeSigNumerator = snapshot_.timeSigNumerator.load(std::memory_order_relaxed);
        snapshot.timeSigDenominator = snapshot_.timeSigDenominator.load(std::memory_order_relaxed);
        snapshot.ppqPosition = snapshot_.ppqPosition.load(std::memory_order_relaxed);
        snapshot.ppqLastBarStart = snapshot_.ppqLastBarStart.load(std::memory_order_relaxed);
        snapshot.isPlaying = snapshot_.isPlaying.load(std::memory_order_relaxed);
        snapshot.isRecording = snapshot_.isRecording.load(std::memory_order_relaxed);
        snapshot.isLooping = snapshot_.isLooping.load(std::memory_order_relaxed);
        snapshot.loopStartSamples = snapshot_.loopStartSamples.load(std::memory_order_relaxed);
        snapshot.loopEndSamples = snapshot_.loopEndSamples.load(std::memory_order_relaxed);
        snapshot.sampleRate = snapshot_.sampleRate.load(std::memory_order_relaxed);

        juce::AudioPlayHead::PositionInfo info;
        info.setTimeInSamples(snapshot.timeInSamples);
        info.setTimeInSeconds(snapshot.timeInSeconds);
        info.setBpm(snapshot.bpm);
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature{ snapshot.timeSigNumerator,
                                                                  snapshot.timeSigDenominator });
        info.setPpqPosition(snapshot.ppqPosition);
        info.setPpqPositionOfLastBarStart(snapshot.ppqLastBarStart);
        info.setIsPlaying(snapshot.isPlaying);
        info.setIsRecording(snapshot.isRecording);
        info.setIsLooping(snapshot.isLooping);

        if (snapshot.isLooping && snapshot.loopEndSamples > snapshot.loopStartSamples && snapshot.sampleRate > 0.0 && snapshot.bpm > 0.0)
        {
            const auto samplesToPpq = [sampleRate = snapshot.sampleRate, bpm = snapshot.bpm](int64_t samples) noexcept
            {
                return ((static_cast<double>(samples) / sampleRate) / 60.0) * bpm;
            };

            info.setLoopPoints(juce::AudioPlayHead::LoopPoints{ samplesToPpq(snapshot.loopStartSamples),
                                                                samplesToPpq(snapshot.loopEndSamples) });
        }

        return info;
    }

    bool canControlTransport() override { return true; }

    void transportPlay(bool shouldStartPlaying) override
    {
        if (playCallback_) playCallback_(shouldStartPlaying);
    }

    void transportRecord(bool shouldStartRecording) override
    {
        if (recordCallback_) recordCallback_(shouldStartRecording);
    }

    void transportRewind() override
    {
        if (rewindCallback_) rewindCallback_();
    }

private:
    struct AtomicSnapshot
    {
        std::atomic<int64_t> timeInSamples{0};
        std::atomic<double> timeInSeconds{0.0};
        std::atomic<double> bpm{120.0};
        std::atomic<int> timeSigNumerator{4};
        std::atomic<int> timeSigDenominator{4};
        std::atomic<double> ppqPosition{0.0};
        std::atomic<double> ppqLastBarStart{0.0};
        std::atomic<bool> isPlaying{false};
        std::atomic<bool> isRecording{false};
        std::atomic<bool> isLooping{false};
        std::atomic<int64_t> loopStartSamples{0};
        std::atomic<int64_t> loopEndSamples{0};
        std::atomic<double> sampleRate{44100.0};
    } snapshot_;

    std::function<void(bool)> playCallback_;
    std::function<void(bool)> recordCallback_;
    std::function<void()> rewindCallback_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostPlayheadCore)
};

} // namespace DAW
