#pragma once
#include <atomic>
#include <cstdint>

namespace DAW {

class RecordingLifecycleStateCore
{
public:
    enum class StopState : uint8_t
    {
        ready,
        pendingDrain,
        terminalFailure
    };

    enum class StopPollAction : uint8_t
    {
        none,
        wait,
        finalize,
        terminalFailure
    };

    struct Snapshot
    {
        StopState stopState = StopState::ready;
        bool pendingRestart = false;
        bool shutdownStarted = false;
        bool restartAllowed = true;
        bool finalizationDispatchFailed = false;
    };

    void beginDeferredStop() noexcept
    {
        if (! shutdownStarted_.load(std::memory_order_acquire)
            && stopState_.load(std::memory_order_acquire) != StopState::terminalFailure)
            stopState_.store(StopState::pendingDrain, std::memory_order_release);
    }

    StopPollAction pollDeferredStop(bool callbacksDrained,
                                    bool deadlineExpired) noexcept
    {
        if (stopState_.load(std::memory_order_acquire) != StopState::pendingDrain)
            return StopPollAction::none;

        if (callbacksDrained)
            return StopPollAction::finalize;

        if (! deadlineExpired)
            return StopPollAction::wait;

        stopState_.store(StopState::terminalFailure, std::memory_order_release);
        pendingRestart_.store(false, std::memory_order_release);
        return StopPollAction::terminalFailure;
    }

    void markSafeCleanupComplete() noexcept
    {
        if (stopState_.load(std::memory_order_acquire) != StopState::terminalFailure)
            stopState_.store(StopState::ready, std::memory_order_release);
    }

    bool requestRestartAfterFinalization() noexcept
    {
        if (! isRestartAllowed())
            return false;

        pendingRestart_.store(true, std::memory_order_release);
        return true;
    }

    bool consumeRestartAfterFinalization(bool transportStillRecording) noexcept
    {
        const bool requested = pendingRestart_.exchange(false, std::memory_order_acq_rel);
        return requested && transportStillRecording && isRestartAllowed();
    }

    void cancelPendingRestart() noexcept
    {
        pendingRestart_.store(false, std::memory_order_release);
    }

    void beginShutdown() noexcept
    {
        shutdownStarted_.store(true, std::memory_order_release);
        pendingRestart_.store(false, std::memory_order_release);
    }

    void markFinalizationDispatchFailed() noexcept
    {
        finalizationDispatchFailed_.store(true, std::memory_order_release);
        pendingRestart_.store(false, std::memory_order_release);
        if (! shutdownStarted_.load(std::memory_order_acquire)
            && stopState_.load(std::memory_order_acquire) != StopState::terminalFailure)
            stopState_.store(StopState::ready, std::memory_order_release);
    }

    Snapshot getSnapshot() const noexcept
    {
        Snapshot snapshot;
        snapshot.stopState = stopState_.load(std::memory_order_acquire);
        snapshot.pendingRestart = pendingRestart_.load(std::memory_order_acquire);
        snapshot.shutdownStarted = shutdownStarted_.load(std::memory_order_acquire);
        snapshot.restartAllowed = snapshot.stopState == StopState::ready
            && ! snapshot.shutdownStarted;
        snapshot.finalizationDispatchFailed =
            finalizationDispatchFailed_.load(std::memory_order_acquire);
        return snapshot;
    }

private:
    bool isRestartAllowed() const noexcept
    {
        return stopState_.load(std::memory_order_acquire) == StopState::ready
            && ! shutdownStarted_.load(std::memory_order_acquire);
    }

    std::atomic<StopState> stopState_ { StopState::ready };
    std::atomic<bool> pendingRestart_ { false };
    std::atomic<bool> shutdownStarted_ { false };
    std::atomic<bool> finalizationDispatchFailed_ { false };
};

} // namespace DAW
