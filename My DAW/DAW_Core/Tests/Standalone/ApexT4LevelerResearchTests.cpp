// APEX T4 research leveler functional regression: no licensed UA reference WAVs.
#include "../../Source/CompressorCore/ApexT4LevelerResearchCore.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>
#include <utility>

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
        const auto calc=[total](double emphasis) {
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
    // Sample-rate correctness: the optical cell time constants are seconds,
    // not a fixed 48k sample count. Compare physically equal 1.0 s exposure
    // and 0.1/0.5/2.0 s recovery at 44.1k against 48k, rather than merely
    // checking finite output at the alternate rate.
    {
        struct TimeProfile {
            double exposureDb=0,release100msDb=0,
                   release500msDb=0,release2000msDb=0;
        };
        const auto measure=[](int sampleRate) {
            ApexT4LevelerResearchCore core;
            core.prepare(sampleRate);
            T4LevelerResearchParameters p;
            p.peakReduction=83.0;
            p.keyEmphasis=.15;
            p.use6AQ5Driver=true;
            core.setParameters(p);
            std::vector<float> excited(static_cast<std::size_t>(sampleRate),.72f);
            core.process(excited.data(),nullptr,sampleRate);
            TimeProfile profile;
            profile.exposureDb=core.getGainReductionDb();
            const auto runSilence=[&](double seconds) {
                const int frames=static_cast<int>(std::round(sampleRate*seconds));
                std::vector<float> silent(static_cast<std::size_t>(frames),0.0f);
                core.process(silent.data(),nullptr,frames);
                return core.getGainReductionDb();
            };
            profile.release100msDb=runSilence(.1);
            profile.release500msDb=runSilence(.4);
            profile.release2000msDb=runSilence(1.5);
            return profile;
        };
        const auto profile44=measure(44100),profile48=measure(48000);
        assert(profile44.exposureDb>1 && profile48.exposureDb>1);
        for(const auto& pair:{
                std::pair<double,double>{profile44.exposureDb,profile48.exposureDb},
                {profile44.release100msDb,profile48.release100msDb},
                {profile44.release500msDb,profile48.release500msDb},
                {profile44.release2000msDb,profile48.release2000msDb}}) {
            assert(std::isfinite(pair.first)&&std::isfinite(pair.second));
            near(pair.first,pair.second,.25); // cross-rate optical tolerance
        }
        assert(profile44.release2000msDb<profile44.exposureDb);
        assert(profile48.release2000msDb<profile48.exposureDb);
        std::cout<<"44.1/48 kHz 6AQ5+T4 optical time-profile: exposure GR "
                 <<profile44.exposureDb<<" / "<<profile48.exposureDb
                 <<" dB, recovery(2s) "<<profile44.release2000msDb
                 <<" / "<<profile48.release2000msDb<<" dB\\n";
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
