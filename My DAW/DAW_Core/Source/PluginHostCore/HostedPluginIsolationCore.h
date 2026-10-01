#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace DAW {

/** Process-wide diagnostic policy for one binary-isolation session.

    The production application resolves the command-line flag before creating
    MainWindow. The value is never serialized and is not exposed as a rolling
    audio-thread toggle. Hosted processors remain loaded and prepared, but
    their DSP callbacks are skipped while surrounding APEX processing remains
    active.
*/
class HostedPluginIsolationCore
{
public:
    static constexpr const char* kCommandLineFlag = "--apex-bypass-all-hosted-dsp";

    static bool commandLineRequestsIsolation(const juce::StringArray& arguments)
    {
        return arguments.contains(kCommandLineFlag, false);
    }

    /** Startup/control-plane only in production. */
    static void configureForSession(bool shouldBypass) noexcept
    {
        bypassHostedDsp_.store(shouldBypass, std::memory_order_release);
    }

    /** Realtime-safe: one bounded atomic load. */
    static bool shouldBypassHostedDsp() noexcept
    {
        return bypassHostedDsp_.load(std::memory_order_acquire);
    }

    /** Keep raw plugin reports unchanged while publishing the latency that is
        actually present in the diagnostic render path. */
    static int effectiveHostedLatencySamples(int reportedLatencySamples) noexcept
    {
        return shouldBypassHostedDsp() ? 0 : juce::jmax(0, reportedLatencySamples);
    }

    /** Deterministic test scope. Production startup does not use rolling
        overrides. */
    class ScopedOverride
    {
    public:
        explicit ScopedOverride(bool shouldBypass) noexcept
            : previous_(shouldBypassHostedDsp())
        {
            configureForSession(shouldBypass);
        }

        ~ScopedOverride()
        {
            configureForSession(previous_);
        }

        ScopedOverride(const ScopedOverride&) = delete;
        ScopedOverride& operator=(const ScopedOverride&) = delete;

    private:
        bool previous_ = false;
    };

private:
    inline static std::atomic<bool> bypassHostedDsp_ { false };
};

} // namespace DAW
