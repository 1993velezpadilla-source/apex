#include "../../Source/CompressorCore/ApexPrecisionCompressorCore.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using APEX::PrecisionC3::Core;
using APEX::PrecisionC3::Parameters;

static void processConstant (Core& engine, std::vector<float>& audio, int chunk)
{
    for (int pos = 0; pos < static_cast<int>(audio.size()); pos += chunk)
        engine.process(audio.data()+pos,nullptr,
                       std::min(chunk,static_cast<int>(audio.size())-pos));
}

int main()
{
    {
        Core processor;
        Parameters p;
        p.thresholdDb=-18;
        p.ratio=4;
        processor.prepare(48000);
        processor.setParameters(p);
        std::vector<float> quiet(4096,0.01f);
        processConstant(processor,quiet,128);
        for (auto s:quiet) assert(s==0.01f); // default neutral below threshold
        assert(processor.getGainReductionDb()<0.001);
    }
    {
        Core processor;
        Parameters p;
        p.thresholdDb=-35;
        p.ratio=8;
        p.kneeDb=0;
        p.attackMs=1;
        p.releaseMs=100;
        processor.prepare(48000);
        processor.setParameters(p);
        std::vector<float> signal(48000,0.7f);
        processConstant(processor,signal,128);
        assert(std::isfinite(signal.back()));
        assert(signal.back()<0.1f);
        assert(processor.getGainReductionDb()>10);
    }
    {
        Core processor;
        Parameters p;
        p.thresholdDb=-50;
        p.ratio=1;
        p.kneeDb=0;
        processor.prepare(48000);
        processor.setParameters(p);
        std::vector<float> signal(2048,0.5f);
        processConstant(processor,signal,64);
        assert(std::abs(signal.back()-0.5f)<1.e-6);
        assert(processor.getGainReductionDb()<1.e-4);
    }
    {
        Core processor;
        Parameters p;
        p.style=4; // upward
        p.thresholdDb=-18;
        p.ratio=4;
        p.kneeDb=0;
        p.attackMs=2;
        processor.prepare(48000);
        processor.setParameters(p);
        std::vector<float> signal(48000,0.005f);
        processConstant(processor,signal,256);
        assert(std::isfinite(signal.back()));
        assert(signal.back()>0.008f); // actually boosts quiet audio
    }
    {
        Core processor;
        Parameters p;
        p.bypass=true;
        p.inputDb=18;
        p.outputDb=18;
        p.drive=100;
        p.character=1;
        processor.prepare(48000);
        processor.setParameters(p);
        std::vector<float> signal(1024,0.4f);
        processConstant(processor,signal,256);
        for(auto s:signal) assert(s==0.4f); // bypass is unity after restore
    }
    {
        Parameters p;
        p.thresholdDb=-28;
        p.ratio=5;
        p.attackMs=3;
        p.autoRelease=true;
        p.autoThreshold=true;
        p.style=8;
        p.sidechainHighpassHz=120;
        p.sidechainLowpassHz=9000;
        Core first,second;
        first.prepare(48000);second.prepare(48000);
        first.setParameters(p);second.setParameters(p);
        std::vector<float> a(32000),b(32000);
        for (int i=0;i<32000;++i)
        {
            a[i]=static_cast<float>(0.2*std::sin(i*0.067)+0.1*std::sin(i*0.011));
            b[i]=a[i];
        }
        processConstant(first,a,1);
        processConstant(second,b,1024);
        for (std::size_t i=0; i<a.size(); ++i)
            assert(std::abs(a[i]-b[i])<1.e-6f); // partition invariant DSP
    }
    {
        Core processor;
        Parameters p;
        p.thresholdDb=std::numeric_limits<double>::quiet_NaN();
        p.ratio=std::numeric_limits<double>::infinity();
        p.attackMs=-99999;
        p.drive=100;
        p.character=3;
        processor.prepare(192000);
        processor.setParameters(p);
        std::vector<float> bad(16384,0.5f);
        bad[4]=std::numeric_limits<float>::quiet_NaN();
        bad[5]=std::numeric_limits<float>::infinity();
        processConstant(processor,bad,37);
        for(auto value:bad) assert(std::isfinite(value));
    }
    {
        Core processor;
        Parameters p;
        p.thresholdDb=-30;
        p.ratio=8;
        p.attackMs=0.1;
        p.kneeDb=0;
        p.stereoLinkPercent=100;
        processor.prepare(48000);
        processor.setParameters(p);
        std::vector<float> a(24000,0.8f),b(24000,0.1f);
        for (int pos=0;pos<24000;pos+=256)
            processor.process(a.data()+pos,b.data()+pos,std::min(256,24000-pos));
        assert(a.back()<0.3f);
        assert(b.back()<0.08f); // linked GR applied to both channels
    }

    // Adversarial contract: external detector must actually key gain reduction.
    // Merely drawing a sidechain UI without audio routing would FAIL.
    {
        Parameters p;
        p.thresholdDb=-30;
        p.ratio=8;
        p.kneeDb=0;
        p.attackMs=0.25;
        p.releaseMs=75;
        Core normal,external;
        normal.prepare(48000); external.prepare(48000);
        normal.setParameters(p); external.setParameters(p);
        std::vector<float> direct(24000,0.6f), keyed(24000,0.6f);
        const std::vector<float> silentKey(24000,0.0001f);
        for(int pos=0;pos<24000;pos+=128)
        {
            const int n=std::min(128,24000-pos);
            normal.process(direct.data()+pos,nullptr,n);
            external.process(keyed.data()+pos,nullptr,n,
                             silentKey.data()+pos);
        }
        assert(direct.back()<0.20f); // audible compression when self-keyed
        assert(std::abs(keyed.back()-0.6f)<1.e-5f); // silence must NOT key
        assert(external.getGainReductionDb()<1.e-4);
    }
    // Side-chain high-pass must protect low-frequency content from pumping.
    {
        Parameters p;
        p.thresholdDb=-36; p.ratio=6; p.attackMs=3; p.kneeDb=0;
        Core bare,filtered;
        bare.prepare(48000);filtered.prepare(48000);
        bare.setParameters(p);
        p.sidechainHighpassHz=450.0;
        filtered.setParameters(p);
        std::vector<float> a(48000),b(48000);
        for(int i=0;i<48000;++i)
        {
            const auto v=static_cast<float>(
                0.68*std::sin(2.0*3.141592653589793*45.0*i/48000.0));
            a[static_cast<std::size_t>(i)]=v;
            b[static_cast<std::size_t>(i)]=v;
        }
        processConstant(bare,a,128);
        processConstant(filtered,b,128);
        assert(filtered.getGainReductionDb()<bare.getGainReductionDb());
    }
    // User-accessible styles must NOT produce NaNs at extreme levels/rates.
    for(int style=0;style<14;++style)
    {
        Core effect;
        Parameters p;
        p.style=style;
        p.thresholdDb=-43;
        p.ratio=40;
        p.attackMs=0.005;
        p.releaseMs=5;
        p.kneeDb=72;
        effect.prepare(192000);
        effect.setParameters(p);
        std::vector<float> a(4096),b(4096);
        for(int i=0;i<4096;++i)
        {
            a[static_cast<std::size_t>(i)]=
                static_cast<float>(0.85*std::sin(i*0.049));
            b[static_cast<std::size_t>(i)]=
                static_cast<float>(0.73*std::cos(i*0.021));
        }
        for(int n=0;n<4096;n+=64)
            effect.process(a.data()+n,b.data()+n,64);
        for(int i=0;i<4096;++i)
            assert(std::isfinite(a[static_cast<std::size_t>(i)])
                && std::isfinite(b[static_cast<std::size_t>(i)]));
        assert(std::isfinite(effect.getGainReductionDb()));
    }

    // Adversarial: Character PRE and POST must be two different *audio* paths,
    // not a cosmetic state or text-only button.
    {
        Parameters p;
        p.thresholdDb=-27; p.ratio=8; p.kneeDb=6;
        p.attackMs=1; p.releaseMs=90;
        p.drive=95; p.character=1;
        Core before,after;
        before.prepare(48000);after.prepare(48000);
        p.characterPre=true;before.setParameters(p);
        p.characterPre=false;after.setParameters(p);
        std::vector<float> a(24000),b(24000);
        for(int i=0;i<24000;++i)
            a[static_cast<std::size_t>(i)]=b[static_cast<std::size_t>(i)]
                =static_cast<float>(0.73*std::sin(i*0.019));
        processConstant(before,a,256);
        processConstant(after,b,256);
        double difference=0.0;
        for(std::size_t i=12000;i<a.size();++i)
            difference+=std::abs(a[i]-b[i]);
        assert(difference>1.0); // distinct saturation order changes signal
    }

    // Adversarial side-chain EQ: each of the four bell bands must change
    // actual gain reduction when detector energy falls in its pass band.
    // An EQ graph with merely decorative nodes MUST fail this contract.
    for(int band=0;band<4;++band)
    {
        const double frequencies[]={130.0,680.0,2700.0,9300.0};
        Parameters flat,boost,cut;
        flat.thresholdDb=-30.0;flat.ratio=8.0;flat.kneeDb=0.0;
        flat.attackMs=1.0;flat.releaseMs=80.0;
        boost=flat;cut=flat;
        boost.sidechainBells[static_cast<std::size_t>(band)].gainDb=15.0;
        cut.sidechainBells[static_cast<std::size_t>(band)].gainDb=-15.0;
        boost.sidechainBells[static_cast<std::size_t>(band)].q=3.0;
        cut.sidechainBells[static_cast<std::size_t>(band)].q=3.0;
        Core eFlat,eBoost,eCut;
        eFlat.prepare(48000);eBoost.prepare(48000);eCut.prepare(48000);
        eFlat.setParameters(flat);eBoost.setParameters(boost);eCut.setParameters(cut);
        constexpr int chunk=128,total=32768;
        std::vector<float> dry(total,0.5f),hot(total,0.5f),cold(total,0.5f);
        std::vector<float> detector(total);
        for(int i=0;i<total;++i)
            detector[static_cast<std::size_t>(i)]=static_cast<float>(0.07
                *std::sin(6.283185307179586*frequencies[band]*i/48000.0));
        for(int i=0;i<total;i+=chunk)
        {
            eFlat.process(dry.data()+i,nullptr,chunk,detector.data()+i);
            eBoost.process(hot.data()+i,nullptr,chunk,detector.data()+i);
            eCut.process(cold.data()+i,nullptr,chunk,detector.data()+i);
        }
        // Strong positive bell sensitizes detector, negative bell protects it.
        assert(eBoost.getGainReductionDb()>eFlat.getGainReductionDb()+1.0);
        assert(eCut.getGainReductionDb()<eFlat.getGainReductionDb()-1.0);
        assert(std::isfinite(hot.back()) && std::isfinite(cold.back()));
    }
    // Six-filter bypass state remains neutral when the four bells are zero.
    {
        Parameters normal,zeroed;
        normal.sidechainHighpassHz=85.0;
        normal.sidechainLowpassHz=6800.0;
        zeroed=normal;
        for(auto& bell:zeroed.sidechainBells)
        {
            bell.gainDb=0.0;bell.q=18.0; // Q must not affect zero gain
        }
        Core a,b;
        a.prepare(48000);b.prepare(48000);
        a.setParameters(normal);b.setParameters(zeroed);
        std::vector<float> left(4096),right(4096);
        for(int i=0;i<4096;++i)
            left[static_cast<std::size_t>(i)]
                =right[static_cast<std::size_t>(i)]
                =static_cast<float>(0.21*std::sin(i*.077));
        processConstant(a,left,64);processConstant(b,right,512);
        for(std::size_t i=0;i<left.size();++i)
            assert(std::abs(left[i]-right[i])<1.e-6f);
    }
    // Bell parameters must remain finite when automated to hostile values.
    {
        Core e;Parameters p;
        p.sidechainBells[0].hz=std::numeric_limits<double>::quiet_NaN();
        p.sidechainBells[1].gainDb=std::numeric_limits<double>::infinity();
        p.sidechainBells[2].q=-500;
        p.sidechainBells[3]={1.e20,-1.e20,1.e20};
        e.prepare(8000);e.setParameters(p);
        std::vector<float> test(4096,0.35f);
        processConstant(e,test,1);
        for(float v:test) assert(std::isfinite(v));
    }
    // Adversarial parity: Pro-C-style Dry Gain is decibel-based, with
    // a genuinely muted default. An old beta session's c3.dry percentage
    // must still recall while the new knob remains at -inf.
    {
        auto processDry=[&](double dryDb,double legacyPercent)
        {
            Core e;Parameters p;
            p.ratio=1.0; p.kneeDb=0.0;
            p.dryPercent=legacyPercent;
            p.dryGainDb=dryDb;
            e.prepare(48000);e.setParameters(p);
            std::vector<float> signal(16384,0.25f);
            processConstant(e,signal,128);
            return static_cast<double>(signal.back());
        };
        assert(std::abs(processDry(-100.0,0.0)-0.25)<1.e-6);
        assert(std::abs(processDry(-6.0205999133,0.0)-0.375)<1.e-4);
        assert(std::abs(processDry(-100.0,50.0)-0.375)<1.e-4);
        // Once the decibel control is engaged it takes precedence over
        // the preview-only percentage rather than double summing dry paths.
        assert(std::abs(processDry(-12.0411998266,50.0)-0.3125)<1.e-4);
    }

    // New real lookahead: at zero milliseconds remain sample-transparent
    // and introduce no hidden DAW monitoring latency.
    {
        Core normal; Parameters p; p.ratio=1; p.lookaheadMs=0;
        normal.prepare(48000);normal.setParameters(p);
        assert(normal.latencySamples()==0);
        std::vector<float> input(4096);
        for(int i=0;i<4096;++i)
            input[static_cast<std::size_t>(i)]=static_cast<float>(.24*std::sin(i*.11));
        const auto original=input;
        processConstant(normal,input,128);
        for(std::size_t i=0;i<input.size();++i)assert(input[i]==original[i]);
    }
    // At 5ms/48k, EXACTLY 240 samples of delayed MAIN output,
    // including dry parallel and plugin bypass. Host PDC must report 240.
    for(const bool bypass:{false,true})
    {
        Core delayed;Parameters p;p.lookaheadMs=5;p.ratio=1;
        p.kneeDb=0;p.bypass=bypass;
        delayed.prepare(48000);delayed.setParameters(p);
        assert(delayed.latencySamples()==240);
        std::vector<float> impulse(1024,0.f);
        impulse[0]=0.8f;
        processConstant(delayed,impulse,128);
        for(int i=0;i<1024;++i)
            assert(std::abs(impulse[static_cast<std::size_t>(i)]
                             -(i==240?0.8f:0.0f))<1.e-6f);
    }
    // The external sidechain sees current key signal while main output
    // is delayed. Thus a key transient at input sample zero can compress
    // the target sample at output sample 240; a non-lookahead attack cannot
    // anticipate the delayed target with a positive attack constant.
    {
        Parameters p;p.thresholdDb=-24;p.ratio=15;p.kneeDb=0;
        p.attackMs=0.1;p.lookaheadMs=5;p.releaseMs=400;
        Core effect;effect.prepare(48000);effect.setParameters(p);
        const int delay=effect.latencySamples();
        std::vector<float> main(2048,0.0f),key(2048,0.f);
        main[0]=.8f;key[0]=.8f;
        for(int i=1;i<512;++i)key[static_cast<std::size_t>(i)]=.8f;
        for(int i=0;i<2048;i+=128)
            effect.process(main.data()+i,nullptr,128,key.data()+i);
        assert(std::isfinite(main[static_cast<std::size_t>(delay)]));
        assert(main[static_cast<std::size_t>(delay)]<0.8f);
    }
    // Lookahead must be independent of block partitioning and preserve
    // stereo channels, with no aliasing of the host's key bus.
    {
        Parameters p;p.lookaheadMs=10;p.thresholdDb=-27;p.ratio=6;
        p.attackMs=.05;p.releaseMs=125;p.kneeDb=5;
        Core a,b;a.prepare(96000);b.prepare(96000);
        a.setParameters(p);b.setParameters(p);
        assert(a.latencySamples()==960);
        std::vector<float> la(4096),lb(4096),ra(4096),rb(4096);
        for(int i=0;i<4096;++i)
        {
            la[static_cast<std::size_t>(i)]=lb[static_cast<std::size_t>(i)]
                =static_cast<float>(.7*std::sin(i*.087));
            ra[static_cast<std::size_t>(i)]=rb[static_cast<std::size_t>(i)]
                =static_cast<float>(.31*std::cos(i*.064));
        }
        for(int i=0;i<4096;++i)
            a.process(la.data()+i,ra.data()+i,1);
        for(int i=0;i<4096;i+=128)
            b.process(lb.data()+i,rb.data()+i,128);
        for(int i=0;i<4096;++i)
        {
            assert(std::abs(la[static_cast<std::size_t>(i)]
                             -lb[static_cast<std::size_t>(i)])<1.e-6f);
            assert(std::abs(ra[static_cast<std::size_t>(i)]
                             -rb[static_cast<std::size_t>(i)])<1.e-6f);
        }
        assert(std::abs(la.back()-ra.back())>1.e-4f);
    }
    // Defend the preallocated ring against exotic rates and nonfinite
    // automation: clipping the requested delay to allocated memory
    // preserves an EXACT report, never a mismatched PDC value.
    {
        Parameters p;p.lookaheadMs=std::numeric_limits<double>::infinity();
        Core e;e.prepare(192000);e.setParameters(p);
        assert(e.latencySamples()==0);
        p.lookaheadMs=10;e.setParameters(p);
        assert(e.latencySamples()==1920);
        p.lookaheadMs=20;e.setParameters(p);
        assert(e.latencySamples()==3840);
        p.lookaheadMs=5000;e.setParameters(p);
        assert(e.latencySamples()==3840);
    }

    // V10: the same REAL 2x FIR filter must now run inside our compressor.
    // OFF remains transparent at zero lookahead, and ON adds exactly 48
    // samples even when character is OFF, wet and bypass included.
    for(const bool bypass:{false,true})
    {
        Parameters p;p.oversampling2x=true;p.ratio=1;p.kneeDb=0;p.bypass=bypass;
        Core e;e.prepare(48000);e.setParameters(p);
        assert(e.latencySamples()==48);
        std::vector<float> impulse(1024,0.f);
        impulse[0]=.8f;
        processConstant(e,impulse,128);
        for(int i=0;i<1024;++i)
            assert(std::abs(impulse[static_cast<std::size_t>(i)]
                -(i==48?.8f:0.0f))<1.e-6f);
    }
    // Lookahead and the FIR's group delay must compose, never accidentally
    // subtract or double-count PDC in a bypassed/parallel chain.
    {
        Parameters p;p.lookaheadMs=5;p.oversampling2x=true;
        p.ratio=1;p.bypass=true;
        Core e;e.prepare(48000);e.setParameters(p);
        assert(e.latencySamples()==288);
        std::vector<float> impulse(2048,0.f);
        impulse[0]=0.65f;
        processConstant(e,impulse,128);
        for(int i=0;i<2048;++i)
            assert(std::abs(impulse[static_cast<std::size_t>(i)]
                -(i==288?0.65f:0.0f))<1.e-6f);
    }
    // Character 2x mode should REDUCE alias frequencies at actual compressor
    // output, not merely run a disconnected oversampling utility test.
    {
        constexpr double pi=3.14159265358979323846;
        constexpr int roll=4800,frames=4800,total=roll+frames;
        Parameters p;p.oversampling2x=false;p.ratio=1;p.kneeDb=0;
        p.character=1;p.drive=100;
        Core raw,filtered;
        raw.prepare(48000);filtered.prepare(48000);
        raw.setParameters(p);
        p.oversampling2x=true;filtered.setParameters(p);
        assert(filtered.latencySamples()==48);
        std::vector<float> a(total),b(total);
        for(int i=0;i<total;++i)
            a[static_cast<std::size_t>(i)]=
                b[static_cast<std::size_t>(i)]=
                static_cast<float>(.85*std::sin(2*pi*10000*i/48000.0));
        processConstant(raw,a,128);processConstant(filtered,b,128);
        const auto harmonic=[&](const std::vector<float>& data,int hz)
        {
            double real=0.0,imag=0.0;
            for(int i=0;i<frames;++i)
            {
                const double phase=2*pi*hz*i/48000.0;
                const double v=data[static_cast<std::size_t>(roll+i)];
                real+=v*std::cos(phase);
                imag+=v*std::sin(phase);
            }
            return 2.0*std::hypot(real,imag)/frames;
        };
        const double rawAlias=harmonic(a,18000);
        const double cleanAlias=harmonic(b,18000);
        const double suppressed=20*std::log10(rawAlias/std::max(1e-15,cleanAlias));
        std::cout<<"Precision C actual 2x alias suppression: "
                 <<suppressed<<" dB\n";
        assert(suppressed>25);
        const double rawFund=harmonic(a,10000),newFund=harmonic(b,10000);
        assert(std::abs(20*std::log10(newFund/rawFund))<0.65);
    }
    // 2x character in both PRE/POST routing modes must be finite,
    // stereo-isolated and independent of the host's block partition.
    for(const bool pre:{false,true})
    {
        Parameters p;p.oversampling2x=true;p.characterPre=pre;
        p.character=2;p.drive=75;p.ratio=6;p.thresholdDb=-30;
        p.attackMs=.25;p.releaseMs=100;
        Core a,b;a.prepare(48000);b.prepare(48000);
        a.setParameters(p);b.setParameters(p);
        std::vector<float> al(8192),ar(8192,0.f),bl(8192),br(8192,0.f);
        for(int i=0;i<8192;++i)
            al[static_cast<std::size_t>(i)]=bl[static_cast<std::size_t>(i)]
                =static_cast<float>(.65*std::sin(i*.075));
        for(int i=0;i<8192;++i)
            a.process(al.data()+i,ar.data()+i,1);
        for(int i=0;i<8192;i+=128)
            b.process(bl.data()+i,br.data()+i,128);
        for(int i=0;i<8192;++i)
        {
            assert(std::isfinite(al[static_cast<std::size_t>(i)]));
            assert(std::abs(al[static_cast<std::size_t>(i)]
                    -bl[static_cast<std::size_t>(i)])<1.e-6f);
            assert(ar[static_cast<std::size_t>(i)]==0.f);
            assert(br[static_cast<std::size_t>(i)]==0.f);
        }
    }
    std::cout << "APEX Precision C3 portable DSP gates PASS\n";
    return 0;
}
