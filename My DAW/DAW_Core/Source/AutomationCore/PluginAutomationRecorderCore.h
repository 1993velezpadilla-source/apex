#pragma once
#include <JuceHeader.h>
#include "AutomationManagerCore.h"
#include "PluginAutomationGestureCore.h"
#include "LastTouchedPluginParameterCore.h"
#include "../Automation/AutomationLaneStoreCore.h"
#include "../Automation/AutomationTransportStateCore.h"
#include "../Automation/AutomationClockCore.h"
#include "../Automation/AutomationParameterKeyCore.h"

namespace DAW {

enum class AutomationWriteMode
{
    Read,
    Write,
    Touch,
    Latch,
    Off
};

class PluginAutomationRecorderCore
    : private juce::Timer
{
public:
    using IsPlayingFn = std::function<bool()>;
    using PositionFn = std::function<int64_t()>;
    using SampleRateFn = std::function<double()>;
    using LaneWrittenFn = std::function<void(const LastTouchedPluginParameter&, const juce::String&)>;

    void setSubsystems(AutomationManagerCore* automationManager,
                       PluginAutomationGestureCore* gestureCore,
                       IsPlayingFn isPlaying,
                       PositionFn position)
    {
        automationManager_ = automationManager;
        gestureCore_ = gestureCore;
        isPlaying_ = std::move(isPlaying);
        position_ = std::move(position);
    }

    // ApplicationCore supplies the active device sample rate. No fixed
    // 44.1 kHz assumption when throttling plugin parameter writing.
    void setSampleRateProvider(SampleRateFn provider)
    {
        sampleRate_ = std::move(provider);
    }

    void start60Hz() { startTimerHz(60); }
    void stop60Hz() { stopTimer(); }
    void setLaneWrittenCallback(LaneWrittenFn callback) { onLaneWritten_ = std::move(callback); }

    void setMode(AutomationWriteMode mode) noexcept { globalMode_.store((int)mode, std::memory_order_relaxed); }
    AutomationWriteMode getMode() const noexcept { return getGlobalMode(); }
    static void setGlobalMode(AutomationWriteMode mode) noexcept { globalMode_.store((int)mode, std::memory_order_relaxed); }
    static AutomationWriteMode getGlobalMode() noexcept { return (AutomationWriteMode)globalMode_.load(std::memory_order_relaxed); }

    bool shouldRecord(bool transportPlaying, bool gestureActive, bool latched) const noexcept
    {
        const auto mode = getGlobalMode();
        if (!transportPlaying
            || !apex::automation::AutomationTransportState::getInstance().isRecordArmed()
            || mode == AutomationWriteMode::Off
            || mode == AutomationWriteMode::Read)
            return false;

        if (mode == AutomationWriteMode::Write)
            return true;

        if (mode == AutomationWriteMode::Touch)
            return gestureActive;

        if (mode == AutomationWriteMode::Latch)
            return gestureActive || latched;

        return false;
    }

    bool shouldWritePoint(const LastTouchedPluginParameter& target, int64_t timeSamples, double sampleRate) noexcept
    {
        const auto minSpacingSamples = (int64_t)std::llround(juce::jmax(1.0, sampleRate) * 0.015);
        if (target.parameterId != lastParameterId_ || target.trackId != lastTrackId_ || target.pluginSlotIndex != lastPluginSlot_)
            return true;

        if (std::llabs(timeSamples - lastWriteTimeSamples_) < minSpacingSamples
            && std::abs(target.normalizedValue - lastWriteValue_) < 0.001f)
            return false;

        return true;
    }

    void markWritten(const LastTouchedPluginParameter& target, int64_t timeSamples) noexcept
    {
        lastTrackId_ = target.trackId;
        lastPluginSlot_ = target.pluginSlotIndex;
        lastParameterId_ = target.parameterId;
        lastWriteTimeSamples_ = timeSamples;
        lastWriteValue_ = target.normalizedValue;
    }

    void writeFinalPoint(const LastTouchedPluginParameter& target, bool transportPlaying)
    {
        if (automationManager_ == nullptr
            || !target.isValid()
            || !transportPlaying
            || !apex::automation::AutomationTransportState::getInstance().isRecordArmed())
            return;

        const auto mode = getGlobalMode();
        if (mode == AutomationWriteMode::Off || mode == AutomationWriteMode::Read)
            return;

        const int64_t timeSamples = position_ ? position_() : 0;
        const auto parameterId = AutomationManagerCore::makePluginParameterId(target);
        auto& coreLane = automationManager_->getOrCreateLane(target.trackId, parameterId);
        coreLane.setParameterName(target.pluginDisplayName + " / " + target.parameterName);
        coreLane.setVisible(true);
        automationManager_->addOrReplacePoint(target.trackId, parameterId, timeSamples, target.normalizedValue, false);
        mirrorToApexLane(target);
        automationManager_->publishSnapshot();
        markWritten(target, timeSamples);
        notifyLaneWritten(target, parameterId);
    }

private:
    void timerCallback() override
    {
        if (automationManager_ == nullptr || gestureCore_ == nullptr)
            return;

        const bool playing = isPlaying_ ? isPlaying_() : false;
        const int64_t timeSamples = position_ ? position_() : 0;

        for (const auto& ended : gestureCore_->expireImplicitGestures())
            writeFinalPoint(ended.target, playing);

        for (const auto& gesture : gestureCore_->getActiveGestures())
        {
            const auto& target = gesture.target;
            if (!shouldRecord(playing, gesture.isActive(), false))
                continue;

            const auto key = PluginAutomationGestureCore::makeKey(target);
            const double activeSampleRate = sampleRate_ ? sampleRate_() : 0.0;
            if (!firstPointWritten_.count(key) || shouldWritePoint(target, timeSamples, activeSampleRate))
            {
                const auto parameterId = AutomationManagerCore::makePluginParameterId(target);
                auto& coreLane = automationManager_->getOrCreateLane(target.trackId, parameterId);
                coreLane.setParameterName(target.pluginDisplayName + " / " + target.parameterName);
                coreLane.setVisible(true);
                automationManager_->addOrReplacePoint(target.trackId, parameterId, timeSamples, target.normalizedValue, true);
                mirrorToApexLane(target);
                firstPointWritten_.insert(key);
                markWritten(target, timeSamples);
                if (++frameCounter_ % 4 == 0)
                    automationManager_->publishSnapshot();
                notifyLaneWritten(target, parameterId);
            }
        }
    }

    inline static std::atomic<int> globalMode_ { (int)AutomationWriteMode::Read };
    AutomationManagerCore* automationManager_ = nullptr;
    PluginAutomationGestureCore* gestureCore_ = nullptr;
    IsPlayingFn isPlaying_;
    PositionFn position_;
    SampleRateFn sampleRate_;
    LaneWrittenFn onLaneWritten_;
    std::set<juce::String> firstPointWritten_;
    int frameCounter_ = 0;
    TrackID lastTrackId_;
    int lastPluginSlot_ = -1;
    juce::String lastParameterId_;
    int64_t lastWriteTimeSamples_ = std::numeric_limits<int64_t>::min();
    float lastWriteValue_ = 0.0f;

    void mirrorToApexLane(const LastTouchedPluginParameter& target)
    {
        if (!target.isValid())
            return;

        // The audio engine publishes the actual transport PPQ every block
        // using the live project tempo/sample rate. Mirror to the SAME PPQ
        // clock as the automation evaluator: converting raw sample positions
        // using fixed 44100/120 shifted recorded plugin curves on 48 kHz
        // devices and projects with a different tempo.
        const auto clock = apex::automation::AutomationClock::getInstance().snapshot();
        if (!clock.transportRolling || !std::isfinite(clock.blockStartPPQ))
            return;
        const auto key = apex::automation::AutomationParameterKeyRegistry::pluginParamKey(
            target.trackId, target.pluginSlotIndex, target.pluginDisplayName, target.parameterId);
        const auto id = apex::automation::AutomationParameterKeyRegistry::getInstance().getOrCreateID(key);
        auto& lane = apex::automation::AutomationLaneStore::getInstance().getOrCreateLane(id);
        auto snap = lane.getSnapshot();
        apex::automation::AutomationLane::PointVector next = snap ? *snap : apex::automation::AutomationLane::PointVector{};
        const double ppq = clock.blockStartPPQ;
        const float value = juce::jlimit(0.0f, 1.0f, target.normalizedValue);

        for (auto& point : next)
        {
            if (std::abs(point.timePPQ - ppq) < 1.0e-9)
            {
                point.normalizedValue = value;
                lane.replacePoints(std::move(next));
                return;
            }
        }

        next.push_back({ ppq, value, apex::automation::CurveType::Linear, 0.0f });
        lane.replacePoints(std::move(next));
    }

    void notifyLaneWritten(const LastTouchedPluginParameter& target, const juce::String& parameterId)
    {
        if (onLaneWritten_)
            onLaneWritten_(target, parameterId);
    }
};

} // namespace DAW
