// APEX limiter — portable C++17 realtime/DSP regression suite.
#include "../../Source/LimiterCore/ApexLimiterCore.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using APEX::Limiter::Core;
using APEX::Limiter::Settings;
static constexpr double kRate = 48000.0;
static constexpr double kPi = 3.14159265358979323846;

static std::vector<float> signal(int size,double amplitude)
{
    std::vector<float> samples(static_cast<size_t>(size));
    for (int i=0;i<size;++i)
        samples[static_cast<size_t>(i)]=static_cast<float>(
            amplitude*std::sin(2.0*kPi*997.0*i/kRate));
    return samples;
}
static double maxAbs(const std::vector<float>& v)
{
    double peak = 0.0;
    for (auto x:v)
    {
        assert(std::isfinite(x));
        peak=std::max(peak,static_cast<double>(std::abs(x)));
    }
    return peak;
}
static void runCeiling(int block,int style,int quality)
{
    Core core; core.prepare(kRate);
    Settings p; p.ceilingDb = -1.0; p.gainDb=24.0; p.style=style;
    p.lookaheadMs=5; p.detectorQuality=quality;
    core.setSettings(p);
    auto l=signal(24000,0.91),r=signal(24000,0.73);
    for (int offset=0;offset<static_cast<int>(l.size());offset+=block)
    {
        const int n=std::min(block,static_cast<int>(l.size())-offset);
        core.process(l.data()+offset,r.data()+offset,n);
    }
    const double ceiling=std::pow(10.0,p.ceilingDb/20.0);
    assert(maxAbs(l)<=ceiling+1.0e-5);
    assert(maxAbs(r)<=ceiling+1.0e-5);
    assert(core.reductionDb()>=0.0);
    assert(core.latencySamples()>=static_cast<int>(0.02*kRate));
}
static void testSilenceAndCorruptInputs()
{
    Core core;core.prepare(kRate);
    std::vector<float> l(13000,0),r(13000,0);
    core.process(l.data(),r.data(),static_cast<int>(l.size()));
    assert(maxAbs(l)==0.0 && maxAbs(r)==0.0);
    l[0]=std::numeric_limits<float>::quiet_NaN();
    l[1]=std::numeric_limits<float>::infinity();
    l[2]=-std::numeric_limits<float>::infinity();
    core.process(l.data(),r.data(),static_cast<int>(l.size()));
    assert(std::isfinite(maxAbs(l)));
}
static void testNoUnwantedGainBelowCeiling()
{
    Core core;core.prepare(kRate);
    Settings p; p.gainDb=0; p.ceilingDb=-1; p.truePeak=false;
    core.setSettings(p);
    const auto original=signal(10000,0.10);
    auto samples=original;
    core.process(samples.data(),nullptr,static_cast<int>(samples.size()));
    const int delay=core.latencySamples();
    for(int n=delay+100;n<static_cast<int>(samples.size());++n)
        assert(std::abs(samples[static_cast<size_t>(n)]
             -original[static_cast<size_t>(n-delay)])<1.0e-5);
}
static void testBlockSizeIndependence()
{
    Settings p; p.gainDb=14.0; p.ceilingDb=-0.7; p.style=1; p.lookaheadMs=0.5;
    Core a,b; a.prepare(kRate); b.prepare(kRate);
    a.setSettings(p);b.setSettings(p);
    auto whole=signal(12000,0.7);
    auto split=whole;
    a.process(whole.data(),nullptr,static_cast<int>(whole.size()));
    for(int i=0;i<static_cast<int>(split.size());i+=127)
    {
        const auto n=std::min(127,static_cast<int>(split.size())-i);
        b.process(split.data()+i,nullptr,n);
    }
    for(size_t i=0;i<whole.size();++i)
        assert(std::abs(whole[i]-split[i])<1.0e-6);
}
static void testBypassIsTrulyDryAndLatencyAligned()
{
    Core core; core.prepare(kRate);
    Settings p; p.gainDb=25.0; p.ceilingDb=-8.0;
    p.dcFilter=true; p.bypass=true;
    core.setSettings(p); core.reset();
    auto original=signal(12000,0.8), output=original;
    core.process(output.data(),nullptr,static_cast<int>(output.size()));
    const int delay=core.latencySamples();
    for (int i=0;i<static_cast<int>(output.size());++i)
    {
        const float expected=i>=delay?original[static_cast<size_t>(i-delay)]:0.0f;
        assert(std::abs(output[static_cast<size_t>(i)]-expected)<1.0e-6f);
    }
}
static void testDitherDeterminismAndCeiling()
{
    for(int mode=1;mode<=2;++mode)
    {
        Core whole,split;
        whole.prepare(kRate);
        split.prepare(kRate);
        Settings settings; settings.ditherMode=mode; settings.ditherBits=16;
        settings.ceilingDb=-2.0; settings.gainDb=10.0;
        whole.setSettings(settings);
        split.setSettings(settings);
        auto input=signal(23000,0.7), single=input, blocks=input;
        whole.process(single.data(),nullptr,static_cast<int>(single.size()));
        for(int i=0;i<static_cast<int>(blocks.size());i+=131)
            split.process(blocks.data()+i,nullptr,
                          std::min(131,static_cast<int>(blocks.size())-i));
        assert(single==blocks);
        const double ceiling=std::pow(10.0,settings.ceilingDb/20.0);
        assert(maxAbs(single)<=ceiling+1.0e-6);
    }
}
static void testParameterSanitisation()
{
    Settings p;
    p.gainDb=std::numeric_limits<double>::infinity();
    p.ceilingDb=200;
    p.attackMs=-400;
    p.channelLink=-10;
    p.releaseLink=42;
    p.style=500;
    p.detectorQuality=100;
    p=Settings::sanitized(p);
    assert(p.gainDb==0 && p.ceilingDb==0);
    assert(p.attackMs>=0.01 && p.channelLink==0 && p.releaseLink==1);
    assert(p.style==7 && p.detectorQuality==3);
}
static void testReset()
{
    Core a; a.prepare(kRate);
    Settings p; p.gainDb=12.0; a.setSettings(p);
    auto source=signal(12000,0.85),first=source;
    a.process(first.data(),nullptr,static_cast<int>(first.size()));
    a.reset();
    auto second=source;
    a.process(second.data(),nullptr,static_cast<int>(second.size()));
    assert(first==second);
}
int main()
{
    testParameterSanitisation();
    testSilenceAndCorruptInputs();
    testNoUnwantedGainBelowCeiling();
    testBypassIsTrulyDryAndLatencyAligned();
    testDitherDeterminismAndCeiling();
    testBlockSizeIndependence();
    testReset();
    for (const int block : {1,17,64,128,257,1024})
        for(int style=0;style<8;++style)
            for(int quality=0;quality<4;++quality)
                runCeiling(block,style,quality);
    std::cout << "APEX Bubblegum Limiter C++17 DSP gates passed\n";
}
