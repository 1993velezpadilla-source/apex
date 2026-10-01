#pragma once
#include <atomic>
#include <cstdint>

namespace DAW {

class AudioResourceReleaseStateCore
{
public:
    using Generation = uint64_t;

    enum class StopAction : uint8_t
    {
        none,
        defer,
        release
    };

    enum class FinishAction : uint8_t
    {
        none,
        release
    };

    Generation deviceStarted() noexcept
    {
        auto current = state_.load(std::memory_order_acquire);
        for (;;)
        {
            const auto nextGeneration = unpackGeneration(current) + 1;
            const auto currentState = unpackState(current);
            const auto nextState = currentState == State::runningExportOwned
                || currentState == State::stoppedDeferred
                    ? State::runningExportOwned : State::running;
            const auto desired = pack(nextGeneration, nextState);
            if (state_.compare_exchange_weak(
                    current, desired, std::memory_order_acq_rel, std::memory_order_acquire))
                return nextGeneration;
        }
    }

    Generation claimExport() noexcept
    {
        auto current = state_.load(std::memory_order_acquire);
        for (;;)
        {
            if (unpackState(current) != State::running)
                return 0;

            const auto generation = unpackGeneration(current);
            const auto desired = pack(generation, State::runningExportOwned);
            if (state_.compare_exchange_weak(
                    current, desired, std::memory_order_acq_rel, std::memory_order_acquire))
                return generation;
        }
    }

    StopAction deviceStopped() noexcept
    {
        auto current = state_.load(std::memory_order_acquire);
        for (;;)
        {
            const auto currentState = unpackState(current);
            if (currentState != State::running && currentState != State::runningExportOwned)
                return StopAction::none;

            const bool exportOwned = currentState == State::runningExportOwned;
            const auto desiredState = exportOwned ? State::stoppedDeferred : State::stoppedReleased;
            const auto desired = pack(unpackGeneration(current), desiredState);
            if (state_.compare_exchange_weak(
                    current, desired, std::memory_order_acq_rel, std::memory_order_acquire))
                return exportOwned ? StopAction::defer : StopAction::release;
        }
    }

    FinishAction finishExport() noexcept
    {
        auto current = state_.load(std::memory_order_acquire);
        for (;;)
        {
            const auto currentState = unpackState(current);
            if (currentState != State::runningExportOwned
                && currentState != State::stoppedDeferred)
                return FinishAction::none;

            const auto desiredState = currentState == State::stoppedDeferred
                ? State::stoppedReleased : State::running;
            const auto desired = pack(unpackGeneration(current), desiredState);
            if (state_.compare_exchange_weak(
                    current, desired, std::memory_order_acq_rel, std::memory_order_acquire))
                return currentState == State::stoppedDeferred
                    ? FinishAction::release : FinishAction::none;
        }
    }

    bool isDeviceRunning() const noexcept
    {
        const auto state = unpackState(state_.load(std::memory_order_acquire));
        return state == State::running || state == State::runningExportOwned;
    }

    bool isExportOwned() const noexcept
    {
        const auto state = unpackState(state_.load(std::memory_order_acquire));
        return state == State::runningExportOwned || state == State::stoppedDeferred;
    }

    Generation getGeneration() const noexcept
    {
        return unpackGeneration(state_.load(std::memory_order_acquire));
    }

    bool canApplyPreparation(Generation generation) const noexcept
    {
        const auto current = state_.load(std::memory_order_acquire);
        return unpackState(current) == State::running
            && unpackGeneration(current) == generation;
    }

    bool isReleaseDeferred() const noexcept
    {
        return unpackState(state_.load(std::memory_order_acquire)) == State::stoppedDeferred;
    }

private:
    enum class State : uint64_t
    {
        running = 0,
        runningExportOwned = 1,
        stoppedDeferred = 2,
        stoppedReleased = 3
    };

    static constexpr uint64_t stateBits = 2;
    static constexpr uint64_t stateMask = (uint64_t { 1 } << stateBits) - 1;

    static constexpr uint64_t pack(Generation generation, State state) noexcept
    {
        return (generation << stateBits) | static_cast<uint64_t>(state);
    }

    static constexpr Generation unpackGeneration(uint64_t packed) noexcept
    {
        return packed >> stateBits;
    }

    static constexpr State unpackState(uint64_t packed) noexcept
    {
        return static_cast<State>(packed & stateMask);
    }

    std::atomic<uint64_t> state_ { pack(0, State::stoppedReleased) };
};

} // namespace DAW
