#include "../../Source/DelayCore/ApexBubblegumDelayEngine.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using APEX::Delay::Engine;
using APEX::Delay::Settings;
static bool near(float a,float b,float e=0.015f){return std::abs(a-b)<=e;}
static void render(Engine& d,std::vector<float>& l,std::vector<float>& r)
{
    constexpr int chunk=128;
    for(int i=0;i<static_cast<int>(l.size());i+=chunk)
        d.process(l.data()+i,r.empty()?nullptr:r.data()+i,
                  std::min(chunk,static_cast<int>(l.size())-i));
}
static void impulseTiming()
{
    Engine d;Settings s;s.sync=0;s.timeMs=100;s.feedback=0;s.mix=1;
    d.setSettings(s);d.prepare(48000);
    std::vector<float> l(14400),r;l[0]=1;render(d,l,r);
    assert(near(l[0],0));assert(near(l[4799],0));
    assert(near(l[4800],1));assert(near(l[9600],0));
    assert(d.getLatencySamples()==0);
}
static void echoFeedback()
{
    Engine d;Settings s;s.sync=0;s.timeMs=100;s.feedback=0.5f;s.mix=1;
    d.setSettings(s);d.prepare(48000);
    std::vector<float> l(15000),r;l[0]=1;render(d,l,r);
    assert(near(l[4800],1));assert(near(l[9600],0.5f));
    assert(near(l[14400],0.25f));
}
static void stereoPingPong()
{
    Engine d;Settings s;
    s.sync=0;s.timeMs=100;s.feedback=0.5f;s.mix=1;s.pingPong=true;
    d.setSettings(s);d.prepare(48000);
    std::vector<float> l(15000),r(15000);l[0]=1;r[0]=1;render(d,l,r);
    assert(near(l[4800],1));assert(near(r[4800],0));
    assert(near(l[9600],0));assert(near(r[9600],0.5f));
    assert(near(l[14400],0.25f));
}
static void dryAndBypass()
{
    Engine d;Settings s;s.sync=0;s.timeMs=200;s.mix=0;s.feedback=0;
    d.setSettings(s);d.prepare(44100);
    std::vector<float> l(200),r;
    for(std::size_t i=0;i<l.size();++i)l[i]=std::sin(i*0.1f)*0.5f;
    const auto before=l;render(d,l,r);
    for(std::size_t i=0;i<l.size();++i)assert(near(l[i],before[i],1e-5f));
    s.mix=1;s.bypass=true;Engine bypass;bypass.setSettings(s);bypass.prepare(44100);
    l=before;render(bypass,l,r);
    for(std::size_t i=0;i<l.size();++i)assert(near(l[i],before[i],1e-5f));
}
static void hostSync()
{
    Engine d;Settings s;s.sync=1;s.note=8;s.mix=1;s.feedback=0;
    d.setSettings(s);d.setHostTempo(120);d.prepare(48000);
    assert(std::abs(d.getTimeMs()-500)<0.001);
    d.setHostTempo(80);assert(std::abs(d.getTimeMs()-750)<0.001);
    s.sync=2;s.manualBpm=100;d.setSettings(s);
    assert(std::abs(d.getTimeMs()-600)<0.001);
}
static void runawaySafety()
{
    Engine d;Settings s;s.sync=0;s.timeMs=4;s.feedback=2;s.mix=1;s.analog=4;
    s.lofi=true;s.lowpassHz=6000;s.highpassHz=200;
    s.modulationDepth=1;s.modulationHz=10;d.setSettings(s);d.prepare(48000);
    std::vector<float> l(96000),r(96000);l[0]=1;r[0]=-1;
    l[1]=std::numeric_limits<float>::quiet_NaN();render(d,l,r);
    for(std::size_t i=0;i<l.size();++i)
    {
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
        assert(std::abs(l[i])<=64&&std::abs(r[i])<=64);
    }
}
int main()
{
    impulseTiming();echoFeedback();stereoPingPong();dryAndBypass();
    hostSync();runawaySafety();
    std::cout<<"APEX Bubblegum Delay DSP gates PASS\n";
}
