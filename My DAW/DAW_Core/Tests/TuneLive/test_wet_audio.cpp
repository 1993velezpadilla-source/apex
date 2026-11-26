#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// Independent source/target spectral projection for Android's new
// whole-signal renderer. This measures leakage, not merely the largest F0.
static double projectTone(const std::vector<float>& a,int rate,double hz)
{
    const int start=rate, count=static_cast<int>(a.size())-start;
    double re=0,im=0,weight=0;
    for(int i=0;i<count;++i) {
        const double w=0.5-0.5*std::cos(6.283185307179586*i/(count-1));
        const double phase=6.283185307179586*hz*(start+i)/rate;
        re+=w*a[static_cast<std::size_t>(start+i)]*std::cos(phase);
        im+=w*a[static_cast<std::size_t>(start+i)]*std::sin(phase);
        weight+=w;
    }
    return 2*std::hypot(re,im)/weight;
}
static bool testMobileSpectral()
{
    using namespace APEX::TuneLive;
    bool allShipping=true;
    for(int rate : {44100,48000,96000}) {
        for(auto profile : {ShifterProfile::HighQuality40ms, ShifterProfile::Experimental18ms, ShifterProfile::Experimental12ms}) {
            for(auto clock : {StudioControlClock::ApprovedHostChunks,StudioControlClock::ResearchFixed1ms})
            for(bool split : {false,true}) {
                constexpr double source=252.0,target=261.6255653;
                std::vector<float> dry(rate*3);
                for(int i=0;i<rate*3;++i) {
                    const double p=6.283185307179586*source*i/rate;
                    dry[static_cast<std::size_t>(i)]=static_cast<float>(0.25*std::sin(p)+0.08*std::sin(2*p));
                }
                auto audio=dry;
                DspCore core;
                core.prepare(rate,512,1,profile,clock,split);
                DspSettings s;
                s.key=1;s.scale=1;s.correction=s.wet=1.0f;
                s.retune=s.humanize=s.flexTune=s.vibratoPreserve=0.0f;
                const int blocks[]={32,64,96,128,192,256,512};
                auto run=[&](std::vector<float>& signal) {
                    for(int pos=0,k=0;pos<static_cast<int>(signal.size());++k) {
                        const int n=std::min(blocks[k%7],static_cast<int>(signal.size())-pos);
                        float* ch[]={signal.data()+pos};core.process(ch,1,n,s);pos+=n;
                    }
                };
                run(audio);
                // Long coherent projections are extremely sensitive to cents
                // error. Measure the actual emitted pitch independently and
                // locate its spectral peak, while retaining an exact original
                // F0 leakage test. Do not infer quality from peak F0 alone.
                PitchTracker meter;meter.prepare(rate);
                double peak=0.0,energy=0.0,maxStep=0.0;
                bool finite=true;
                for(int i=rate;i<rate*3;++i) {
                    const double v=audio[i];finite &= std::isfinite(v);
                    if(!finite)break;
                    meter.push(audio[i]);
                    peak=std::max(peak,std::abs(v));energy+=v*v;
                    maxStep=std::max(maxStep,std::abs(v-audio[i-1]));
                }
                const auto measured=meter.latest();
                const double cents=measured.voiced?1200*std::log2(measured.hz/target):9999;
                const double original=projectTone(audio,rate,source);
                double corrected=0.0,peakHz=0.0;
                // 0.25 Hz grid spanning +/- 18 cents of target.
                for(double f=target-2.75;f<=target+2.75;f+=0.25) {
                    const double level=projectTone(audio,rate,f);
                    if(level>corrected){corrected=level;peakHz=f;}
                }
                const double expectedMs=profile==ShifterProfile::HighQuality40ms?40:
                    (profile==ShifterProfile::Experimental18ms?18:12);
                const int window=static_cast<int>(std::lround(rate*expectedMs/1000));
                const int expected=window+(split?std::max(64,static_cast<int>(std::lround(window*0.25))):0);
                const int latency=core.latencySamples();
                const double tracked=core.frequencyHz(),requested=core.debugTargetShiftSemitones(),
                    applied=core.debugAppliedShiftSemitones(),wet=core.debugWetAmount();
                core.reset();s.bypass=true;audio=dry;run(audio);
                bool bypass=true;
                for(int i=latency;i<static_cast<int>(audio.size());++i)
                    bypass &= audio[i]==dry[i-latency];
                const bool pass=finite && std::abs(cents)<=18 && corrected>0.10
                    && original<0.10*corrected && peak<1.0 && maxStep<0.12
                    && std::abs(latency-expected)<=4 && bypass;
                std::cout<<"MOBILE_SPECTRAL_PROFILE rate="<<rate<<" window_ms="<<expectedMs
                    <<" clock="<<static_cast<int>(clock)<<" split="<<split<<" tracked="<<tracked
                    <<" requested_st="<<requested<<" applied_st="<<applied<<" wet="<<wet
                    <<" measured_hz="<<measured.hz<<" cents="<<cents<<" source_f0="<<original
                    <<" corrected_peak="<<corrected<<" corrected_peak_hz="<<peakHz
                    <<" rms="<<std::sqrt(energy/(2*rate))<<" peak="<<peak<<" max_step="<<maxStep
                    <<" PDC="<<latency<<" bypass="<<bypass<<" pass="<<pass<<"\n";
                if(profile==ShifterProfile::Experimental18ms && split &&
                   clock==StudioControlClock::ResearchFixed1ms) allShipping &= pass;
            }
        }
    }
    return allShipping;
}

