// APEX Limiter — independently reconstructed TRUE-PEAK diagnostics.
// This is an ADVERSARIAL QUALITY REPORT, NOT ITU/EBU certification.
// High-rate Core and actual antialias decimation are exercised; the offline
// 32x windowed-sinc reference independently measures hidden intersample peaks.
// Do not allow a successful diagnostic run to claim a 0 dBTP release guard.
#include "../../Source/LimiterCore/ApexLimiterCore.h"
#include "../../Source/LimiterCore/ApexLimiterOversampling.h"
#include "ApexLimiterTruePeakReference.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using APEX::Limiter::AudioRateConverter;
using APEX::Limiter::Core;
using APEX::Limiter::Settings;
using APEX::Limiter::TruePeakReference;
namespace {
constexpr double pi=3.14159265358979323846;
constexpr int frames=4096;
constexpr std::array<int,3> rates{{44100,48000,96000}};
constexpr std::array<int,4> factors{{1,4,16,32}};
constexpr double ceilingDb=-1.0;
constexpr double ceiling=0.8912509381337456;

void processHigh(void* ctx,float* l,float* r,int n) noexcept
{ static_cast<Core*>(ctx)->process(l,r,n); }

std::vector<float> programme(int rate,int profile)
{
    std::vector<float> x(static_cast<size_t>(frames),0.0f);
    for(int n=0;n<frames;++n)
    {
        const double t=static_cast<double>(n)/rate;
        double value=0.0;
        if(profile==0)
            value=0.71*std::sin(2*pi*rate*0.25*t+pi*0.25);
        else if(profile==1)
        {
            // No file dependency: fixed reproducible multiband programme
            // with strong stereo-mastering transients.
            value=0.30*std::sin(2*pi*173*t)
                 +0.26*std::sin(2*pi*997*t+0.35)
                 +0.19*std::sin(2*pi*3700*t+0.66)
                 +0.13*std::sin(2*pi*11000*t+0.12);
            if(n%527<3)value+=0.57;
        }
        else
            value=0.74*std::sin(2*pi*0.44*rate*t+pi*0.29)
                 +(n%911<2?0.35:0.0);
        x[static_cast<size_t>(n)]=static_cast<float>(value);
    }
    return x;
}
std::vector<float> render(int rate,int factor,const std::vector<float>& source,
                          bool& reductionObserved)
{
    Core core;
    core.prepare(static_cast<double>(rate)*factor);
    Settings p;
    p.gainDb=12.0;
    p.ceilingDb=ceilingDb;
    p.attackMs=0.01;
    p.releaseMs=55.0;
    p.lookaheadMs=5.0;
    p.detectorQuality=0;
    p.truePeak=false;
    p.ditherMode=0;
    core.setSettings(p);
    AudioRateConverter converter;
    converter.prepare(factor,128,factor>1?57:0);
    auto output=source;
    reductionObserved=false;
    // Deliberately odd-sized callbacks: the converter must carry its
    // FIR, high-rate alignment and stereo-independent stream history.
    for(int base=0;base<frames;base+=113)
    {
        const int count=std::min(113,frames-base);
        converter.process(output.data()+base,nullptr,count,&core,processHigh);
        reductionObserved|=core.blockReductionMaxDb()>0.05;
        // Equivalent to the native host post-decimation SAMPLE ceiling.
        // This clamp cannot guarantee the independently measured TP.
        for(int n=base;n<base+count;++n)
        {
            auto& v=output[static_cast<size_t>(n)];
            v=static_cast<float>(std::clamp(static_cast<double>(v),-ceiling,ceiling));
        }
    }
    return output;
}
}
int main()
{
    std::puts("APEX_TRUEPEAK columns: rate,factor,profile,sample_peak,reconstructed_32x_peak,ceiling,overshoot_dbTP,limiting_active");
    double maximumOvershoot=-1000.0;
    int overshootCases=0;
    for(const int rate:rates)
        for(const int profile:{0,1,2})
        {
            const auto original=programme(rate,profile);
            for(const int factor:factors)
            {
                bool limited=false;
                const auto out=render(rate,factor,original,limited);
                const auto tp=TruePeakReference::analyze(out.data(),frames,32);
                const double overshoot=20.0*std::log10(
                    std::max(1.0e-12,tp.reconstructedPeak)/ceiling);
                std::printf("APEX_TRUEPEAK,%d,%d,%d,%.9f,%.9f,%.9f,%+.5f,%d\n",
                            rate,factor,profile,tp.samplePeak,
                            tp.reconstructedPeak,ceiling,overshoot,limited?1:0);
                if(!limited || !std::isfinite(tp.reconstructedPeak) ||
                   tp.samplePeak>ceiling+1.0e-6 ||
                   tp.reconstructedPeak>2.0)
                {
                    std::fprintf(stderr,
                        "APEX true-peak diagnostics invalid at %dHz %dx profile %d\n",
                        rate,factor,profile);
                    return EXIT_FAILURE;
                }
                maximumOvershoot=std::max(maximumOvershoot,overshoot);
                if(overshoot>0.02)++overshootCases;
                // A diagnostic safety REGRESSION gate, not certification:
                // at the highest 32x DSP rate the independent 32x
                // reconstructed peak must remain within +0.10 dB of the
                // user-selected sample ceiling for THIS adversarial matrix.
                // APEX still does not guarantee 0 dBTP or BS.1770 accuracy.
                if(factor==32 && overshoot>0.10)
                {
                    std::fprintf(stderr,
                        "APEX 32x TP REGRESSION: %dHz profile %d exceeded "
                        "+0.10 dB tolerance: %+0.5f dBTP\n",
                        rate,profile,overshoot);
                    return EXIT_FAILURE;
                }
            }
        }
    std::printf("APEX_TRUEPEAK_SUMMARY,max_overshoot_db=%.5f,overshoot_cases=%d/36\n",
                maximumOvershoot,overshootCases);
    if(overshootCases)
        std::puts("APEX_TRUEPEAK_WARNING: post-OS sample ceiling has NOT met the 32x offline true-peak ceiling on every case; retain release block.");
    else
        std::puts("APEX_TRUEPEAK_WARNING: directional 32x windowed-sinc probe does NOT certify BS.1770 or all transient cases.");
}
