#pragma once
#include <atomic>
#include <cstdint>
#include <utility>

namespace DAW {

template <typename Value>
class AsyncCompletionStateCore
{
public:
    bool publish(Value value)
    {
        if (state_.load(std::memory_order_acquire) != State::empty)
            return false;

        value_ = std::move(value);
        auto expected = State::empty;
        return state_.compare_exchange_strong(
            expected, State::pending, std::memory_order_release, std::memory_order_acquire);
    }

    bool consume(Value& value)
    {
        auto expected = State::pending;
        if (!state_.compare_exchange_strong(
                expected, State::consumed, std::memory_order_acq_rel, std::memory_order_acquire))
            return false;

        value = value_;
        return true;
    }

    void suppress() noexcept
    {
        auto current = state_.load(std::memory_order_acquire);
        while (current == State::empty || current == State::pending)
            if (state_.compare_exchange_weak(
                    current, State::suppressed, std::memory_order_acq_rel, std::memory_order_acquire))
                return;
    }

private:
    enum class State : uint8_t
    {
        empty,
        pending,
        consumed,
        suppressed
    };

    Value value_ {};
    std::atomic<State> state_ { State::empty };
};

} // namespace DAW
