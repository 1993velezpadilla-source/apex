// APEX Bubblegum Limiter — wet signal phase, magnitude and PDC audit.
// Actual AUDIO oversampling and limiter Core, not a mocked detector.
// No JUCE dependency. Diagnostic until we prove sample-accurate wet PDC.
// C++17, 44.1/48/96 kHz, 0.5-20 kHz bands and antialias region covered.
#include "../../Source/LimiterCore/ApexLimiterCore.h"
#include "../../Source/LimiterCore/ApexLimiterOversampling.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
constexpr std::array<int,3> rates{{44100,48000,96000}};
constexpr int frames=8192;
constexpr int analysisStart=4096;
constexpr int period=frames-analysisStart;
constexpr std::array<int,5> frequencies{{500,4000,8000,16000,20000}};
using Bins=std::array<int,5>;
Bins chooseBins(int rate)
{
    Bins bins{};
    for(size_t i=0;i<bins.size();++i)
        bins[i]=static_cast<int>(std::lround(
            static_cast<double>(frequencies[i])*period/rate));
    return bins;
}
// Separate exact FFT bins avoid window leakage and let us measure multiple
// frequencies using ONE simultaneous musical multi-tone per factor.
using APEX::Limiter::Core;
using APEX::Limiter::Settings;
using APEX::Limiter::AudioRateConverter;

void coreCallback(void* ctx,float* left,float* right,int count) noexcept
{
    static_cast<Core*>(ctx)->process(left,right,count);
}
std::vector<float> reference(const Bins& bins)
{
    std::vector<float> x(static_cast<size_t>(frames),0.0f);
    for(int n=0;n<frames;++n)
    {
        double value=0.0;
        for(size_t i=0;i<bins.size();++i)
            value+=0.072*std::cos(2*pi*bins[i]*n/period+0.13*i);
        x[static_cast<size_t>(n)]=static_cast<float>(value);
    }
    return x;
}
std::complex<double> component(const std::vector<float>& x,int bin)
{
    std::complex<double> sum{0,0};
    for(int n=analysisStart;n<frames;++n)
    {
        const double phase=2*pi*bin*n/period;
        sum+=static_cast<double>(x[static_cast<size_t>(n)])*
             std::complex<double>{std::cos(phase),-std::sin(phase)};
    }
    return sum;
}
std::vector<float> render(int rate,int factor,int block,const std::vector<float>& input,
                          int& coreLatency,double& firLatency)
{
    Core core;
    core.prepare(rate*factor);
    Settings p;
    p.gainDb=0.0;
    p.ceilingDb=-0.1;
    p.truePeak=false;
    p.detectorQuality=0;
    p.ditherMode=0;
    core.setSettings(p);
    coreLatency=core.latencySamples();
    AudioRateConverter resampler;
    // Use the SAME 25 high-rate alignment frames as the JUCE host.
    resampler.prepare(factor,128,factor>1?25:0);
    firLatency=resampler.nominalFilterDelayFrames();
    auto x=input;
    for(int n=0;n<frames;n+=block)
        resampler.process(x.data()+n,nullptr,std::min(block,frames-n),
                          &core,coreCallback);
    return x;
}
}
int main()
{
    std::puts("APEX_WET_PHASE columns: factor,frequency_hz,reported_pdc,exact_nominal_delay,amplitude_ratio,phase_error_reported_rad,phase_error_exact_rad,measured_delay_modulo_period,host_rate_hz");
    for(const int rate:rates)
    {
      const auto bins=chooseBins(rate);
      const auto input=reference(bins);
      for(const int factor:{1,2,4,8,16,32})
      {
        int coreLatency=0;
        double firLatency=0;
        const auto output=render(rate,factor,73,input,coreLatency,firLatency);
        int compareLatency=0;
        double compareFir=0;
        const auto split=render(rate,factor,1024,input,compareLatency,compareFir);
        double maximumDifference=0.0;
        for(int n=0;n<frames;++n)
            maximumDifference=std::max(maximumDifference,std::abs(
                static_cast<double>(output[static_cast<size_t>(n)]-
                                    split[static_cast<size_t>(n)])));
        if(maximumDifference>1.0e-5 || compareLatency!=coreLatency ||
           std::abs(compareFir-firLatency)>1.0e-12)
        {
            std::fprintf(stderr,"Wet host block invariance failed at %dx: %.9f\n",
                         factor,maximumDifference);
            return EXIT_FAILURE;
        }
        const double nominal=static_cast<double>(coreLatency)/factor+firLatency;
        const int reported=static_cast<int>(std::lround(nominal));
        for(const int bin:bins)
        {
            const auto ratio=component(output,bin)/component(input,bin);
            const double omega=2*pi*bin/period;
            const double magnitude=std::abs(ratio);
            const double phase=std::arg(ratio);
            const double deltaReport=std::remainder(phase+omega*reported,2*pi);
            const double deltaNominal=std::remainder(phase+omega*nominal,2*pi);
            const double measuredDelay=std::remainder(-phase,2*pi)/omega;
            std::printf("APEX_WET_PHASE,%d,%.3f,%d,%.6f,%.8f,%+.8f,%+.8f,%.6f,%d\n",
                        factor,static_cast<double>(bin)*rate/period,
                        reported,nominal,magnitude,deltaReport,deltaNominal,
                        measuredDelay,rate);
            // A mathematically linear-phase FIR, a pure high-rate
            // integer delay, and a transparent non-limiting Core must have
            // an effectively ZERO phase error at 0.5–20 kHz.
            // This catches the old 0.5..0.97 host-sample PDC defect.
            if(!std::isfinite(magnitude) ||
               std::abs(magnitude-1.0)>0.015 ||
               !std::isfinite(deltaNominal) ||
               std::abs(deltaNominal)>0.015 ||
               std::abs(deltaReport)>0.015)
            {
                std::fprintf(stderr,
                    "PDC / wet phase QUALITY GATE failed at %dHz, %dx %.1fHz: reported %.6f rad, exact %.6f rad, gain %.6f\n",
                    rate,factor,static_cast<double>(bin)*rate/period,
                    deltaReport,deltaNominal,magnitude);
                return EXIT_FAILURE;
            }
        }
      }
    }
    std::puts("APEX_WET_PHASE 44.1/48/96k strict 0.015-radian/0.015-gain regression PASS (not certified).");
}
