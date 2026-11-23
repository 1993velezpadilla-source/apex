// RESEARCH ONLY: control-envelope latency is NOT the time at which audio
// differs from the original microphone waveform. Compare actual wet samples
// with a bit-exact copy of the dry input at cold-start. The detector gets
// audio only, not the frequency label. F0 is checked independently at tail.
// This measures first nontrivial *waveform deviation*, NOT the moment the
// voice sounds corrected, validated formants, or full mic-to-ear latency.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
constexpr double kPi=3.1415926535897932384626433832795;
struct Measured {
    double firstF0Ms=-1.0,halfControlMs=-1.0;
    double firstWaveChangeMs=-1.0, tailDifferenceRms=0.0;
    double outputCents=9999.0, maxStep=0.0;
    bool initialDryExact=true,finite=true;
};
Measured measure(double rate,double target,double detuneCents,bool fourHop) {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    const int count=static_cast<int>(0.58*rate);
    const double input=target*std::pow(2.0,detuneCents/1200.0);
    std::vector<float> dry(static_cast<std::size_t>(count));
    std::vector<float> wet(static_cast<std::size_t>(count));
    for(int i=0;i<count;++i) {
        const double phase=2.0*kPi*input*i/rate;
        dry[static_cast<std::size_t>(i)]=static_cast<float>(
            0.24*std::sin(phase)+0.062*std::sin(2.0*phase+0.23)
            +0.018*std::sin(3.0*phase+0.37));
    }
    wet=dry;
    HarmonicLivePrototype engine;
    engine.prepare(rate);
    APEX::TuneLive::VoiceSettings settings;
    settings.root=9;
    settings.scale=0;
    settings.correction=1.0f;
    settings.referenceHz=440.0f;
    settings.humanize=0.0f;
    settings.flexTune=0.0f;
    settings.noteHysteresis=0.10f;
    engine.setVoiceSettings(settings);
    engine.enableEarlyOnset(true);
    engine.enableHighRegisterOnset(true);
    engine.enableUltraHighRegisterOnset(true);
    if(fourHop)engine.enableUltraHighFastHopResearch(true);
    engine.enableFastEngagement(true);
    engine.enableResidualPreservation(true);
    // Deliberately mixed 1/17/64/128/257-sample host callbacks.
    constexpr std::array<int,5> blocks{{1,17,64,128,257}};
    for(int i=0,block=0;i<count;++block) {
        const int n=std::min(blocks[static_cast<std::size_t>(block%5)],count-i);
        float* data[1]={wet.data()+i};
        engine.process(data,1,n);
        i+=n;
    }
    Measured result;
    result.firstF0Ms=engine.firstVoiceSample()>=0
        ?1000.0*engine.firstVoiceSample()/rate:-1.0;
    result.halfControlMs=engine.firstHalfWetSample()>=0
        ?1000.0*engine.firstHalfWetSample()/rate:-1.0;
    const int firstMs=std::max(1,static_cast<int>(std::round(rate/1000.0)));
    // Original microphone sample must remain untouched for its first 1ms.
    for(int i=0;i<firstMs;++i)
        result.initialDryExact &= dry[static_cast<std::size_t>(i)]==
                                  wet[static_cast<std::size_t>(i)];
    const auto rmsDiff=[&](int begin,int end) {
        double sum=0.0;
        for(int i=begin;i<end;++i) {
            const double d=static_cast<double>(wet[static_cast<std::size_t>(i)])
                          -dry[static_cast<std::size_t>(i)];
            sum+=d*d;
        }
        return std::sqrt(sum/std::max(1,end-begin));
    };
    const int tailBegin=static_cast<int>(rate*0.38);
    result.tailDifferenceRms=rmsDiff(tailBegin,count);
    // Nontrivial waveform difference: 1ms sliding RMS above 0.15% FS
    // for three consecutive windows. This is NOT a corrected-note estimate.
    int consecutive=0;
    for(int begin=0;begin+firstMs<=count;begin+=firstMs) {
        const bool different=rmsDiff(begin,begin+firstMs)>0.0015;
        consecutive=different?consecutive+1:0;
        if(consecutive>=3) {
            result.firstWaveChangeMs=
                1000.0*(begin-2*firstMs)/rate;
            break;
        }
    }
    APEX::TuneLive::PitchTracker independent;
    independent.prepare(rate);
    for(int i=0;i<count;++i) {
        const auto v=wet[static_cast<std::size_t>(i)];
        result.finite &=std::isfinite(v);
        if(i>0)result.maxStep=std::max(result.maxStep,std::abs(
            static_cast<double>(v-wet[static_cast<std::size_t>(i-1)])));
        if(i>=tailBegin)independent.push(v);
    }
    const auto f0=independent.latest();
    if(f0.voiced && f0.hz>0.0f)
        result.outputCents=1200.0*std::log2(f0.hz/target);
    return result;
}
} // namespace
int main() {
    bool green=true;
    int checked=0;
    for(double rate:std::array<double,3>{{44100.0,48000.0,96000.0}})
      for(double target:std::array<double,2>{{440.0,660.0}})
       for(double detune:std::array<double,2>{{-30.0,30.0}}) {
        const Measured slow=measure(rate,target,detune,false);
        const Measured fast=measure(rate,target,detune,true);
        const auto valid=[](const Measured& x) {
            return x.initialDryExact && x.finite &&
                x.firstF0Ms>5.0 && x.firstWaveChangeMs>1.0 &&
                x.firstWaveChangeMs<100.0 &&
                x.halfControlMs>x.firstF0Ms &&
                x.tailDifferenceRms>0.015 &&
                std::abs(x.outputCents)<18.0 && x.maxStep<0.65;
        };
        // The 4-hop optimization must not delay *signal* engagement
        // more than a 2ms analysis/crossfade tolerance; no presumption
        // about the listener's subjective musical note perception.
        const bool pass=valid(slow)&&valid(fast) &&
            fast.firstF0Ms<=slow.firstF0Ms+0.1 &&
            fast.firstWaveChangeMs<=slow.firstWaveChangeMs+2.0;
        green &=pass;checked+=pass;
        std::cout<<"ULTRA_ACTUAL_AUDIO_CHANGE sr="<<rate
                 <<" target="<<target<<" detune_cents="<<detune
                 <<" eight_hop_f0_ms="<<slow.firstF0Ms
                 <<" four_hop_f0_ms="<<fast.firstF0Ms
                 <<" eight_hop_first_wave_diff_ms="<<slow.firstWaveChangeMs
                 <<" four_hop_first_wave_diff_ms="<<fast.firstWaveChangeMs
                 <<" four_hop_control_wet50_ms="<<fast.halfControlMs
                 <<" four_hop_tail_diff_rms="<<fast.tailDifferenceRms
                 <<" independent_corrected_tail_cents="<<fast.outputCents
                 <<" first_1ms_exact_dry="<<fast.initialDryExact
                 <<" passed="<<pass
                 <<" NOT_FIRST_CORRECTED_NOTE_OR_ASIO_LATENCY=1\n";
    }
    std::cout<<"ULTRA_ACTUAL_AUDIO_CHANGE_GATE passed="<<checked
             <<"/12 verdict="<<(green?"PASS":"FAIL")
             <<" signal_change_NOT_pitch_settling=1\n";
    return green?0:1;
}
