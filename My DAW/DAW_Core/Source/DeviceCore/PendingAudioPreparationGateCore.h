#pragma once
#include <atomic>
#include <cstdint>

namespace DAW {

class PendingAudioPreparationGateCore
{
public:
    using Generation = uint64_t;

    enum class CallbackAction : uint8_t
    {
        clearOutputAndReturn,
        accessPreparedResources
    };

    enum class PrepareAction : uint8_t
    {
        stale,
        waitForCallbacks,
        prepare
    };

    void defer(Generation generation) noexcept
    {
        pendingGeneration_.store(generation, std::memory_order_release);
    }

    bool shouldGateCallback() const noexcept
    {
        return pendingGeneration_.load(std::memory_order_acquire) != 0;
    }

    CallbackAction callbackAction(bool guardAdmitted) const noexcept
    {
        return guardAdmitted && !shouldGateCallback()
            ? CallbackAction::accessPreparedResources
            : CallbackAction::clearOutputAndReturn;
    }

    bool isCurrent(Generation generation) const noexcept
    {
        return generation != 0
            && pendingGeneration_.load(std::memory_order_acquire) == generation;
    }

    bool complete(Generation generation) noexcept
    {
        if (generation == 0)
            return false;

        auto expected = generation;
        return pendingGeneration_.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    PrepareAction prepareAction(Generation generation,
                                bool generationCurrent,
                                int activeCallbacks) const noexcept
    {
        if (!generationCurrent || !isCurrent(generation))
            return PrepareAction::stale;

        return activeCallbacks > 0
            ? PrepareAction::waitForCallbacks : PrepareAction::prepare;
    }

    void cancel() noexcept
    {
        pendingGeneration_.store(0, std::memory_order_release);
    }

private:
    std::atomic<Generation> pendingGeneration_ { 0 };
};

} // namespace DAW
