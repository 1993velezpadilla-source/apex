// Research-only A/B: faster 1-period / 1ms wet crossfade vs 3-period /
// 8ms baseline, WITHOUT pretending either one recognizes a note in 1ms.
// Source F0 comes from actual microphone-like samples via streaming YIN,
// never from a known oracle passed into the plugin. Independent YIN checks
// real emitted wet output and simple amplitude-step regression.
// HQ40 shipping DSP and the normal causal-research defaults are unchanged.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
struct Result {
    double voiceMs=0.0,halfWetMs=0.0,wetCents=9999.0,highMs=-1.0;
    double maxOutputStep=0.0,wetMix=0.0;
    bool finite=true;
};
Result run(double rate,double target,bool fast,bool highSeed=false) {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    const int samples=static_cast<int>(1.2*rate);
    const double input=target*std::pow(2.0,30.0/1200.0);
    std::vector<float> audio(static_cast<std::size_t>(samples));
    for(int i=0;i<samples;++i) {
        const double t=i/rate;
        const double rad=2.0*pi*input*t;
        audio[static_cast<std::size_t>(i)]=static_cast<float>(
            0.26*std::sin(rad)+0.053*std::sin(2*rad+0.14)
            +0.019*std::sin(3*rad+0.37));
    }
    HarmonicLivePrototype candidate;
    candidate.prepare(rate);
    APEX::TuneLive::VoiceSettings policy;
    policy.root=9;policy.scale=0;policy.correction=1.0f;
    policy.referenceHz=440.0f;policy.humanize=0.0f;
    policy.flexTune=0.0f;policy.noteHysteresis=0.1f;
    candidate.setVoiceSettings(policy);
    candidate.enableEarlyOnset(true);
    candidate.enableHighRegisterOnset(highSeed);
    candidate.enableResidualPreservation(true);
    candidate.enableFastEngagement(fast);
    constexpr std::array<int,6> chunks{{1,48,128,257,17,64}};
    for(int offset=0,iteration=0;offset<samples;++iteration) {
        const int n=std::min(chunks[static_cast<std::size_t>(iteration%6)],samples-offset);
        float* mono[1]={audio.data()+offset};
        candidate.process(mono,1,n);
        offset+=n;
    }
    APEX::TuneLive::PitchTracker independent;
    independent.prepare(rate);
    Result result;
    for(int i=0;i<samples;++i) {
        const float v=audio[static_cast<std::size_t>(i)];
        result.finite &=std::isfinite(v);
        if(i>0)
            result.maxOutputStep=std::max(result.maxOutputStep,
                std::abs(static_cast<double>(v-audio[static_cast<std::size_t>(i-1)])));
        if(i>=samples-static_cast<int>(0.4*rate))
            independent.push(v);
    }
    const auto checked=independent.latest();
    if(checked.voiced && checked.hz>0.0f)
        result.wetCents=1200.0*std::log2(checked.hz/target);
    result.voiceMs=1000.0*candidate.firstVoiceSample()/rate;
    result.highMs=candidate.firstHighSample()>=0?
        1000.0*candidate.firstHighSample()/rate:-1.0;
    result.halfWetMs=1000.0*candidate.firstHalfWetSample()/rate;
    result.wetMix=candidate.researchWetMix();
    return result;
}
}
int main() {
    bool success=true;
    for(double rate:std::array<double,3>{{44100.0,48000.0,96000.0}})
        for(double target:std::array<double,2>{{220.0,440.0}}) {
            const auto baseline=run(rate,target,false);
            const auto fast=run(rate,target,true);
            const double saved=baseline.halfWetMs-fast.halfWetMs;
            const bool response=fast.voiceMs>1.0 &&
                 fast.halfWetMs>fast.voiceMs &&
                 baseline.halfWetMs>fast.halfWetMs &&
                 saved>=3.0;
            const bool audio=fast.finite && baseline.finite &&
                 std::abs(fast.wetCents)<18.0 &&
                 std::abs(baseline.wetCents)<18.0 &&
                 fast.wetMix>0.95 &&
                 fast.maxOutputStep<0.65;
            const bool ok=response && audio;
            success &=ok;
            std::cout<<"CAUSAL_FAST_WET sr="<<rate
                     <<" target_hz="<<target
                     <<" first_yin_ms="<<fast.voiceMs
                     <<" baseline_wet50_ms="<<baseline.halfWetMs
                     <<" one_period_wet50_ms="<<fast.halfWetMs
                     <<" earlier_wet50_ms="<<saved
                     <<" fast_independent_wet_cents="<<fast.wetCents
                     <<" baseline_independent_wet_cents="<<baseline.wetCents
                     <<" max_step="<<fast.maxOutputStep
                     <<" safe="<<ok
                     <<" NOT_1MS_PITCH_RESPONSE=1\n";
        }
    std::cout<<"CAUSAL_FAST_WET_BUDGET "<<(success?"PASS":"FAIL")
             <<" — half-wet is a CONTROL MIX milestone, not sound-quality,"
               " spectral alignment, full acoustic latency or ASIO measurement\n";
    return success?0:1;
}