// The exact same DSP class called by the APEX JUCE processor.
// Feed a detuned monophonic A4 and check the real processed audio's output F0.
int main() {
    if(!testMobileSpectral()) { std::cerr<<"Android spectral correction/bypass regression\n";return 2; }
    using namespace APEX::TuneLive;
    constexpr double sr=48000.0;
    constexpr int block=128;
    constexpr int frames=48000*2;
    const double inputHz=440.0*std::pow(2.0,0.30/12.0);
    std::vector<float> audio(frames);
    for(int i=0;i<frames;++i)
        audio[static_cast<std::size_t>(i)]=
            static_cast<float>(0.38*std::sin(6.283185307179586*inputHz*i/sr));

    DspCore plugin;
    plugin.prepare(sr,block,1);
    DspSettings knobs;
    knobs.correction=1.0f;
    knobs.retune=0.0f;
    knobs.key=9; // A
    knobs.scale=0;
    knobs.wet=1.0f;
    knobs.humanize=0.0f;
    // Hard-tune accuracy benchmark must disable the natural dead-zone.
    // Flex=0.35 intentionally leaves ~6.3 cents uncorrected, so testing
    // that natural setting as zero-error hard-tune was the wrong fixture.
    knobs.flexTune=0.0f;
    knobs.stability=0.4f;
    knobs.preserveFormants=true;

    for(int offset=0;offset<frames;offset+=block) {
        const int count=std::min(block,frames-offset);
        float* channel[1]={audio.data()+offset};
        plugin.process(channel,1,count,knobs);
    }

    PitchTracker out;
    out.prepare(sr);
    for(int i=frames-24000;i<frames;++i) {
        const float v=audio[static_cast<std::size_t>(i)];
        if(!std::isfinite(v)) {std::cerr<<"Nonfinite wet audio\n";return 1;}
        out.push(v);
    }
    const auto result=out.latest();
    if(!result.voiced) {std::cerr<<"Silent/unvoiced corrected output\n";return 1;}
    const double cents=1200.0*std::log2(result.hz/440.0);
    std::cout<<"dry_Hz="<<inputHz<<" wet_Hz="<<result.hz
             <<" cents_from_A4="<<cents
             <<" plugin_PDC_samples="<<plugin.latencySamples()
             <<" detector_window_ms="<<plugin.detectorWindowMs()
             <<" tracked_input_Hz="<<plugin.frequencyHz()
             <<" requested_shift_st="<<plugin.debugTargetShiftSemitones()
             <<" shifter_shift_st="<<plugin.debugAppliedShiftSemitones()
             <<" effective_wet="<<plugin.debugWetAmount()<<"\n";
    if(!std::isfinite(cents)||std::abs(cents)>18.0) {
        std::cerr<<"Corrected audio failed target A4 (+/-18 cents)\n";
        return 1;
    }
    // Real-time bypass should converge to delayed, uncorrected source.
    plugin.reset();
    std::vector<float> bypassAudio(frames);
    for(int i=0;i<frames;++i)
        bypassAudio[static_cast<std::size_t>(i)]=
            static_cast<float>(0.38*std::sin(6.283185307179586*inputHz*i/sr));
    knobs.bypass=true;
    for(int offset=0;offset<frames;offset+=block) {
        float* channel[1]={bypassAudio.data()+offset};
        plugin.process(channel,1,std::min(block,frames-offset),knobs);
    }
    PitchTracker bypassPitch;
    bypassPitch.prepare(sr);
    for(int i=frames-24000;i<frames;++i)
        bypassPitch.push(bypassAudio[static_cast<std::size_t>(i)]);
    const auto bypass=bypassPitch.latest();
    if(!bypass.voiced || std::abs(bypass.hz-inputHz)>inputHz*0.01) {
        std::cerr<<"Bypass failed to preserve source F0\n";
        return 1;
    }
    // Sample-accurate PDC: long-term bypass must reproduce the exact
    // unprocessed source delayed by the reported plugin latency.
    double sumSquared=0.0;
    int compared=0;
    for(int i=frames-16000;i<frames;++i) {
        const int inputPosition=i-plugin.latencySamples();
        if(inputPosition<0) continue;
        const double dry=0.38*std::sin(6.283185307179586*inputHz*inputPosition/sr);
        const double err=bypassAudio[static_cast<std::size_t>(i)]-dry;
        sumSquared+=err*err;
        ++compared;
    }
    const double bypassErrorRms=std::sqrt(sumSquared/std::max(1,compared));
    std::cout<<"bypass_PDC_error_RMS="<<bypassErrorRms<<"\\n";
    if(bypassErrorRms>1.0e-4) {
        std::cerr<<"Bypass timing mismatch to host latency disclosure\\n";
        return 1;
    }
    std::cout<<"PASS wet correction + exact PDC-aligned bypass on actual production DspCore\\n";
    return 0;
}
