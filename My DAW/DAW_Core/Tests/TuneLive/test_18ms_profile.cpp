// Experimental latency feasibility test on the production DSP, not a
// second/demo shifter. Includes exact sample-accurate PDC and real F0
// measurements; user-facing default MUST stay HighQuality40ms.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

static bool probe(double sampleRate,double targetHz,double detune) {
    using namespace APEX::TuneLive;
    const int sr=static_cast<int>(sampleRate),block=128,frames=sr*2;
    const double dryHz=targetHz*std::pow(2.0,detune/12.0);
    std::vector<float> audio(static_cast<std::size_t>(frames));
    for(int i=0;i<frames;++i) {
        const double t=static_cast<double>(i)/sampleRate;
        audio[static_cast<std::size_t>(i)]=static_cast<float>(
            0.30*std::sin(6.283185307179586*dryHz*t)
          + 0.035*std::sin(6.283185307179586*dryHz*2.0*t));
    }
    DspCore core;
    core.prepare(sampleRate,block,1,ShifterProfile::Experimental18ms);
    if(core.profile()!=ShifterProfile::Experimental18ms)return false;
    const int expected=static_cast<int>(std::lround(0.018*sampleRate));
    const int latency=core.latencySamples();
    if(latency<expected-4 || latency>expected+4) {
        std::cerr<<"Wrong 18ms mode PDC: "<<latency<<" vs "<<expected<<"\n";
        return false;
    }
    DspSettings cfg;
    cfg.correction=1.0f;cfg.retune=0.0f;cfg.wet=1.0f;
    cfg.humanize=0.0f;cfg.flexTune=0.0f;cfg.preserveFormants=true;
    cfg.scale=0; // chromatic
    for(int i=0;i<frames;i+=block) {
        float* input[1]={audio.data()+i};
        core.process(input,1,std::min(block,frames-i),cfg);
    }
    PitchTracker wet;
    wet.prepare(sampleRate);
    bool finite=true;float peak=0.0f;
    for(int i=frames-sr/2;i<frames;++i){
        const float value=audio[static_cast<std::size_t>(i)];
        finite&=std::isfinite(value);
        peak=std::max(peak,std::abs(value));
        wet.push(std::isfinite(value)?value:0.0f);
    }
    const auto f0=wet.latest();
    const double errorCents=f0.voiced ? 1200.0*std::log2(f0.hz/targetHz):9999.0;
    std::cout<<"EXPERIMENTAL 18ms sr="<<sr
             <<" target="<<targetHz<<" input="<<dryHz
             <<" detune_semitones="<<detune
             <<" corrected_hz="<<f0.hz
             <<" residual_cents="<<errorCents
             <<" pdc_samples="<<latency
             <<" peak="<<peak<<"\n";
    // Only safety and basic correction criteria are release-gated.
    // Passing this does NOT mean pro quality or Antares low-latency parity.
    const bool safe=finite&&peak>0.01f&&peak<2.0f&&f0.voiced
                 &&std::isfinite(errorCents)&&std::abs(errorCents)<55.0;
    if(!safe)return false;
    if(sr==48000 && targetHz==440.0 && detune>0.0 &&
       std::abs(errorCents)>=30.0) {
        std::cerr<<"18ms candidate cannot improve 30-cent sharp A4\n";
        return false;
    }

    // Bypassed PDC must remain EXACT even though wet mode was shorter.
    core.reset();cfg.bypass=true;
    for(int i=0;i<frames;++i)audio[static_cast<std::size_t>(i)]=static_cast<float>(
        0.23*std::sin(6.283185307179586*dryHz*i/sampleRate));
    for(int i=0;i<frames;i+=block) {
        float* input[1]={audio.data()+i};
        core.process(input,1,std::min(block,frames-i),cfg);
    }
    double maxError=0;
    for(int i=frames-sr/3;i<frames;++i) {
        const double baseline=0.23*std::sin(
            6.283185307179586*dryHz*(i-latency)/sampleRate);
        maxError=std::max(maxError,
                 std::abs(audio[static_cast<std::size_t>(i)]-baseline));
    }
    return maxError<1.0e-6;
}
int main() {
    using APEX::TuneLive::DspCore;
    using APEX::TuneLive::ShifterProfile;
    DspCore safeDefault;
    safeDefault.prepare(48000.0,128,1);
    if(safeDefault.latencySamples()!=1920 ||
       safeDefault.profile()!=ShifterProfile::HighQuality40ms) {
        std::cerr<<"FAIL: experiment changed the production default\n";
        return 1;
    }
    bool pass=true;
    for(double rate:std::array<double,2>{{44100.0,48000.0}})
        for(double base:std::array<double,2>{{220.0,440.0}})
            for(double detune:std::array<double,2>{{-0.30,0.30}})
                pass&=probe(rate,base,detune);
    if(!pass) {
        std::cerr<<"FAIL: experimental 18ms DSP quality/safety gate\n";
        return 1;
    }
    std::cout<<"PASS: 8 experimental 18ms DSP probes and exact dry PDC (NOT release-ready)\n";
}
