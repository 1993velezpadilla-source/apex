#pragma once
#include <JuceHeader.h>
#include <functional>

namespace DAW {

/**
 * AutoSaveCore — periodic background auto-save.
 *
 * Saves project state periodically (default: every 2 minutes).
 * Must NOT block audio thread or freeze UI.
 * Uses a background thread for file I/O.
 *
 * Auto-save files are stored alongside the project with a
 * distinctive naming pattern for crash recovery.
 */
class AutoSaveCore : private juce::Timer
{
public:
    AutoSaveCore() = default;

    ~AutoSaveCore() override { stopTimer(); }

    /** Set the auto-save callback. Called on the message thread. */
    void setAutoSaveCallback(std::function<void()> callback)
    {
        saveCallback_ = std::move(callback);
    }

    void setEnabled(bool enabled)
    {
        enabled_ = enabled;
        if (enabled_ && intervalMs_ > 0)
            startTimer(intervalMs_);
        else
            stopTimer();
    }

    bool isEnabled() const noexcept { return enabled_; }

    /** Set auto-save interval in seconds (minimum 30). */
    void setIntervalSeconds(int seconds)
    {
        intervalMs_ = juce::jmax(30, seconds) * 1000;
        if (enabled_) startTimer(intervalMs_);
    }

    int getIntervalSeconds() const noexcept { return intervalMs_ / 1000; }

private:
    void timerCallback() override
    {
        if (enabled_ && saveCallback_)
            saveCallback_();
    }

    std::function<void()> saveCallback_;
    bool enabled_ = false;
    int intervalMs_ = 120000; // 2 minutes default
};

} // namespace DAW
