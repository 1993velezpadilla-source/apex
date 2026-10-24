#pragma once
// APEX Limiter — experimental post-reconstruction true-peak reserve.
// A modest headroom allowance is applied BEFORE actual audio-rate limiting
// when the user enables True Peak, never when True Peak is OFF or Bypass ON.
// The original user-selected ceiling remains the FINAL HOST sample ceiling.
// A safety reserve is not a substitute for a certified BS.1770 true-peak
// algorithm; the numbers are provisional until broader ISP test coverage.
namespace APEX { namespace Limiter {

inline double audioTruePeakReserveDb(int factor,bool enabled) noexcept
{
    if(!enabled || factor<=1)return 0.0;
    switch(factor)
    {
        case 2: return 0.35; // measured 2x ISP guard leaves ~0.11dB reserve
        case 4: return 0.25;
        case 8: return 0.15;
        case 16:return 0.085;
        case 32:return 0.075;
        default:return 0.0;
    }
}

}} // namespace APEX::Limiter
