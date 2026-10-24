// APEX experimental limiter: compare unoptimised vs symmetric FIR builds.
// Compile this exact file twice, with and without
// -DAPEX_LIMITER_OS_REFERENCE_FIR, then compare EVERY printed output sample.
// The two binaries share input, host callback segmentation and Core settings.
// This is an offline regression, NOT an automatic real-time guarantee.
#include "../../Source/LimiterCore/ApexLimiterCore.h"
#include "../../Source/LimiterCore/ApexLimiterOversampling.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using APEX::Limiter::AudioRateConverter;
using APEX::Limiter::Core;
using APEX::Limiter::Settings;
namespace {
constexpr int rate=48000;
constexpr int frames=4096;
constexpr double pi=3.14159265358979323846;
void identity(void*,float*,float*,int) noexcept {}
void limiter(void* ctx,float* l,float* r,int count) noexcept
{
    static_cast<Core*>(ctx)->process(l,r,count);
}
}
int main()
{
    for(const int factor:{1,2,4,8,16,32})
        for(const int block:{37,128,511})
            for(const int mode:{0,1})
            {
                std::vector<float> left(frames),right(frames);
                for(int n=0;n<frames;++n)
                {
                    const double t=static_cast<double>(n)/rate;
                    const double a=0.43*std::sin(2*pi*431*t)
                                   +0.22*std::sin(2*pi*9831*t+0.67)
                                   +(n%509<2?0.8:0.0);
                    const double b=0.40*std::sin(2*pi*727*t+0.19)
                                   +0.30*std::sin(2*pi*11541*t)
                                   -(n%701<3?0.7:0.0);
                    left[static_cast<size_t>(n)]=static_cast<float>(a);
                    right[static_cast<size_t>(n)]=static_cast<float>(b);
                }
                AudioRateConverter converter;
                converter.prepare(factor,512,factor>1?57:0);
                Core core;
                core.prepare(rate*factor,factor>1,factor>1);
                Settings settings;
                settings.gainDb=11.0;
                settings.ceilingDb=-1.0;
                settings.attackMs=0.01;
                settings.releaseMs=55.0;
                settings.lookaheadMs=5.0;
                settings.truePeak=false;
                settings.detectorQuality=0;
                core.setSettings(settings);
                for(int n=0;n<frames;n+=block)
                {
                    const int count=std::min(block,frames-n);
                    converter.process(left.data()+n,right.data()+n,
                                      count,mode?static_cast<void*>(&core):nullptr,
                                      mode?limiter:identity);
                }
                for(int n=0;n<frames;++n)
                {
                    const float l=left[static_cast<size_t>(n)];
                    const float r=right[static_cast<size_t>(n)];
                    if(!std::isfinite(l)||!std::isfinite(r))return EXIT_FAILURE;
                    std::printf("%d,%d,%d,%d,%.9g,%.9g\n",
                                factor,block,mode,n,
                                static_cast<double>(l),static_cast<double>(r));
                }
            }
}
