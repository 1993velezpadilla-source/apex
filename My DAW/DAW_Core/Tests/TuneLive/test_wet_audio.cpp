#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// The exact same DSP class called by the APEX JUCE processor.
// Feed a detuned monophonic A4 and check the real processed audio's output F0.
int main() {
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
             <<" detector_window_ms="<<plugin.detectorWindowMs()<<"\n";
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
