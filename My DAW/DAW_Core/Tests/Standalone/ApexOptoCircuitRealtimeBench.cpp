// APEX Opto 2A: host-shaped STANDALONE 48 kHz CPU survey.
// This is NOT a measurement inside APEX DAW, VST3, Windows audio engine,
// an Apollo driver, or user hardware. CI runner noise makes timings advisory.
// No heap allocations/locks are needed by the research DSP callback.
#include "../../Source/CompressorCore/ApexOptoCircuitPathResearchCore.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

using APEX::Compressor::ApexOptoCircuitPathResearchCore;
using APEX::Compressor::ApexOptoCircuitResearchParameters;

namespace {
constexpr int sampleRate=48000;
constexpr int maxFrames=128;
constexpr int measuredBlocks=512;
constexpr double pi=3.14159265358979323846;
using Clock=std::chrono::steady_clock;

void survey(int blockFrames,bool magnetic) {
    if(blockFrames!=64 && blockFrames!=128)std::abort();
    ApexOptoCircuitPathResearchCore dsp;
    dsp.prepare(sampleRate,magnetic);
    ApexOptoCircuitResearchParameters p;
    p.peakReduction=77.0;
    p.emphasis=.4;
    p.gainDb=3.0;
    p.mix=.82;
    dsp.setParameters(p);
    if(dsp.latencySamples()!=(magnetic?32:0))std::abort();

    std::array<float,maxFrames> left{},right{};
    std::array<double,measuredBlocks> callbackMs{};
    double totalMs=0.0;
    double checksum=0.0;
    // Run 32 prefilling blocks outside timed section for steady state.
    for(int warm=0;warm<32;++warm){
        for(int n=0;n<blockFrames;++n){
            left[static_cast<std::size_t>(n)]=static_cast<float>(.24*std::sin((warm*blockFrames+n)*.034));
            right[static_cast<std::size_t>(n)]=static_cast<float>(.17*std::cos((warm*blockFrames+n)*.027));
        }
        dsp.process(left.data(),right.data(),blockFrames);
    }
    for(int block=0;block<measuredBlocks;++block){
        for(int n=0;n<blockFrames;++n){
            const auto index=static_cast<double>((block+32)*blockFrames+n);
            left[static_cast<std::size_t>(n)]=static_cast<float>(
                .26*std::sin(2*pi*110.0*index/sampleRate)
                +.08*std::sin(2*pi*4000.0*index/sampleRate));
            right[static_cast<std::size_t>(n)]=static_cast<float>(
                .19*std::sin(2*pi*155.0*index/sampleRate)
                +.07*std::sin(2*pi*6500.0*index/sampleRate));
        }
        const auto begin=Clock::now();
        dsp.process(left.data(),right.data(),blockFrames);
        const auto end=Clock::now();
        const auto ms=std::chrono::duration<double,std::milli>(end-begin).count();
        callbackMs[static_cast<std::size_t>(block)]=ms;
        totalMs+=ms;
        for(int n=0;n<blockFrames;++n){
            if(!std::isfinite(left[static_cast<std::size_t>(n)])
                    ||!std::isfinite(right[static_cast<std::size_t>(n)])) {
                std::cerr<<"Non-finite sample during Opto benchmark\n";
                std::abort();
            }
        }
        checksum+=static_cast<double>(left[0])+right[blockFrames-1];
    }
    std::sort(callbackMs.begin(),callbackMs.end());
    const double deadlineMs=1000.0*blockFrames/sampleRate;
    const double audioMs=deadlineMs*measuredBlocks;
    const auto p99=callbackMs[static_cast<std::size_t>(
        (measuredBlocks*99)/100)];
    const auto maxMs=callbackMs.back();
    const double dspShare=100.0*totalMs/audioMs;
    std::cout<<std::fixed<<std::setprecision(4)
             <<"mode="<<(magnetic?"4x":"1x")
             <<" frames="<<blockFrames
             <<" callback_budget_ms="<<deadlineMs
             <<" total_dsp_ms="<<totalMs
             <<" simulated_audio_ms="<<audioMs
             <<" mean_cpu_share_percent="<<dspShare
             <<" p99_callback_ms="<<p99
             <<" max_callback_ms="<<maxMs
             <<" checksum="<<checksum
             <<" (CI runner only; advisory)\n";
}
} // namespace

int main(){
    for(int frames:{64,128}){
        survey(frames,false);
        survey(frames,true);
    }
    std::cout<<"APEX Opto 48k stereo block survey completed (no host qualification)\n";
}
