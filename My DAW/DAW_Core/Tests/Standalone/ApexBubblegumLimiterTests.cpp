// APEX limiter — portable C++17 realtime/DSP regression suite.
#include "../../Source/LimiterCore/ApexLimiterCore.h"
#include "../../Source/LimiterCore/ApexLimiterLoudness.h"
#include "ApexLimiterTruePeakReference.h"
#include "../../Source/LimiterCore/ApexLimiterOversampling.h"
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
    // 6k frames > 20ms fixed latency, while bounding the FIR 8x DSP
    // sanitizer matrix (6 blocks x 8 styles x 4 detector resolutions).
    auto l=signal(6000,0.91),r=signal(6000,0.73);
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

static void testReductionTelemetryAcrossHostBlocks()
{
    // Host boundaries must not affect visual gain-reduction peaks. Test the
    // same fixed 10ms display windows using 128 and 1024 sample callbacks.
    std::vector<float> original(48000);
    for(int i=0;i<48000;++i)
    {
        const double t=i/kRate;
        const double envelope=0.12+0.7*std::abs(std::sin(2*kPi*1.7*t));
        original[static_cast<size_t>(i)]=static_cast<float>(
            envelope*(std::sin(2*kPi*220*t)+0.3*std::sin(2*kPi*1760*t)));
    }
    const auto capture=[&](int hostSize)
    {
        Core core;
        core.prepare(kRate);
        Settings settings;
        settings.gainDb=8.0;
        settings.ceilingDb=-1.0;
        core.setSettings(settings);
        auto buffer=original;
        std::vector<float> reductions;
        reductions.reserve(100);
        int frameFill=0;
        float frameMax=0.0f;
        for(int base=0;base<48000;)
        {
            const int call=std::min(hostSize,48000-base);
            int used=0;
            while(used<call)
            {
                const int n=std::min(call-used,480-frameFill);
                core.process(buffer.data()+base+used,nullptr,n);
                frameMax=std::max(frameMax,static_cast<float>(core.blockReductionMaxDb()));
                frameFill+=n;
                used+=n;
                if(frameFill==480)
                {
                    reductions.push_back(frameMax);
                    frameFill=0;
                    frameMax=0;
                }
            }
            base+=call;
        }
        assert(reductions.size()==100);
        return reductions;
    };
    const auto smallBuffers=capture(128);
    const auto largeBuffers=capture(1024);
    for(size_t i=0;i<smallBuffers.size();++i)
        assert(std::abs(smallBuffers[i]-largeBuffers[i])<1.0e-5f);
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

static void testStereoIntegratedAndLra()
{
    using APEX::Limiter::LoudnessSummary;
    LoudnessSummary whole,split;
    whole.prepare(kRate);
    split.prepare(kRate);
    assert(!std::isfinite(whole.integratedLufs()));
    assert(whole.loudnessRange()==0.0f);

    const int length=static_cast<int>(12*kRate);
    std::vector<float> l(static_cast<size_t>(length)),r(static_cast<size_t>(length));
    for(int i=0;i<length;++i)
    {
        const double t=i/kRate;
        // Same stereo programme in two block schedules; five seconds
        // quieter, followed by an abrupt, controlled +12dB level jump.
        const double amplitude=t<5.0?0.035:0.14;
        l[static_cast<size_t>(i)]=static_cast<float>(
            amplitude*std::sin(2.0*kPi*997.0*t));
        r[static_cast<size_t>(i)]=static_cast<float>(
            amplitude*0.82*std::sin(2.0*kPi*701.0*t));
    }
    whole.process(l.data(),r.data(),length);
    for(int i=0;i<length;i+=127)
        split.process(l.data()+i,r.data()+i,std::min(127,length-i));
    const float iA=whole.integratedLufs();
    const float iB=split.integratedLufs();
    const float rangeA=whole.loudnessRange();
    assert(std::isfinite(iA) && iA>-40.0f && iA<-10.0f);
    assert(std::abs(iA-iB)<0.01f);
    assert(std::abs(rangeA-split.loudnessRange())<0.01f);
    assert(rangeA>2.0f && rangeA<30.0f);

    whole.reset();
    assert(!std::isfinite(whole.integratedLufs()));
    assert(whole.loudnessRange()==0.0f);
    whole.process(l.data(),r.data(),length);
    assert(std::abs(whole.integratedLufs()-iA)<0.01f);
    assert(std::abs(whole.loudnessRange()-rangeA)<0.01f);

    // Mono session is a supported host layout and must remain finite.
    LoudnessSummary mono;
    mono.prepare(44100.0);
    auto sample=signal(44100*5,0.1);
    mono.process(sample.data(),nullptr,static_cast<int>(sample.size()));
    assert(std::isfinite(mono.integratedLufs()));
    assert(mono.loudnessRange()>=0.0f);
}


static void testFIRReconstructionActuallyReducesIntersamplePeak()
{
    using APEX::Limiter::TruePeakReference;
    // Use a phase-shifted Nyquist-adjacent sine with hidden overshoots.
    const int frames=8192;
    std::vector<float> source(static_cast<size_t>(frames));
    for(int i=0;i<frames;++i)
        source[static_cast<size_t>(i)]=static_cast<float>(
            std::sin(kPi*0.5*i+kPi*0.25));
    const auto render=[&](bool isp,int quality)
    {
        Core core;
        core.prepare(kRate);
        Settings p;
        p.gainDb=0.0;
        p.ceilingDb=-1.0;
        p.lookaheadMs=5.0;
        p.truePeak=isp;
        p.detectorQuality=quality;
        core.setSettings(p);
        auto samples=source;
        for(int offset=0;offset<frames;offset+=127)
            core.process(samples.data()+offset,nullptr,
                         std::min(127,frames-offset));
        return TruePeakReference::analyze(samples.data(),frames,32);
    };
    const auto samplesOnly=render(false,0);
    const auto reconstructed4=render(true,2);
    const double ceiling=std::pow(10.0,-1.0/20.0);
    assert(samplesOnly.samplePeak<ceiling);
    assert(samplesOnly.reconstructedPeak>ceiling*1.08);
    // This is a directional quality regression, NOT TP certification.
    assert(reconstructed4.reconstructedPeak <
           samplesOnly.reconstructedPeak*0.98);
    std::cout<<"APEX FIR ISP audit: sample-only reconstructed peak "
             <<samplesOnly.reconstructedPeak
             <<", enabled 4x FIR reconstructed peak "
             <<reconstructed4.reconstructedPeak<<"\n";
}

static void testOfflineReferenceDetectsRealIntersampleOvershoot()
{
    // An adversarial 12 kHz sine sampled at +45 degrees has 0.707 sample
    // peaks but an almost 1.0 reconstructed peak: a sample peak limiter
    // with -1 dBFS ceiling cannot identify the gap by sample values alone.
    using APEX::Limiter::TruePeakReference;
    std::vector<float> phaseShifted(4096);
    for(int i=0;i<static_cast<int>(phaseShifted.size());++i)
        phaseShifted[static_cast<size_t>(i)]=static_cast<float>(
            std::sin(kPi*i*0.5 + kPi*0.25));
    const auto r32=TruePeakReference::analyze(
        phaseShifted.data(),static_cast<int>(phaseShifted.size()),32);
    const auto r4=TruePeakReference::analyze(
        phaseShifted.data(),static_cast<int>(phaseShifted.size()),4);
    assert(r32.samplePeak>0.70 && r32.samplePeak<0.72);
    assert(r32.reconstructedPeak>0.98 && r32.reconstructedPeak<1.02);
    assert(std::abs(r32.reconstructedPeak-r4.reconstructedPeak)<0.01);

    std::vector<float> silence(256,0.0f);
    const auto quiet=TruePeakReference::analyze(
        silence.data(),static_cast<int>(silence.size()),32);
    assert(quiet.samplePeak==0.0 && quiet.reconstructedPeak==0.0);

    // Deliberate diagnostic of the existing SAMPLE-only mode: reveal the
    // missing true-peak guard instead of misreporting 'certified' limiting.
    Core sampleOnly;
    sampleOnly.prepare(kRate);
    Settings cfg;
    cfg.truePeak=false;
    cfg.detectorQuality=0;
    cfg.ceilingDb=-1.0;
    cfg.gainDb=0.0;
    sampleOnly.setSettings(cfg);
    auto output=phaseShifted;
    sampleOnly.process(output.data(),nullptr,static_cast<int>(output.size()));
    const auto actual=TruePeakReference::analyze(
        output.data(),static_cast<int>(output.size()),32);
    const double ceiling=std::pow(10.0,cfg.ceilingDb/20.0);
    assert(actual.samplePeak<ceiling);
    assert(actual.reconstructedPeak>ceiling*1.08);
    std::cout<<"APEX TP audit: sample peak "<<actual.samplePeak
             <<", reconstructed 32x peak "<<actual.reconstructedPeak
             <<", -1 dBFS ceiling "<<ceiling
             <<" (sample-only mode still requires true-peak protection)\n";
}

static void testRealAudioOversamplerRunsAndRejectsAliasing()
{
    using APEX::Limiter::AudioRateConverter;
    struct Context { int64_t highRateSamples=0; };
    const auto pass=[](void* opaque,float*,float*,int samples) noexcept
    {
        static_cast<Context*>(opaque)->highRateSamples+=samples;
    };
    const int frames=4096;
    auto inputL=signal(frames,0.68);
    std::vector<float> inputR(static_cast<size_t>(frames));
    for(int i=0;i<frames;++i)
        inputR[static_cast<size_t>(i)]=static_cast<float>(
            0.48*std::sin(2*kPi*2200.0*i/kRate+0.37));

    for(int factor:{1,2,4,8,16,32})
    {
        const auto render=[&](int hostBlock)
        {
            AudioRateConverter converter;
            converter.prepare(factor,128);
            assert(converter.factor()==factor);
            assert(converter.stages()<=5);
            auto left=inputL,right=inputR;
            Context ctx;
            for(int off=0;off<frames;off+=hostBlock)
                converter.process(left.data()+off,right.data()+off,
                    std::min(hostBlock,frames-off),&ctx,pass);
            assert(ctx.highRateSamples==static_cast<int64_t>(frames)*factor);
            return std::array<std::vector<float>,2>{std::move(left),std::move(right)};
        };
        const auto odd=render(37);
        const auto large=render(1001);
        double inputEnergy=0.0,outputEnergy=0.0;
        for(int i=1200;i<frames;++i)
        {
            const auto n=static_cast<size_t>(i);
            assert(std::isfinite(odd[0][n]) && std::isfinite(odd[1][n]));
            assert(std::abs(odd[0][n]-large[0][n])<1.0e-5);
            assert(std::abs(odd[1][n]-large[1][n])<1.0e-5);
            inputEnergy+=inputL[n]*inputL[n];
            outputEnergy+=odd[0][n]*odd[0][n];
        }
        const double ratio=std::sqrt(outputEnergy/inputEnergy);
        // Mono/stereo 32x filtering may shift phase but should preserve
        // a low-frequency programme's RMS without accidental 32x gain.
        assert(ratio>0.93 && ratio<1.07);
    }

    // 32x-nyquist-band oscillations must be rejected by the downsampler.
    // Otherwise extra nonlinear high-rate harmonics would alias into APEX.
    AudioRateConverter high;
    high.prepare(32,128);
    std::vector<float> silence(4096,0.0f);
    struct Oscillator { int64_t cursor=0; } oscillator;
    const auto inject=[](void* opaque,float* left,float*,int count) noexcept
    {
        auto& source=*static_cast<Oscillator*>(opaque);
        for(int n=0;n<count;++n,++source.cursor)
            left[n]=static_cast<float>(std::sin(
                2.0*3.14159265358979323846*0.36*source.cursor));
    };
    for(int offset=0;offset<static_cast<int>(silence.size());offset+=67)
        high.process(silence.data()+offset,nullptr,
                     std::min(67,static_cast<int>(silence.size())-offset),
                     &oscillator,inject);
    double outEnergy=0.0;
    for(size_t i=1200;i<silence.size();++i)
        outEnergy+=static_cast<double>(silence[i])*silence[i];
    const double rms=std::sqrt(outEnergy/(silence.size()-1200));
    assert(rms<0.02);
}


static void testReal32xProcessedLimiterAttenuatesTruePeaks()
{
    using APEX::Limiter::AudioRateConverter;
    using APEX::Limiter::TruePeakReference;
    // Actual high-rate DSP, unlike detector-only 32x peak measurement:
    // 32 fresh audio samples enter Core for every one host audio sample,
    // and a real antialias FIR reconstructs the host-rate output.
    constexpr int frames=8192;
    std::vector<float> source(static_cast<size_t>(frames));
    for(int i=0;i<frames;++i)
        source[static_cast<size_t>(i)]=static_cast<float>(
            std::sin(0.5*kPi*i+0.25*kPi));
    Settings s;
    s.truePeak=false; // avoid double FIR detection at 32x DSP rate
    s.detectorQuality=0;
    s.ceilingDb=-1.0;
    s.gainDb=0.0;
    s.lookaheadMs=5.0;
    s.attackMs=0.01;
    s.releaseMs=50.0;
    Core sampleRateDSP;
    sampleRateDSP.prepare(kRate);
    sampleRateDSP.setSettings(s);
    auto native=source;
    for(int i=0;i<frames;i+=127)
        sampleRateDSP.process(native.data()+i,nullptr,std::min(127,frames-i));
    const auto nativePeak=TruePeakReference::analyze(
        native.data(),frames,32);

    AudioRateConverter oversample;
    oversample.prepare(32,128);
    Core highRateDSP;
    highRateDSP.prepare(32*kRate);
    highRateDSP.setSettings(s);
    const auto callback=[](void* opaque,float* l,float* r,int n) noexcept
    {
        static_cast<Core*>(opaque)->process(l,r,n);
    };
    auto oversampled=source;
    for(int i=0;i<frames;i+=173)
        oversample.process(oversampled.data()+i,nullptr,
                           std::min(173,frames-i),&highRateDSP,callback);
    const auto highRatePeak=TruePeakReference::analyze(
        oversampled.data(),frames,32);
    assert(nativePeak.reconstructedPeak>0.98);
    assert(std::isfinite(highRatePeak.reconstructedPeak));
    assert(highRatePeak.reconstructedPeak<nativePeak.reconstructedPeak*0.98);
    std::cout<<"APEX ACTUAL 32x audio DSP sample-only TP="
             <<nativePeak.reconstructedPeak
             <<" -> processed+antialiased TP="<<highRatePeak.reconstructedPeak
             <<"; this is a directional test, NOT certified true peak or latency\n";
}

// The JUCE >1x host owns dry bypass at project rate. Switching the high-
// rate Core to WET-only must never change processed signal samples compared
// with the pre-optimization implementation, even during parameter automation.
// Legacy 1x Core bypass is separately exercised above.
static void testExternalHostDryLanePreserves32xWetAudio()
{
    using APEX::Limiter::AudioRateConverter;
    constexpr int frames=4096, factor=32;
    auto sourceL=signal(frames,0.72);
    std::vector<float> sourceR(static_cast<size_t>(frames));
    for(int i=0;i<frames;++i)
        sourceR[static_cast<size_t>(i)]=static_cast<float>(
            0.44*std::sin(2.0*kPi*1709.0*i/kRate+0.2));
    const auto render=[&](bool hostOwnsBypass)
    {
        Core core;
        core.prepare(kRate*factor,true,hostOwnsBypass);
        AudioRateConverter os;
        os.prepare(factor,128,57);
        auto l=sourceL,r=sourceR;
        const auto high=[](void* ctx,float* left,float* right,int size) noexcept
        {
            static_cast<Core*>(ctx)->process(left,right,size);
        };
        int base=0,step=0;
        while(base<frames)
        {
            Settings p;
            p.gainDb=(step%3==0)?0.0:((step%3==1)?8.0:19.0);
            p.ceilingDb=(step%2==0)?-1.0:-6.0;
            p.lookaheadMs=(step%2==0)?5.0:0.5;
            p.releaseMs=(step%3==0)?55.0:200.0;
            p.attackMs=(step%2==0)?0.05:1.0;
            p.truePeak=false;
            p.detectorQuality=0;
            p.bypass=false; // host handles external dry/bypass, not the Core
            core.setSettings(p);
            const int count=std::min(frames-base,step%2==0?37:113);
            os.process(l.data()+base,r.data()+base,count,&core,high);
            base+=count;
            ++step;
        }
        return std::array<std::vector<float>,2>{std::move(l),std::move(r)};
    };
    const auto reference=render(false);
    const auto external=render(true);
    assert(reference[0].size()==external[0].size());
    assert(reference[0]==external[0] && reference[1]==external[1]);
    std::cout<<"APEX 32x external-host dry bypass: bit-exact wet stereo, "
             <<frames*2<<" samples under automation"<<std::endl;
}

int main()
{
    testParameterSanitisation();
    testSilenceAndCorruptInputs();
    testNoUnwantedGainBelowCeiling();
    testBypassIsTrulyDryAndLatencyAligned();
    testDitherDeterminismAndCeiling();
    testBlockSizeIndependence();
    testReductionTelemetryAcrossHostBlocks();
    testReset();
    testStereoIntegratedAndLra();
    testOfflineReferenceDetectsRealIntersampleOvershoot();
    testFIRReconstructionActuallyReducesIntersamplePeak();
    testRealAudioOversamplerRunsAndRejectsAliasing();
    testReal32xProcessedLimiterAttenuatesTruePeaks();
    testExternalHostDryLanePreserves32xWetAudio();
    for (const int block : {1,17,64,128,257,1024})
        for(int style=0;style<8;++style)
            for(int quality=0;quality<4;++quality)
                runCeiling(block,style,quality);
    std::cout << "APEX Bubblegum Limiter C++17 DSP gates passed\n";
}
