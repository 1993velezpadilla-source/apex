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
static void testOriginalOpticalVoicings()
{
    // FAST/CLASSIC/SLOW share one processor and differ in envelope time
    // constants. They are original tunings, NOT measured LA-2A revisions.
    for(const double rate:{44100.0,48000.0,96000.0})
    {
        auto attackAt20ms=[rate](int voice) {
            ApexOptoLevelerCore core;core.prepare(rate);
            OptoParameters p;p.peakReduction=80;p.colorEnabled=false;p.voicing=voice;
            core.setParameters(p);
            std::vector<float> loud(static_cast<size_t>(rate*0.020),.8f);
            core.process(loud.data(),nullptr,static_cast<int>(loud.size()));
            return core.getGainReductionDb();
        };
        const double aFast=attackAt20ms(0);
        const double aClassic=attackAt20ms(1);
        const double aSlow=attackAt20ms(2);
        assert(aFast>aClassic && aClassic>aSlow && aSlow>0.0);
        auto releaseAt60ms=[rate](int voice) {
            ApexOptoLevelerCore core;core.prepare(rate);
            OptoParameters p;p.peakReduction=80;p.colorEnabled=false;p.voicing=voice;
            core.setParameters(p);
            std::vector<float> loud(static_cast<size_t>(rate),.8f);
            core.process(loud.data(),nullptr,static_cast<int>(loud.size()));
            std::vector<float> silence(static_cast<size_t>(rate*.060),0.f);
            core.process(silence.data(),nullptr,static_cast<int>(silence.size()));
            return core.getGainReductionDb();
        };
        const double rFast=releaseAt60ms(0);
        const double rClassic=releaseAt60ms(1);
        const double rSlow=releaseAt60ms(2);
        assert(rFast<rClassic && rClassic<rSlow);
        assert(std::isfinite(rFast) && std::isfinite(rSlow));
    }
    OptoParameters p;p.voicing=999;
    assert(OptoParameters::sanitise(p).voicing==2);
    p.voicing=-999;
    assert(OptoParameters::sanitise(p).voicing==0);
    // Callback partitioning may NOT influence any envelope response.
    for(int voice:{0,1,2})
    {
        ApexOptoLevelerCore single,sliced;
        single.prepare(48000.0);sliced.prepare(48000.0);
        OptoParameters prm;prm.peakReduction=64;prm.colorEnabled=false;prm.voicing=voice;
        single.setParameters(prm);sliced.setParameters(prm);
        std::vector<float> a(6000,.6f),b=a;
        single.process(a.data(),nullptr,static_cast<int>(a.size()));
        for(int n=0;n<static_cast<int>(b.size());n+=31)
            sliced.process(b.data()+n,nullptr,
                std::min(31,static_cast<int>(b.size())-n));
        for(size_t n=0;n<a.size();++n)near(a[n],b[n],1.e-6);
    }
}
static void testClickBoundedColorAutomation()
{
    // Optical dynamics and Color use the same split-invariant smoothing
    // clock. Enabling a strong nonlinear stage may NEVER step abruptly from
    // the original waveform in the very next sample.
    for (double sr : {44100.0,48000.0,96000.0})
    {
        OptoParameters prm;
        prm.peakReduction=0;
        prm.gainDb=0;
        prm.mix=1;
        prm.colorEnabled=false;
        prm.colorDrive=4;
        ApexOptoLevelerCore single;
        single.setParameters(prm);
        single.prepare(sr);
        std::vector<float> steady(512,.35f);
        single.process(steady.data(),nullptr,(int)steady.size());
        near(steady.back(),.35,1.e-6);

        prm.colorEnabled=true;
        single.setParameters(prm);
        float first=.35f;
        single.process(&first,nullptr,1);
        assert(std::abs(first-.35f)<.005f);
        std::vector<float> saturated(8192,.35f);
        single.process(saturated.data(),nullptr,(int)saturated.size());
        assert(std::abs(saturated.back()-.35f)>.02f);
        const float beforeOff=saturated.back();

        prm.colorEnabled=false;
        single.setParameters(prm);
        float firstOff=.35f;
        single.process(&firstOff,nullptr,1);
        assert(std::abs(firstOff-beforeOff)<.005f);
        std::vector<float> restored(8192,.35f);
        single.process(restored.data(),nullptr,(int)restored.size());
        near(restored.back(),.35,1.e-5);

        // The exact same toggle and drive automation timeline must render
        // identically whether audio arrives in 31 or 128-sample host blocks.
        auto render=[sr](int frames) {
            ApexOptoLevelerCore dsp;
            OptoParameters p;p.peakReduction=0;p.colorEnabled=false;
            p.colorDrive=4;p.mix=1;
            dsp.setParameters(p);dsp.prepare(sr);
            std::vector<float> audio(2048,.35f);
            for(int off=0;off<(int)audio.size();)
            {
                if(off==512) {p.colorEnabled=true;dsp.setParameters(p);}
                if(off==1024) {p.colorDrive=1.5;dsp.setParameters(p);}
                if(off==1536) {p.colorEnabled=false;dsp.setParameters(p);}
                const int nextBoundary=off<512?512:(off<1024?1024:(off<1536?1536:2048));
                const int n=std::min(frames,nextBoundary-off);
                dsp.process(audio.data()+off,nullptr,n);
                off+=n;
            }
            return audio;
        };
        const auto block128=render(128), block31=render(31);
        for(size_t n=0;n<block128.size();++n)
            near(block128[n],block31[n],2.e-6);
    }
}
static void testMovingAutomationAtAudioBlockBoundaries()
{
    // FAST->CLASSIC->SLOW, Peak Reduction, bypass, mix and Color changes
    // must be sample-clock invariant under different callback partitions.
    // Keep input constant to isolate zipper/click steps from source material.
    for(const double sampleRate : {44100.0,48000.0,96000.0})
    {
        const auto render=[sampleRate](int callbackFrames) {
            ApexOptoLevelerCore dsp;
            OptoParameters p;
            p.peakReduction=80;
            p.mix=1;
            p.colorEnabled=false;
            p.voicing=1;
            dsp.setParameters(p);
            dsp.prepare(sampleRate);
            std::vector<float> audio(8192,.35f);
            for(int offset=0;offset<(int)audio.size();)
            {
                switch(offset)
                {
                    case 1024:p.voicing=0;break;      // FAST
                    case 2048:p.voicing=2;break;      // SLOW
                    case 3072:p.peakReduction=0;break;
                    case 4096:p.bypass=true;break;
                    case 5120:p.bypass=false;break;
                    case 6144:p.mix=.25;break;
                    case 7168:p.colorEnabled=true;p.colorDrive=4;break;
                    default:break;
                }
                dsp.setParameters(p);
                const int next=std::min(8192,((offset/1024)+1)*1024);
                const int frames=std::min(callbackFrames,next-offset);
                dsp.process(audio.data()+offset,nullptr,frames);
                offset+=frames;
            }
            return audio;
        };
        const auto whole=render(1024);
        const auto tiny=render(31);
        const auto device=render(128);
        for(std::size_t i=0;i<whole.size();++i)
        {
            near(whole[i],tiny[i],1.e-6);
            near(whole[i],device[i],1.e-6);
            assert(std::isfinite(whole[i]));
            if(i>0)assert(std::abs(whole[i]-whole[i-1])<.05f);
        }
    }
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
    testOriginalOpticalVoicings();
    testClickBoundedColorAutomation();
    testMovingAutomationAtAudioBlockBoundaries();
    std::cout<<"APEX Opto Core: 10 suites PASS (algorithm scaffold only; no hardware match claim)\n";
}
