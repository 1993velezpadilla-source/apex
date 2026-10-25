// APEX limiter cached sliding-peak logarithm adversarial comparison.
// Build twice: default fast cache and -DAPEX_LIMITER_REFERENCE_PEAK_LOG.
// Both binaries must produce equal finite samples under runtime automation.
#include "../../Source/LimiterCore/ApexLimiterCore.h"
#include "../../Source/LimiterCore/ApexLimiterOversampling.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace APEX::Limiter;
namespace {
constexpr int length=8192;
constexpr int rate=48000;
constexpr double pi=3.14159265358979323846;
void runCore(void* context,float* left,float* right,int count) noexcept
{ static_cast<Core*>(context)->process(left,right,count); }
}
int main()
{
    for(const int factor:{2,4,8,16,32})
      for(const int stereo:{0,1})
      {
        AudioRateConverter converter;
        converter.prepare(factor,128,57);
        Core core;
        core.prepare(rate*factor,true,true);
        std::vector<float> left(length),right(length);
        for(int n=0;n<length;++n)
        {
            double t=static_cast<double>(n)/rate;
            left[static_cast<size_t>(n)]=static_cast<float>(
                0.40*std::sin(2*pi*757*t)
               +0.27*std::sin(2*pi*9833*t+0.7)
               +(n%301<3?0.41:0.0));
            right[static_cast<size_t>(n)]=static_cast<float>(
                0.44*std::sin(2*pi*499*t+0.3)
               +0.14*std::sin(2*pi*11033*t)
               -(n%409<2?0.38:0.0));
        }
        int base=0;
        int segment=0;
        while(base<length)
        {
            // A change of ceiling is especially important: a cache keyed
            // ONLY on the peak would return the wrong reduction in dB.
            Settings settings;
            settings.gainDb=segment%3==0 ? 5.0 : (segment%3==1?14.0:0.0);
            settings.ceilingDb=segment%4==0 ? -1.0
                : (segment%4==1?-6.0:(segment%4==2?-0.1:-12.0));
            settings.lookaheadMs=segment%2==0 ? 5.0 : 0.5;
            settings.channelLink=segment%3==0?1.0:0.2;
            settings.releaseLink=segment%2==0?1.0:0.4;
            settings.style=segment%8;
            settings.truePeak=false;
            settings.detectorQuality=0;
            core.setSettings(settings);
            const int amount=std::min(length-base,segment%2==0?113:37);
            converter.process(left.data()+base,stereo?right.data()+base:nullptr,
                              amount,&core,runCore);
            base+=amount;
            ++segment;
        }
        for(int n=0;n<length;++n)
        {
            const float l=left[static_cast<size_t>(n)];
            const float r=right[static_cast<size_t>(n)];
            if(!std::isfinite(l)||!std::isfinite(r))return EXIT_FAILURE;
            std::printf("%d,%d,%d,%.9g,%.9g\n",
                        factor,stereo,n,static_cast<double>(l),
                        static_cast<double>(r));
        }
      }
}
