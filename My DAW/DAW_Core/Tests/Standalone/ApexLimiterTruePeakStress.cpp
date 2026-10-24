// APEX Limiter — independently reconstructed TRUE-PEAK diagnostics.
// This is an ADVERSARIAL QUALITY REPORT, NOT ITU/EBU certification.
// High-rate Core and actual antialias decimation are exercised; the offline
// 32x windowed-sinc reference independently measures hidden intersample peaks.
// Do not allow a successful diagnostic run to claim a 0 dBTP release guard.
#include "../../Source/LimiterCore/ApexLimiterCore.h"
#include "../../Source/LimiterCore/ApexLimiterOversampling.h"
#include "../../Source/LimiterCore/ApexLimiterTruePeakReserve.h"
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
constexpr std::array<int,6> factors{{1,2,4,8,16,32}};
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
                          bool requestTruePeak,bool& reductionObserved,
                          bool enableNative1xDetector=false)
{
    Core core;
    core.prepare(static_cast<double>(rate)*factor,factor>1);
    Settings p;
    p.gainDb=12.0;
    p.ceilingDb=ceilingDb-APEX::Limiter::audioTruePeakReserveDb(
        factor,requestTruePeak);
    p.attackMs=0.01;
    p.releaseMs=55.0;
    p.lookaheadMs=5.0;
    // Native 1x FIR ISP detector runs at the original project rate, not
    // the audio oversampling path. It is deliberately measured separately
    // from the historical 1x sample-only RED control.
    p.truePeak=factor==1 && enableNative1xDetector;
    p.detectorQuality=p.truePeak?3:0;  // 8x FIR reconstruction at native rate.
    // Above 1x, full-rate audio + empirical reserve replaces duplicate FIR.
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
    int overshootCases=0,totalCases=0;
    for(const int rate:rates)
        for(const int profile:{0,1,2})
        {
            const auto original=programme(rate,profile);
            for(const int factor:factors)
            {
                bool limited=false;
                const auto out=render(rate,factor,original,true,limited);
                ++totalCases;
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
                // True Peak ON with 2x–32x real AUDIO oversampling must
                // respect the +0.01 dB ISP regression tolerance for every
                // fixed synthetic stress case, not just high-quality 32x.
                // 1x sample-only is an intentionally exposed legacy RED
                // baseline; this does not certify a BS.1770 meter/limiter.
                if(factor>1 && overshoot>0.01)
                {
                    std::fprintf(stderr,
                        "APEX %dx TP REGRESSION: %dHz profile %d exceeded "
                        "+0.01 dB tolerance: %+0.5f dBTP\n",
                        factor,rate,profile,overshoot);
                    return EXIT_FAILURE;
                }
            }
        }
    // Separate 1x TRUE PEAK ON probe: the baseline matrix above uses
    // 1x TP OFF intentionally. Exercise the native 8x-windowed-sinc
    // detector at all three native rates; compare it against TP OFF.
    // Measurements are directional, not a claim of BS.1770 compliance.
    int nativeChangedRates=0;
    std::puts("APEX_NATIVE_1X_TP columns: rate,profile,tp_off,tp_on,on_overshoot_dbTP,max_sample_delta,reduction_active");
    for(const int rate:rates)
    {
        double maxRateDelta=0.0;
        for(const int profile:{0,1,2})
        {
            const auto source=programme(rate,profile);
            bool reducedOff=false,reducedOn=false;
            const auto off=render(rate,1,source,false,reducedOff);
            const auto on=render(rate,1,source,true,reducedOn,true);
            const auto offPeak=TruePeakReference::analyze(off.data(),frames,32);
            const auto onPeak=TruePeakReference::analyze(on.data(),frames,32);
            double maxDelta=0.0;
            for(int n=0;n<frames;++n)
                maxDelta=std::max(maxDelta,std::abs(
                    static_cast<double>(on[static_cast<size_t>(n)])
                   -static_cast<double>(off[static_cast<size_t>(n)])));
            const double overshoot=20.0*std::log10(
                std::max(1.0e-12,onPeak.reconstructedPeak)/ceiling);
            std::printf("APEX_NATIVE_1X_TP,%d,%d,%.9f,%.9f,%+.5f,%.9f,%d\n",
                        rate,profile,offPeak.reconstructedPeak,
                        onPeak.reconstructedPeak,overshoot,maxDelta,reducedOn?1:0);
            if(!reducedOff||!reducedOn||!std::isfinite(overshoot)||
               !std::isfinite(maxDelta)||onPeak.samplePeak>ceiling+1.0e-6)
            {
                std::fprintf(stderr,"Native 1x TP detector regression at %dHz profile %d\n",
                             rate,profile);
                return EXIT_FAILURE;
            }
            maxRateDelta=std::max(maxRateDelta,maxDelta);
        }
        // Confirm the native FIR changes real rendered AUDIO at every rate.
        // This does not assert perfect ISP compliance or loudness matching.
        if(maxRateDelta>1.0e-7)++nativeChangedRates;
    }
    if(nativeChangedRates!=static_cast<int>(rates.size()))
    {
        std::fprintf(stderr,"Native 1x detector did not change audio at all rates\n");
        return EXIT_FAILURE;
    }
    std::printf("APEX_TRUEPEAK_SUMMARY,max_overshoot_db=%.5f,overshoot_cases=%d/%d\n",
                maximumOvershoot,overshootCases,totalCases);
    // Explicit A/B: switching TP OFF must remove the headroom reserve.
    // This also guards against permanently attenuating the mix when
    // the user turns off TP, audition or bypass.
    {
        const auto source=programme(48000,1);
        bool onReduction=false,offReduction=false;
        const auto on=render(48000,32,source,true,onReduction);
        const auto off=render(48000,32,source,false,offReduction);
        const double tpOn=TruePeakReference::analyze(on.data(),frames,32).reconstructedPeak;
        const double tpOff=TruePeakReference::analyze(off.data(),frames,32).reconstructedPeak;
        const double deltaDb=20.0*std::log10(tpOff/std::max(1.0e-12,tpOn));
        std::printf("APEX_TRUEPEAK_TOGGLE,32,48000,%.6f,%.6f,%.6f\n",
                    tpOn,tpOff,deltaDb);
        if(!onReduction || !offReduction || deltaDb<0.02)
        {
            std::fprintf(stderr,"TP toggle failed to restore nonreserved audio\n");
            return EXIT_FAILURE;
        }
    }
    if(overshootCases)
        std::puts("APEX_TRUEPEAK_WARNING: some ISP cases (especially 1x sample-only) exceed the ceiling; this stress matrix is NOT certification.");
    else
        std::puts("APEX_TRUEPEAK_WARNING: directional 32x windowed-sinc probe does NOT certify BS.1770 or all transient cases.");
}
