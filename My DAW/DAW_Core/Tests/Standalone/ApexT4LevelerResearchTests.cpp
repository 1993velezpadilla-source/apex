// APEX T4 research leveler functional regression: no licensed UA reference WAVs.
#include "../../Source/CompressorCore/ApexT4LevelerResearchCore.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace APEX::Compressor;
static void near(double a,double b,double tolerance=2e-6) {
    if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>tolerance) {
        std::cerr << "Mismatch " << a << " " << b << "\n";
        std::abort();
    }
}
int main()
{
    constexpr int total=48000;
    // Pass unity dry exactly with zero peak reduction + no processing.
    {
        ApexT4LevelerResearchCore d;d.prepare(48000);
        std::vector<float> a(total);
        for(int i=0;i<total;++i) a[i]=static_cast<float>(.31*std::sin(i*.15));
        const auto original=a;
        d.process(a.data(),nullptr,total);
        near(d.getGainReductionDb(),0);
        for(int i=0;i<total;++i)near(a[i],original[i]);
    }
    // Compression remains linked across asymmetric stereo signal.
    {
        ApexT4LevelerResearchCore d;d.prepare(48000);
        T4LevelerResearchParameters p;p.peakReduction=75;
        d.setParameters(p);
        std::vector<float> a(total,.8f),b(total,.2f);
        d.process(a.data(),b.data(),total);
        assert(d.getGainReductionDb()>3);
        assert(a.back()<.8f);
        near(a.back()/.8,b.back()/.2);
        assert(d.getAudioPhotoResistanceOhms()<1.e7);
        assert(d.getMeterPhotoResistanceOhms()<1.e7);
    }
    // Parallel dry: output must remain the original data at 0% wet.
    {
        ApexT4LevelerResearchCore d;d.prepare(48000);
        T4LevelerResearchParameters p;p.peakReduction=98;p.parallelMix=0;
        d.setParameters(p);
        std::vector<float> a(total,.41f);
        d.process(a.data(),nullptr,total);
        assert(d.getGainReductionDb()>3);
        near(a.back(),.41f);
    }
    // Initial bypass is bit-transparent and the internal photo state still
    // advances so that an unbypass does not restart the program detector.
    {
        ApexT4LevelerResearchCore d;d.prepare(48000);
        T4LevelerResearchParameters p;p.peakReduction=90;p.bypass=true;
        d.setParameters(p);
        std::vector<float> a(total,.61f);
        d.process(a.data(),nullptr,total);
        near(a.back(),.61f);
        assert(d.getGainReductionDb()>1);
    }
    // Digital key emphasis applies to detector only, no EQ applied to audio.
    // Low frequency content should be less aggressive with high-pass key.
    {
        const auto calc=[](double emphasis) {
            ApexT4LevelerResearchCore d;d.prepare(48000);
            T4LevelerResearchParameters p;p.peakReduction=43;
            p.keyEmphasis=emphasis;d.setParameters(p);
            std::vector<float> a(total);
            for(int n=0;n<total;++n)
                a[n]=static_cast<float>(.24*std::sin(2.0*3.141592653589793*72*n/48000.0));
            d.process(a.data(),nullptr,total);
            return d.getGainReductionDb();
        };
        const auto flat=calc(0.0),hpf=calc(1.0);
        assert(flat>hpf+0.1);
    }
    // Partition invariance, deterministic regardless of host buffer sizes.
    {
        T4LevelerResearchParameters p;p.peakReduction=59;
        p.parallelMix=.62;p.gainDb=5;p.keyEmphasis=.19;
        ApexT4LevelerResearchCore whole,parts;
        whole.prepare(48000);parts.prepare(48000);
        whole.setParameters(p);parts.setParameters(p);
        std::vector<float> a(30000),b(30000),c(30000),d(30000);
        for(int i=0;i<30000;++i) {
            a[i]=c[i]=static_cast<float>(.36*std::sin(i*.047));
            b[i]=d[i]=static_cast<float>(.16*std::cos(i*.081));
        }
        whole.process(a.data(),b.data(),30000);
        for(int pos=0;pos<30000;pos+=31)
            parts.process(c.data()+pos,d.data()+pos,std::min(31,30000-pos));
        for(int i=0;i<30000;++i) {
            near(a[i],c[i]);near(b[i],d[i]);
        }
        near(whole.getGainReductionDb(),parts.getGainReductionDb());
    }
    // Path sanitizes invalid audio and invalid automated controls.
    {
        ApexT4LevelerResearchCore d;d.prepare(NAN);
        T4LevelerResearchParameters p;p.peakReduction=INFINITY;
        p.parallelMix=-INFINITY;p.gainDb=NAN;p.keyEmphasis=INFINITY;
        d.setParameters(p);
        float samples[6]={NAN,INFINITY,-INFINITY,0.0f,.2f,-.5f};
        d.process(samples,nullptr,6);
        for(const auto v:samples)assert(std::isfinite(v));
    }
    std::cout << "T4 stereo opto research gates PASS (NOT hardware-verified)\n";
}
