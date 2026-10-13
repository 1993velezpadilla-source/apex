// EXPERIMENTAL 12-ms research, not a selectable production profile.
// Measure the SAME shipped DspCore at 12 vs 18 ms on vowel-rich input.
// Do not claim better singer sound just because plugin PDC falls.
// A weak pitch result rejects the candidate for release; safety/PDC failures
// fail CI outright. The 40-ms production default must remain unchanged.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
constexpr int block=128;
struct Result {
    int pdc=0;
    double cents=0.0,peak=0.0;
    bool voiced=false,finite=true,bypassCorrect=false;
};
Result probe(double sr,double targetHz,double detune,
             APEX::TuneLive::ShifterProfile profile) {
    using namespace APEX::TuneLive;
    const int frames=static_cast<int>(sr*2.0);
    const double hz=targetHz*std::pow(2.0,detune/12.0);
    std::vector<float> input(static_cast<std::size_t>(frames));
    for(int i=0;i<frames;++i) {
        const double time=i/sr;
        double v=0.29*std::sin(2*pi*hz*time);
        // Harmonics approximate the onset of voiced formant contours,
        // not a complete real glottal/vowel acoustics model.
        for(int h=2;h<=13;++h) {
            const double fh=hz*h;
            const double envelope=0.025
                +0.20*std::exp(-0.5*std::pow((fh-750.0)/210.0,2))
                +0.12*std::exp(-0.5*std::pow((fh-1250.0)/280.0,2));
            v+=envelope/h*std::sin(2*pi*fh*time+0.13*h);
        }
        input[static_cast<std::size_t>(i)]=static_cast<float>(v);
    }
    DspCore dsp;
    dsp.prepare(sr,block,1,profile);
    DspSettings cfg;
    cfg.correction=1.0f;
    cfg.retune=0.0f;
    cfg.humanize=0.0f;
    cfg.flexTune=0.0f;
    cfg.wet=1.0f;
    for(int start=0;start<frames;start+=block) {
        float* channels[1]={input.data()+start};
        dsp.process(channels,1,std::min(block,frames-start),cfg);
    }
    PitchTracker tracker;
    tracker.prepare(sr);
    Result result;
    result.pdc=dsp.latencySamples();
    for(int i=frames-static_cast<int>(sr*0.5);i<frames;++i) {
        const float v=input[static_cast<std::size_t>(i)];
        result.finite&=std::isfinite(v);
        result.peak=std::max(result.peak,std::abs(static_cast<double>(v)));
        tracker.push(std::isfinite(v)?v:0.0f);
    }
    const auto note=tracker.latest();
    result.voiced=note.voiced;
    result.cents=note.voiced ? 1200.0*std::log2(note.hz/targetHz):9999.0;
    // Transparent bypass must reproduce any source exactly at sample PDC.
    dsp.reset();
    cfg.bypass=true;
    for(int i=0;i<frames;++i)
        input[static_cast<std::size_t>(i)]=
            static_cast<float>(0.23*std::sin(2*pi*hz*i/sr));
    for(int i=0;i<frames;i+=block) {
        float* channels[1]={input.data()+i};
        dsp.process(channels,1,std::min(block,frames-i),cfg);
    }
    double maxErr=0;
    for(int i=frames-static_cast<int>(sr*0.25);i<frames;++i) {
        const double expected=0.23*std::sin(2*pi*hz*(i-result.pdc)/sr);
        maxErr=std::max(maxErr,std::abs(input[static_cast<std::size_t>(i)]-expected));
    }
    result.bypassCorrect=maxErr<1.0e-6;
    return result;
}
}
int main() {
    using namespace APEX::TuneLive;
    DspCore shipping;
    shipping.prepare(48000.0,block,1);
    if(shipping.profile()!=ShifterProfile::HighQuality40ms
       ||shipping.latencySamples()!=1920) {
        std::cerr<<"FAIL: experimental study changed default shipping latency\n";
        return 1;
    }
    int safe=0,acceptable12ms=0,total=0;
    for(const double sr:std::array<double,2>{{44100.0,48000.0}})
      for(const double target:std::array<double,2>{{220.0,440.0}})
       for(const double detune:std::array<double,2>{{-0.30,0.30}}) {
        const auto faster=probe(sr,target,detune,ShifterProfile::Experimental12ms);
        const auto baseline=probe(sr,target,detune,ShifterProfile::Experimental18ms);
        const int expected12=static_cast<int>(std::lround(sr*0.012));
        const int expected18=static_cast<int>(std::lround(sr*0.018));
        const bool valid=faster.finite && baseline.finite
            && faster.peak>0.005 && baseline.peak>0.005
            && faster.peak<2.0 && baseline.peak<2.0
            && faster.bypassCorrect && baseline.bypassCorrect
            && std::abs(faster.pdc-expected12)<=4
            && std::abs(baseline.pdc-expected18)<=4;
        if(!valid) {
            std::cerr<<"FAIL: 12/18ms candidate nonfinite, silent, clipped or false PDC\n";
            return 1;
        }
        ++safe;
        const bool eligible=faster.voiced && std::abs(faster.cents)<=18.0
          && std::abs(faster.cents)<std::abs(detune*100.0);
        if(eligible)++acceptable12ms;
        ++total;
        std::cout<<"12ms_vs_18ms sr="<<sr<<" target_hz="<<target
                 <<" detune_cents="<<detune*100.0
                 <<" experimental12_pdc="<<faster.pdc
                 <<" experimental18_pdc="<<baseline.pdc
                 <<" experimental12_residual_cents="<<faster.cents
                 <<" experimental18_residual_cents="<<baseline.cents
                 <<" voiced12="<<faster.voiced
                 <<" release_accuracy_candidate="<<eligible<<"\n";
    }
    std::cout<<"12ms_research_safety_pass="<<safe
             <<" sound_accuracy_eligible="<<acceptable12ms<<"/"<<total
             <<" (f0 only, not vowel/formant certification)\n";
    std::cout<<"PASS: 12-ms feasibility and exact PDC; "
                "EXPERIMENTAL ONLY, NOT RELEASE-READY\n";
}
