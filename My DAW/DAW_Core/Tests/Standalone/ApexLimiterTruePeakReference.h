#pragma once
// APEX offline true-peak audit reference (no realtime use).
// Windowed-sinc reconstruction exposes intersample overshoots that ordinary
// sample-peak ceilings can miss. Not a certified ITU BS.1770 measurement.
// Runs only in standalone DSP regression tests; no audio-thread allocations.
#include <algorithm>
#include <cmath>

namespace APEX { namespace Limiter {

struct TruePeakReferenceResult
{
    double samplePeak = 0.0;
    double reconstructedPeak = 0.0;
};

class TruePeakReference final
{
public:
    // Excludes 32 input frames at either end to avoid artificial edges.
    // This is an offline, finite-signal regression comparator, not a meter.
    static TruePeakReferenceResult analyze(const float* data,int frames,
                                           int factor=32) noexcept
    {
        TruePeakReferenceResult out;
        if(!data || frames<=0)return out;
        for(int i=0;i<frames;++i)
        {
            const double peak=std::isfinite(data[i])
                 ?std::abs(static_cast<double>(data[i])):0.0;
            out.samplePeak=std::max(out.samplePeak,peak);
        }
        if(frames<2*kGuard+1 || factor<=0)return out;
        factor=std::clamp(factor,1,32);
        constexpr double pi=3.14159265358979323846;
        for(int i=kGuard;i<frames-kGuard;++i)
        {
            for(int phase=0;phase<factor;++phase)
            {
                const double fractional=static_cast<double>(phase)/factor;
                double reconstructed=0.0,weights=0.0;
                for(int k=-kGuard+1;k<=kGuard;++k)
                {
                    const double displacement=static_cast<double>(k)-fractional;
                    const double absD=std::abs(displacement);
                    if(absD>=kGuard)continue;
                    const double sinc=absD<1.0e-12 ? 1.0
                                  : std::sin(pi*displacement)/(pi*displacement);
                    const double window=0.42
                        +0.5*std::cos(pi*displacement/kGuard)
                        +0.08*std::cos(2.0*pi*displacement/kGuard);
                    const double w=sinc*window;
                    reconstructed+=w*static_cast<double>(data[i+k]);
                    weights+=w;
                }
                if(std::abs(weights)>1.0e-12)
                    out.reconstructedPeak=std::max(out.reconstructedPeak,
                                        std::abs(reconstructed/weights));
            }
        }
        return out;
    }
private:
    static constexpr int kGuard=32;
};

}} // namespace APEX::Limiter
