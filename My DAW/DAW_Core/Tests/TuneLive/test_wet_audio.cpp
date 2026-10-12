#include "../../Source/TuneLiveCore/ApexTuneLiveVoiceControl.h"
#include "signalsmith-stretch.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// End-to-end portable wet audio proof (not a replacement for native APEX/Apollo smoke).
// A detuned 447.69 Hz voice-like sine must move toward A4 440 Hz.
int main() {
    using namespace APEX::TuneLive;
    constexpr double sr = 48000.0;
    constexpr int block = 128;
    constexpr int frames = 48000*2;
    const double inputHz = 440.0*std::pow(2.0,0.30/12.0);
    std::vector<float> source(frames), wet(frames);
    for (int i=0;i<frames;++i)
        source[static_cast<std::size_t>(i)] =
            static_cast<float>(0.38 * std::sin(6.283185307179586 * inputHz * i/sr));
    PitchTracker tracker;
    tracker.prepare(sr);
    VoiceControl control;
    VoiceSettings settings;
    settings.root=0; settings.scale=0; settings.correction=1.0f;
    settings.humanize=0.0f; settings.noteHysteresis=0.15f;
    signalsmith::stretch::SignalsmithStretch<float> stretch(20261012L);
    stretch.configure(1,1920,480,false);
    stretch.setFormantFactor(1.0f,true);
    float target = 0.0f, smoothed = 0.0f;
    for (int off=0;off<frames;off+=block) {
        const int count = std::min(block,frames-off);
        smoothed += (target-smoothed)*0.75f;
        stretch.setTransposeFactor(std::pow(2.0f,smoothed/12.0f));
        const float* input[1]={source.data()+off};
        float* output[1]={wet.data()+off};
        stretch.process(input,count,output,count);
        for (int i=0;i<count;++i) {
            if (tracker.push(input[0][i]))
                target=control.update(tracker.latest(),settings).shiftSemitones;
        }
    }
    PitchTracker outputDetector;
    outputDetector.prepare(sr);
    for (int i=frames-24000;i<frames;++i) {
        const float sample=wet[static_cast<std::size_t>(i)];
        if(!std::isfinite(sample)) {std::cerr<<"Nonfinite wet audio\n";return 1;}
        outputDetector.push(sample);
    }
    const auto tuned=outputDetector.latest();
    const double cents=1200.0*std::log2(tuned.hz/440.0);
    std::cout<<"input_Hz="<<inputHz<<" wet_Hz="<<tuned.hz
             <<" correction_error_cents="<<cents
             <<" reported_latency_samples="<<stretch.inputLatency()+stretch.outputLatency()<<"\n";
    if(!tuned.voiced || !std::isfinite(cents) || std::abs(cents)>18.0) {
        std::cerr<<"Wet pitch-correction path failed: must settle within 18 cents of A4\n";
        return 1;
    }
    return 0;
}
