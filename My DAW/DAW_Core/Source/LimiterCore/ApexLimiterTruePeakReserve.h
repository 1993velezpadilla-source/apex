#pragma once
// APEX Limiter — experimental post-reconstruction true-peak reserve.
// A modest headroom allowance is applied BEFORE actual audio-rate limiting
// when the user enables True Peak, never when True Peak is OFF or Bypass ON.
// The original user-selected ceiling remains the FINAL HOST sample ceiling.
// A safety reserve is not a substitute for a certified BS.1770 true-peak
// algorithm; the numbers are provisional until broader ISP test coverage.
namespace APEX { namespace Limiter {

// Highest-precision *native 1x* FIR detector remains experimental. In
// adversarial 44.1/48/96kHz test material, the unreserved 8x detector
// permitted +0.03211dBTP after the sample ceiling. A 0.06dB ceiling
// allowance is a conservative initial synthetic-test guard, NOT a general
// intersample-peak or BS.1770 guarantee. Only enable for quality=8x (3):
// lower FIR detector qualities are not yet calibrated.
inline double native1xTruePeakReserveDb(int detectorQuality,bool enabled) noexcept
{
    return enabled && detectorQuality==3 ? 0.06 : 0.0;
}

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
