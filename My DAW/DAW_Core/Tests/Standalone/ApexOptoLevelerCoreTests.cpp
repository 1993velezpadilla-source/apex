// Standalone portable test suite, no JUCE/runtime dependency.
// g++ -std=c++17 -O2 -Wall -Wextra -pedantic ApexOptoLevelerCoreTests.cpp -o opto_test
#include "../../Source/CompressorCore/ApexOptoLevelerCore.h"
#include "../../Source/CompressorCore/ApexOptoVuBallisticsCore.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace APEX::Compressor;
static constexpr double fs = 48000.0;
static void near(double a, double b, double eps=1e-5) { if (std::abs(a-b)>eps) { std::cerr << "FAILED near " << a << " " << b << " err="<<std::abs(a-b)<<" eps=" <<eps<<"\n";std::abort(); } }

static void testDryAndSilence()
{
    ApexOptoLevelerCore p; p.prepare(fs);
    OptoParameters prm; prm.peakReduction=100; prm.mix=0; prm.gainDb=40;
    p.setParameters(prm); p.prepare(fs); // initialise initial parameters
    std::vector<float> l(2048), r(2048);
    for(int n=0;n<2048;++n) { l[n] = static_cast<float>(0.5*std::sin(0.02*n)); r[n]=0.35f; }
    const auto originalL=l, originalR=r;
    p.process(l.data(),r.data(),2048);
    for(int n=0;n<2048;++n) { near(l[n],originalL[n]); near(r[n],originalR[n]); }
    p.prepare(fs); prm.mix=1; prm.colorEnabled=false; prm.gainDb=0; prm.peakReduction=80; p.setParameters(prm);
    std::vector<float> zeros(96000,0.f); p.process(zeros.data(),nullptr,(int)zeros.size());
    for(const auto x:zeros) near(x,0);
    near(p.getGainReductionDb(),0);
}
static void testCompressionAndStereoLink()
{
    ApexOptoLevelerCore p; p.prepare(fs);
    OptoParameters prm; prm.peakReduction=70; prm.colorEnabled=false; p.setParameters(prm);
    std::vector<float> l(48000,0.8f), r(48000,0.2f);
    p.process(l.data(),r.data(),(int)l.size());
    assert(p.getGainReductionDb()>3.0);
    near(l.back()/0.8f, r.back()/0.2f, 1e-5);
    assert(l.back()<0.8f && r.back()<0.2f);
    prm.peakReduction=0; p.setParameters(prm); p.prepare(fs);
    std::fill(l.begin(),l.end(),0.25f); std::fill(r.begin(),r.end(),0.25f);
    p.process(l.data(),r.data(),(int)l.size());
    near(l.back(),0.25,1e-6); near(r.back(),0.25,1e-6);
}
static void testTwoStageRelease()
{
    ApexOptoLevelerCore p; p.prepare(fs);
    OptoParameters prm; prm.peakReduction=70; prm.colorEnabled=false; p.setParameters(prm);
    std::vector<float> loud(48000,0.8f); p.process(loud.data(),nullptr,(int)loud.size());
    const double start=p.getGainReductionDb(); assert(start>3);
    std::vector<float> silence(2880,0.f); p.process(silence.data(),nullptr,(int)silence.size());
    const double at60ms=p.getGainReductionDb();
    assert(at60ms>0.35*start && at60ms<0.60*start);
    silence.resize(48000,0.f); p.process(silence.data(),nullptr,(int)silence.size());
    assert(p.getGainReductionDb()>0 && p.getGainReductionDb()<at60ms);
}
static void testBlockInvariance()
{
    OptoParameters prm; prm.peakReduction=45; prm.mix=0.7; prm.gainDb=7; prm.emphasis=0.4;
    ApexOptoLevelerCore single, sliced; single.prepare(fs);sliced.prepare(fs);
    single.setParameters(prm); sliced.setParameters(prm);
    std::vector<float> a(8192), b(8192);
    for(int n=0;n<(int)a.size();++n) a[n]=b[n]=static_cast<float>(0.6*std::sin(0.09*n));
    single.process(a.data(),nullptr,(int)a.size());
    int pos=0; while(pos<(int)b.size()) {const int size=std::min(31,(int)b.size()-pos); sliced.process(b.data()+pos,nullptr,size);pos+=size;}
    for(int n=0;n<(int)a.size();++n) near(a[n],b[n],1e-6);
    near(single.getGainReductionDb(),sliced.getGainReductionDb(),1e-9);
}
static void testInputSanitisationAndParameterSafety()
{
    ApexOptoLevelerCore p; p.prepare(fs);
    OptoParameters prm; prm.peakReduction=INFINITY;prm.mix=NAN;prm.gainDb=INFINITY;
    prm.colorDrive=-INFINITY; p.setParameters(prm);
    std::vector<float> a { NAN, INFINITY,-INFINITY,0.2f, -0.5f };
    p.process(a.data(),nullptr,(int)a.size());
    for(float x:a) assert(std::isfinite(x));
    for(double sampleRate: { 44100.0,48000.0,96000.0,192000.0 }) {
        p.prepare(sampleRate);
        prm.peakReduction=85;prm.mix=1;prm.gainDb=0;prm.colorDrive=1;
        p.setParameters(prm);
        std::vector<float> b(512, 0.4f);
        for(int i=0;i<500;++i)p.process(b.data(),nullptr,(int)b.size());
        assert(std::isfinite(p.getGainReductionDb()));
    }
}
static void testBypassAndParameterChanges()
{
    ApexOptoLevelerCore p; p.prepare(fs);
    OptoParameters prm; prm.peakReduction=90;prm.gainDb=40;prm.bypass=true;
    p.setParameters(prm);
    float a[4]={0.25f,-0.35f,0.8f,0.f};
    p.process(a,nullptr,4);
    near(a[0],0.25);near(a[1],-0.35);near(a[2],0.8);near(a[3],0);
    prm.bypass=false;prm.mix=1;prm.gainDb=0;prm.colorEnabled=false;
    p.setParameters(prm);
    std::vector<float> sound(48000,0.8f);p.process(sound.data(),nullptr,(int)sound.size());
    assert(p.getGainReductionDb()>0);
}
static void testVuBallistics()
{
    OptoVuBallisticsCore one, split;
    one.prepare(fs); split.prepare(fs);
    std::vector<float> audio(48000,0.8f);
    one.process(audio.data(),nullptr,(int)audio.size());
    for(int i=0;i<(int)audio.size();i+=13)
        split.process(audio.data()+i,nullptr,std::min(13,(int)audio.size()-i));
    near(one.getIndicatedAmplitude(),split.getIndicatedAmplitude(),1.e-10);
    near(one.getIndicatedAmplitude(),0.8,0.001);
    split.reset(); near(split.getIndicatedAmplitude(),0.0);
    split.process(audio.data(),nullptr,14400); // 300ms hardware-style step
    const double fraction=split.getIndicatedAmplitude()/0.8;
    assert(fraction>0.98 && fraction<0.995);
    for(auto sr:{44100.0,48000.0,96000.0,192000.0})
    {
        OptoVuBallisticsCore vu;vu.prepare(sr);
        std::vector<float> unit(static_cast<size_t>(sr*0.3),0.8f);
        vu.process(unit.data(),nullptr,(int)unit.size());
        near(vu.getIndicatedAmplitude()/0.8,fraction,0.002);
        vu.reset();
        unit.assign(64,NAN);
        vu.process(unit.data(),nullptr,(int)unit.size());
        assert(std::isfinite(vu.getIndicatedRmsDbfs()));
    }
}
int main()
{
    std::cerr << "dry\n"; testDryAndSilence(); std::cerr << "compress\n"; testCompressionAndStereoLink(); std::cerr << "release\n";testTwoStageRelease();
    std::cerr << "invariance\n"; testBlockInvariance();std::cerr << "sanity\n";testInputSanitisationAndParameterSafety();
    std::cerr << "bypass\n"; testBypassAndParameterChanges();
    std::cerr << "VU ballistics\\n"; testVuBallistics();
    std::cout<<"APEX Opto Core: 7 suites PASS (algorithm scaffold only; no hardware match claim)\n";
}
