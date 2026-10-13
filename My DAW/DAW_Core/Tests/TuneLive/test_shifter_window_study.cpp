// Controlled offline experiment, NOT a release quality certification.
// Measure the pinned Signalsmith Stretch at different window lengths before
// changing live monitoring latency. The experiment does not alter production.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

struct Probe {
    int windowMs=0, latencySamples=0;
    double centsError=0, inputHz=0, outputHz=0;
    bool healthy=false;
};
static Probe runProbe(int windowMs) {
    using Shifter=signalsmith::stretch::SignalsmithStretch<float>;
    constexpr double sr=48000.0;
    constexpr int frames=48000*3, block=128;
    const double detuned=440.0*std::pow(2.0,0.30/12.0);
    std::vector<float> input(static_cast<std::size_t>(frames)),
                       output(static_cast<std::size_t>(frames),0.0f);
    for(int i=0;i<frames;++i)
        input[static_cast<std::size_t>(i)]=
            static_cast<float>(0.38*std::sin(6.283185307179586*detuned*i/sr));
    Shifter shifter(0x41504558L);
    const int window=static_cast<int>(sr*windowMs*0.001);
    const int interval=window/4;
    shifter.configure(1,window,interval,false);
    shifter.setTransposeFactor(static_cast<float>(std::pow(2.0,-0.30/12.0)));
    shifter.setFormantFactor(1.0f,true);
    shifter.setFormantBase(static_cast<float>(detuned/sr));
    shifter.reset();
    for(int i=0;i<frames;i+=block) {
        const float* source[1]={input.data()+i};
        float* wet[1]={output.data()+i};
        const int count=std::min(block,frames-i);
        shifter.process(source,count,wet,count);
    }
    APEX::TuneLive::PitchTracker detected;
    detected.prepare(sr);
    bool healthy=true;
    for(int i=frames-24000;i<frames;++i) {
        const float v=output[static_cast<std::size_t>(i)];
        healthy&=std::isfinite(v);
        detected.push(std::isfinite(v)?v:0.0f);
    }
    const auto f0=detected.latest();
    Probe result{};
    result.windowMs=windowMs;
    result.latencySamples=shifter.inputLatency()+shifter.outputLatency();
    result.inputHz=detuned;
    result.outputHz=f0.hz;
    result.healthy=healthy && f0.voiced;
    result.centsError=(result.healthy ? 1200.0*std::log2(f0.hz/440.0) : 999.0);
    return result;
}
int main() {
    const std::array<int,4> windowsMs{{18,24,30,40}};
    for(const int ms:windowsMs) {
        const auto probe=runProbe(ms);
        std::cout<<"window_ms="<<ms
                 <<" pdc_samples="<<probe.latencySamples
                 <<" pdc_ms="<<1000.0*probe.latencySamples/48000.0
                 <<" input_hz="<<probe.inputHz
                 <<" output_hz="<<probe.outputHz
                 <<" residual_cents="<<probe.centsError
                 <<" voiced="<<probe.healthy<<"\\n";
        if(!probe.healthy || !std::isfinite(probe.centsError)) {
            std::cerr<<"FAIL: pinned shifter unstable in latency exploration\\n";
            return 1;
        }
    }
    // Deliberately no "winner" or accuracy threshold for the 18-30ms
    // exploratory windows; choose only after auditory voice/formant tests.
    std::cout<<"PASS: stable window-size exploration (not sound certification)\\n";
}
