#pragma once
#include <atomic>

// Actual completed startup checkpoints, shared by the JUCE message thread and
// the independent Win32 Evo Eye renderer. NEVER move this counter on a timer:
// 100% means the real StartupPanel is on-screen.
namespace DAW
{
class StartupProgressCore final
{
public:
    static void reset() noexcept { percent().store(0, std::memory_order_release); }

    // Monotonic completed-work milestones; do not regress on late callbacks.
    static void reach(int value) noexcept
    {
        value = value < 0 ? 0 : (value > 100 ? 100 : value);
        auto current = percent().load(std::memory_order_relaxed);
        while (current < value &&
               !percent().compare_exchange_weak(current, value,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {}
    }

    static float fraction() noexcept
    {
        return percent().load(std::memory_order_acquire) / 100.0f;
    }

private:
    static std::atomic<int>& percent() noexcept
    {
        static std::atomic<int> completed { 0 };
        return completed;
    }
};
} // namespace DAW
