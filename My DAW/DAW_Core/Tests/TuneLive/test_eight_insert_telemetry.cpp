// APEX Tune Live — CPU-only eight-insert performance benchmark.
// Unlike the original stress test, generates ALL vocal input offline, outside
// the processing clock. A benchmark measuring sin/pow synthesis inside the
// realtime block was not actually isolating plugin CPU load.
// Reports per-callback distribution; no CI CPU pass threshold because
// Windows runners are not ASIO/real-time audio devices.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
constexpr int kInserts=8,kBlock=128,kRate=48000,kSeconds=3;
constexpr int kFrames=kRate*kSeconds;
constexpr double kPi=3.14159265358979323846;
struct Instance {
    std::unique_ptr<APEX::TuneLive::DspCore> dsp{
        std::make_unique<APEX::TuneLive::DspCore>()};
    APEX::TuneLive::DspSettings settings;
    std::vector<float> left,right;
    int channels=1;
};
double percentile(std::vector<double> sorted,double fraction) {
    if(sorted.empty())return 0.0;
    const std::size_t index=static_cast<std::size_t>(
        std::floor(fraction*static_cast<double>(sorted.size()-1)));
    return sorted[std::min(index,sorted.size()-1)];
}
}
int main() {
    using namespace APEX::TuneLive;
    std::array<Instance,kInserts> effects;
    // Synthetic vowel-like waveform with musical vibrato + breath gap.
    // Every sample is generated before ANY measured DSP callback.
    for(int track=0;track<kInserts;++track) {
        auto& item=effects[static_cast<std::size_t>(track)];
        item.channels=(track==0||track==7)?2:1;
        item.dsp->prepare(kRate,kBlock,item.channels);
        item.settings.correction=1.0f;
        item.settings.retune=0.0f;
        item.settings.humanize=0.4f;
        item.settings.flexTune=0.0f;
        item.settings.wet=1.0f;
        item.settings.stability=0.25f;
        item.left.resize(kFrames);
        if(item.channels==2)item.right.resize(kFrames);
        const double fundamental=110.0*std::pow(2.0,track/4.0);
        double phase=0.0;
        for(int n=0;n<kFrames;++n) {
            const double t=static_cast<double>(n)/kRate;
            const double hz=fundamental*(1.0+0.003*std::sin(2*kPi*5.2*t));
            phase+=2*kPi*hz/kRate;
            const double envelope=(t>1.38 && t<1.46)?0.012:1.0;
            const double voiced=envelope*(0.28*std::sin(phase)+
                     0.075*std::sin(phase*2)+0.025*std::sin(phase*3));
            item.left[static_cast<std::size_t>(n)]=static_cast<float>(voiced);
            if(item.channels==2)
                item.right[static_cast<std::size_t>(n)]
                   =static_cast<float>(0.85*voiced);
        }
    }
    std::vector<double> milliseconds;
    milliseconds.reserve(static_cast<std::size_t>(kSeconds*kRate/kBlock));
    const double budgetMs=1000.0*kBlock/kRate;
    double totalMeasuredMs=0.0;
    for(int start=0;start<kFrames;start+=kBlock) {
        const int n=std::min(kBlock,kFrames-start);
        const auto begin=Clock::now();
        for(auto& item:effects) {
            float* channels[2]={item.left.data()+start,
                item.channels==2 ? item.right.data()+start : nullptr};
            item.dsp->process(channels,item.channels,n,item.settings);
        }
        const double ms=std::chrono::duration<double,std::milli>(
             Clock::now()-begin).count();
        // Leave one second for FFT/filter prime and record two warm blocks
        // worth of samples in the actual measurement window.
        if(start>=kRate) {
            milliseconds.push_back(ms);
            totalMeasuredMs+=ms;
        }
    }
    if(milliseconds.empty())return 1;
    double peak=0.0,energy=0.0;
    for(const auto& item:effects) {
        for(float sample:item.left) {
            if(!std::isfinite(sample))return 1;
            peak=std::max(peak,std::abs(static_cast<double>(sample)));
            energy+=static_cast<double>(sample)*sample;
        }
        for(float sample:item.right) {
            if(!std::isfinite(sample))return 1;
            peak=std::max(peak,std::abs(static_cast<double>(sample)));
            energy+=static_cast<double>(sample)*sample;
        }
    }
    const double maxBlockMs=*std::max_element(milliseconds.begin(),milliseconds.end());
    const auto overBudget=std::count_if(milliseconds.begin(),milliseconds.end(),
        [budgetMs](double ms){return ms>budgetMs;});
    std::sort(milliseconds.begin(),milliseconds.end());
    const double measuredStreamSeconds=kSeconds-1.0;
    std::cout<<"EIGHT_INSERTS_DSP_ONLY"
             <<" voices="<<kInserts<<" stereo_inserts=2 mono_inserts=6"
             <<" sample_rate="<<kRate<<" buffer="<<kBlock
             <<" budget_ms="<<budgetMs
             <<" measured_stream_sec="<<measuredStreamSeconds
             <<" processing_sec="<<totalMeasuredMs/1000.0
             <<" processing_to_realtime_ratio="<<totalMeasuredMs/(1000.0*measuredStreamSeconds)
             <<" median_aggregate_callback_ms="<<percentile(milliseconds,0.5)
             <<" p95_aggregate_callback_ms="<<percentile(milliseconds,0.95)
             <<" p99_aggregate_callback_ms="<<percentile(milliseconds,0.99)
             <<" worst_aggregate_callback_ms="<<maxBlockMs
             <<" budget_exceed_count="<<overBudget
             <<" measured_callback_count="<<milliseconds.size()
             <<" peak="<<peak
             <<" total_energy="<<energy<<"\n";
    if(!std::isfinite(totalMeasuredMs)||!std::isfinite(peak)||
       peak<=0||peak>8.0||energy<=0||!std::isfinite(energy))return 1;
    std::cout<<"PASS: measured only actual 8-insert production DSP execution. "
                "Host-runner timing is informational, NOT a real-ASIO xrun guarantee.\n";
}
