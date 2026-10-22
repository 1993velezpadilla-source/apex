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
    std::cout << "APEX Precision C3 portable DSP gates PASS\n";
    return 0;
}
