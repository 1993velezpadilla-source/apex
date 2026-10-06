#pragma once
#include <JuceHeader.h>
#include "HostPlayheadCore.h"
#include "PluginTransportSnapshotCore.h"
#include "../TransportCore/TransportController.h"
#include <atomic>

namespace DAW {

class PluginPlayheadInfoCore
{
public:
    PluginPlayheadInfoCore() = default;

    void setTransportController(TransportController* transport)
    {
        transport_ = transport;
        playhead_.setTransportControlCallbacks(
            [this](bool shouldPlay)
            {
                pendingPlay_.store(shouldPlay ? 1 : 0, std::memory_order_release);
            },
            [this](bool shouldRecord)
            {
                pendingRecord_.store(shouldRecord ? 1 : 0, std::memory_order_release);
            },
            [this]
            {
                requestSeekSamples(0);
            });
    }

    void updateSnapshot(const PluginTransportSnapshot& snapshot) noexcept
    {
        playhead_.updateSnapshot(snapshot);
    }

    void requestSeekSamples(int64_t samplePosition) noexcept
    {
        pendingSeekSamples_.store(juce::jmax<int64_t>(0, samplePosition), std::memory_order_relaxed);
        pendingSeek_.store(true, std::memory_order_release);
    }

    void requestStartFromSamples(int64_t samplePosition) noexcept
    {
        requestSeekSamples(samplePosition);
    }

    bool consumePendingSeek(int64_t& samplePosition) noexcept
    {
        if (!pendingSeek_.exchange(false, std::memory_order_acq_rel))
            return false;

        samplePosition = pendingSeekSamples_.load(std::memory_order_relaxed);
        return true;
    }

    void flushPendingTransportControlRequests()
    {
        if (transport_ == nullptr) return;

        const int play = pendingPlay_.exchange(-1, std::memory_order_acq_rel);
        if (play == 1)
            transport_->play();
        else if (play == 0)
            transport_->pause();

        const int record = pendingRecord_.exchange(-1, std::memory_order_acq_rel);
        if (record == 1)
            transport_->record();
        else if (record == 0)
            transport_->stopRecording();
    }

    juce::AudioPlayHead* getPlayhead() noexcept { return &playhead_; }
    const juce::AudioPlayHead* getPlayhead() const noexcept { return &playhead_; }

private:
    HostPlayheadCore playhead_;
    TransportController* transport_ = nullptr;
    std::atomic<bool> pendingSeek_{false};
    std::atomic<int64_t> pendingSeekSamples_{0};
    std::atomic<int> pendingPlay_{-1};
    std::atomic<int> pendingRecord_{-1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginPlayheadInfoCore)
};

} // namespace DAW
