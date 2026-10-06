#pragma once
#include <atomic>

namespace DAW {

enum class ClickActiveMode
{
    Off             = 0,
    OnDuringRecord  = 1,
    Always          = 2
};

enum class ClickCountInBars
{
    None     = 0,
    OneBar   = 1,
    TwoBars  = 2,
    FourBars = 4
};

struct ClickStateModel
{
    std::atomic<int>   activeMode      { (int) ClickActiveMode::OnDuringRecord };
    std::atomic<int>   countInBars     { (int) ClickCountInBars::OneBar };
    std::atomic<float> volumeLinear    { 0.6f };
    std::atomic<bool>  muted           { false };
    std::atomic<int>   beatsPerBar     { 4 };
    std::atomic<int>   beatUnit        { 4 };
    std::atomic<bool>  countInActive   { false };

    ClickActiveMode getActiveMode() const noexcept
    {
        return (ClickActiveMode) activeMode.load(std::memory_order_relaxed);
    }

    ClickCountInBars getCountInBars() const noexcept
    {
        return (ClickCountInBars) countInBars.load(std::memory_order_relaxed);
    }
};

} // namespace DAW
