// NON-SHIPPING APEX Tune LIVE research stress: 8 independent instances,
// mixed mono / stereo, 44.1/48/96k, 64/128/257 host audio blocks.
// Enforces zero heap allocations inside EVERY process callback and checks
// independently measured wet output F0 + protected low male-register note.
// Real-time wallclock is emitted for diagnosis; shared CI runners are NOT
// authoritative hardware CPU benchmarks nor an Apollo Solo ASIO test.
// No 1ms musical note-recognition assertion is made here.
#define main previous_single_instance_allocation_gate_not_called
#include "test_causal_live_rt_no_alloc.cpp"
#undef main
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

namespace {
using APEX::TuneLive::Research::HarmonicLivePrototype;
constexpr int kTracks=8;
constexpr int kMaxBlock=257;
struct Track {
    HarmonicLivePrototype engine;
    APEX::TuneLive::PitchTracker checkWet;
    std::array<float,kMaxBlock> left{},right{};
    double phase=0.0;
    double inputHz=0.0,targetHz=0.0;
    int channels=1;
    bool valid=true;
    double peak=0.0,wetCents=9999.0;
};
}
int main() {
    bool allGreen=true;
    constexpr std::array<double,kTracks> notes{{
        220.0,440.0,660.0,440.0,220.0,660.0,440.0,660.0
    }};
    for(const double rate:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        std::array<Track,kTracks> tracks{};
        for(int id=0;id<kTracks;++id) {
            auto& t=tracks[static_cast<std::size_t>(id)];
            t.targetHz=notes[static_cast<std::size_t>(id)];
            t.inputHz=t.targetHz*std::pow(2.0,30.0/1200.0);
            t.channels=id<4?1:2;
            t.engine.prepare(rate,t.channels);
            APEX::TuneLive::VoiceSettings voice;
            voice.root=9;voice.scale=0;voice.correction=1.0f;
            voice.referenceHz=440.0f;voice.humanize=0.0f;
            voice.flexTune=0.0f;voice.noteHysteresis=0.10f;
            t.engine.setVoiceSettings(voice);
            t.engine.enableEarlyOnset(true);
            t.engine.enableHighRegisterOnset(true);
            t.engine.enableExperimentalEarlyTransitions(true);
            t.engine.enableResidualPreservation(true);
            t.engine.enableFastEngagement(true);
            t.checkWet.prepare(rate);
        }
        const int total=static_cast<int>(0.55*rate);
        int offset=0,iteration=0;
        std::array<int,3> sizes{{64,128,257}};
        gCallbackAllocs.store(0,std::memory_order_relaxed);
        const auto start=std::chrono::steady_clock::now();
        while(offset<total) {
            const int n=std::min(sizes[static_cast<std::size_t>(iteration%3)],
                                 total-offset);
            ++iteration;
            for(int id=0;id<kTracks;++id) {
                auto& t=tracks[static_cast<std::size_t>(id)];
                for(int i=0;i<n;++i) {
                    t.phase+=2.0*pi*t.inputHz/rate;
                    // Harmonic, not source-F0 oracle. Stereo has right-only
                    // singer to catch accidental L/R crosstalk.
                    const float x=static_cast<float>(
                        0.24*std::sin(t.phase)+
                        0.050*std::sin(2.0*t.phase+0.21)+
                        0.019*std::sin(3.0*t.phase+0.39));
                    t.left[static_cast<std::size_t>(i)]=t.channels==1?x:0.0f;
                    t.right[static_cast<std::size_t>(i)]=t.channels==2?x:0.0f;
                }
                float* samples[2]={t.left.data(),t.channels==2?t.right.data():nullptr};
                // Watch exactly the real plugin callback, not generators or
                // independent analysis on the non-audio test thread.
                gWatching.store(true,std::memory_order_relaxed);
                t.engine.process(samples,t.channels,n);
                gWatching.store(false,std::memory_order_relaxed);
                for(int i=0;i<n;++i) {
                    const auto l=t.left[static_cast<std::size_t>(i)];
                    const auto r=t.right[static_cast<std::size_t>(i)];
                    t.valid &=std::isfinite(l)&&std::isfinite(r);
                    if(t.channels==2)t.valid &=l==0.0f;
                    const float wet=t.channels==1?l:r;
                    t.peak=std::max(t.peak,std::abs(static_cast<double>(wet)));
                    if(offset+i>=total-static_cast<int>(0.28*rate))
                        t.checkWet.push(wet);
                }
            }
            offset+=n;
        }
        const auto end=std::chrono::steady_clock::now();
        const double processingSeconds=
            std::chrono::duration<double>(end-start).count();
        const double audioSeconds=total/rate;
        const int allocations=gCallbackAllocs.load(std::memory_order_relaxed);
        bool safe=allocations==0;
        for(int id=0;id<kTracks;++id) {
            auto& t=tracks[static_cast<std::size_t>(id)];
            const auto output=t.checkWet.latest();
            if(output.voiced && output.hz>0.0f)
                t.wetCents=1200.0*std::log2(output.hz/t.targetHz);
            const bool high=t.targetHz>=400.0;
            const bool hasSeed=t.engine.firstHighSample()>=0;
            const bool valid=t.valid && t.peak<1.0 &&
               t.engine.researchWetMix()>0.95 &&
               std::abs(t.wetCents)<18.0 &&
               hasSeed==high &&
               (high? t.engine.firstHighSample()<static_cast<int64_t>(0.011*rate)
                    :true);
            safe &=valid;
            std::cout<<"CAUSAL_8_INSERT rate="<<rate<<" insert="<<id
                     <<" channels="<<t.channels
                     <<" input="<<t.inputHz<<" target="<<t.targetHz
                     <<" wet_cents="<<t.wetCents
                     <<" first_high_ms="<<(hasSeed?
                       1000.0*t.engine.firstHighSample()/rate:-1.0)
                     <<" wet="<<t.engine.researchWetMix()
                     <<" peak="<<t.peak<<" valid="<<valid<<"\n";
        }
        std::cout<<"CAUSAL_8_INSERT_SUMMARY sr="<<rate
                 <<" voices="<<kTracks<<" total_audio_s="<<audioSeconds
                 <<" total_wall_s="<<processingSeconds
                 <<" wall_to_audio_ratio="<<processingSeconds/audioSeconds
                 <<" callback_new_calls="<<allocations
                 <<" verdict="<<(safe?"PASS":"FAIL")
                 <<" shared_CI_not_production_CPU_or_ASIO=1\n";
        allGreen &=safe;
    }
    std::cout<<"CAUSAL_8_INSERT_GATE "<<(allGreen?"PASS":"FAIL")
             <<" no_1ms_new_note_claim=1\n";
    return allGreen?0:1;
}
