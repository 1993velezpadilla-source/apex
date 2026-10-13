// Research-only: direct pinned SignalsmithStretch downshift transfer function.
//
// Objective: APEX Tune HQ40 is commanded ~-30c on input sung +30c,
// but measured late wet output remains +20c at A3 and +15c at A4.
// The independent Goertzel spectrum agreed with YIN, indicating real
// pitch residual, not a standalone detector meter error.
// Now isolate the pitch shifter from APEX pitch detection, note policy
// and wet crossfade. Sweep the *requested* shifter ratios.
// Never install auto-calibration in the production singer-approved plugin
// based solely on this synthetic study.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
constexpr int rate=48000,block=128,frames=rate*2,windowSamples=8192;
double energy(const std::vector<float>& audio,double frequency) {
    const int offset=frames-windowSamples-512;
    const double recurrence=2*std::cos(2*pi*frequency/rate);
    double q1=0,q2=0;
    for(int n=0;n<windowSamples;++n) {
        const double hann=0.5*(1-std::cos(2*pi*n/(windowSamples-1)));
        const double q0=audio[static_cast<std::size_t>(offset+n)]*hann
                      +recurrence*q1-q2;
        q2=q1;q1=q0;
    }
    return std::sqrt(std::max(0.0,q1*q1+q2*q2-recurrence*q1*q2));
}
double spectralCents(const std::vector<float>& audio,double targetHz) {
    double peak=0,best=9999;
    for(int cents=-90;cents<=90;++cents) {
        const double a=energy(audio,targetHz*std::pow(2.0,cents/1200.0));
        if(a>peak){peak=a;best=cents;}
    }
    return best;
}
void run(double targetHz,int requestedShiftCents) {
    const double sourceHz=targetHz*std::pow(2.0,30.0/1200.0);
    std::vector<float> original(frames),wet(frames);
    double phase=0;
    for(int i=0;i<frames;++i) {
        phase+=2*pi*sourceHz/rate;
        original[static_cast<std::size_t>(i)]=static_cast<float>(
           0.28*std::sin(phase)+0.06*std::sin(2*phase));
    }
    signalsmith::stretch::SignalsmithStretch<float> stretch{0x41504558L};
    const int window=static_cast<int>(std::lround(rate*0.040));
    stretch.configure(1,window,window/4,false);
    stretch.reset();
    stretch.setTransposeFactor(static_cast<float>(
        std::pow(2.0,static_cast<double>(requestedShiftCents)/1200.0)));
    stretch.setFormantFactor(1.0f,true);
    stretch.setFormantBase(static_cast<float>(sourceHz/rate));
    std::vector<float> scratch(block,0.0f);
    for(int offset=0;offset<frames;offset+=block) {
        const int count=std::min(block,frames-offset);
        const float* inputs[1]={original.data()+offset};
        float* outputs[1]={scratch.data()};
        stretch.process(inputs,count,outputs,count);
        for(int i=0;i<count;++i)
            wet[static_cast<std::size_t>(offset+i)]=scratch[static_cast<std::size_t>(i)];
    }
    const double observed=spectralCents(wet,targetHz);
    double peak=0;
    for(float sample:wet) {
        if(!std::isfinite(sample))throw std::runtime_error("nonfinite research shifter");
        peak=std::max(peak,std::abs(static_cast<double>(sample)));
    }
    const int pdc=stretch.inputLatency()+stretch.outputLatency();
    std::cout<<"RESEARCH_SHIFTER_TRANSFER"
             <<" note_hz="<<targetHz
             <<" source_detune_cents=30"
             <<" commanded_shift_cents="<<requestedShiftCents
             <<" measured_output_detune_cents="<<observed
             <<" transfer_error_cents="<<observed-(30+requestedShiftCents)
             <<" PDC_samples="<<pdc
             <<" peak="<<peak<<"\\n";
    if(pdc!=1920||peak<=0||peak>2||!std::isfinite(observed))
        throw std::runtime_error("invalid independent shifter transfer output");
}
}
int main() {
    try {
        for(double hz:std::array<double,2>{{220.0,440.0}})
            for(int shift:std::array<int,5>{{-20,-30,-40,-50,-60}})
                run(hz,shift);
        std::cout<<"RESEARCH_SHIFTER_TRANSFER_PASS: ten independently driven "
                    "non-shipping HQ40 window experiments; no production "
                    "ratio changed.\\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"RESEARCH_SHIFTER_TRANSFER_FAIL: "<<error.what()<<"\\n";
        return 1;
    }
}
