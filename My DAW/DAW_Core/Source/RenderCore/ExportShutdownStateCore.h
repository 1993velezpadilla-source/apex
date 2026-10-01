#pragma once
#include <atomic>

namespace DAW {

class ExportShutdownStateCore
{
public:
    void beginExport() noexcept
    {
        cancellationRequested_.store(false, std::memory_order_release);
        callbacksEnabled_.store(true, std::memory_order_release);
    }

    void requestCancellation() noexcept
    {
        cancellationRequested_.store(true, std::memory_order_release);
    }

    void prepareForShutdown() noexcept
    {
        callbacksEnabled_.store(false, std::memory_order_release);
        cancellationRequested_.store(true, std::memory_order_release);
    }

    bool shouldPostCallbacks() const noexcept
    {
        return callbacksEnabled_.load(std::memory_order_acquire);
    }

    bool isCancellationRequested() const noexcept
    {
        return cancellationRequested_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> callbacksEnabled_ { false };
    std::atomic<bool> cancellationRequested_ { false };
};

} // namespace DAW
