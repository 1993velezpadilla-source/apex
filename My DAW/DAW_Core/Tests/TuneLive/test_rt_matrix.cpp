#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <cstdint>
#include <vector>

// Actual DspCore PDC identity and block bound check.
// This catches stereo clock skew, wrong dry delay, host oversized chunks,
// and sample-rate regressions independent of a live audio interface.
static bool probe(double sampleRate,int preparedBlock,int hostBlock,int channels) {
    constexpr double pi=3.14159265358979323846;
    const int length=static_cast<int>(sampleRate*0.22);
    std::vector<float> left(static_cast<std::size_t>(length)),right(static_cast<std::size_t>(length));
    for(int i=0;i<length;++i) {
        left[static_cast<std::size_t>(i)] =
            static_cast<float>(0.25*std::sin(2*pi*137.0*i/sampleRate)
                                +0.08*std::sin(2*pi*811.0*i/sampleRate));
        right[static_cast<std::size_t>(i)] =
            static_cast<float>(0.18*std::sin(2*pi*221.0*i/sampleRate)
                                +0.11*std::sin(2*pi*511.0*i/sampleRate));
    }
    const auto originalLeft=left,originalRight=right;
    APEX::TuneLive::DspCore dsp;
    dsp.prepare(sampleRate,preparedBlock,channels);
    APEX::TuneLive::DspSettings settings;
    settings.bypass=true;
    for(int start=0;start<length;start+=hostBlock) {
        float* audio[2]={left.data()+start,
                         channels==2?right.data()+start:nullptr};
        dsp.process(audio,channels,std::min(hostBlock,length-start),settings);
    }
    double worst=0.0;
    const int latency=dsp.latencySamples();
    for(int i=latency+128;i<length;++i) {
        const int delayed=i-latency;
        const double l=left[static_cast<std::size_t>(i)]
                        -originalLeft[static_cast<std::size_t>(delayed)];
        worst=std::max(worst,std::abs(l));
        if(channels==2) {
            const double r=right[static_cast<std::size_t>(i)]
                            -originalRight[static_cast<std::size_t>(delayed)];
            worst=std::max(worst,std::abs(r));
        }
    }
    std::cout<<"rate="<<sampleRate<<" prepared="<<preparedBlock
             <<" host="<<hostBlock<<" channels="<<channels
             <<" PDC="<<latency<<" worst_error="<<worst<<"\n";
    return latency>0 && latency<length/2 && worst<1.0e-5;
}
// A vocal recorded on the RIGHT input must still be detected and tuned.
// Prior prototype tracked only source[0], so this scenario silently stayed dry.
static bool rightChannelVocal() {
    constexpr double rate=48000.0;
    constexpr int block=128;
    constexpr int total=48000*2;
    const double frequency=440.0*std::pow(2.0,0.30/12.0);
    std::vector<float> right(total),left(total,0.0f);
    for(int i=0;i<total;++i)
        right[static_cast<std::size_t>(i)]=
            static_cast<float>(0.35*std::sin(6.283185307179586*frequency*i/rate));
    APEX::TuneLive::DspCore dsp;
    dsp.prepare(rate,block,2);
    APEX::TuneLive::DspSettings knobs;
    knobs.correction=1.0f;
    knobs.retune=0.0f;
    knobs.wet=1.0f;
    knobs.humanize=0.0f;
    knobs.flexTune=0.0f;
    knobs.scale=0;
    for(int offset=0;offset<total;offset+=block) {
        float* stereo[2]={left.data()+offset,right.data()+offset};
        dsp.process(stereo,2,std::min(block,total-offset),knobs);
    }
    APEX::TuneLive::PitchTracker out;
    out.prepare(rate);
    for(int i=total-24000;i<total;++i)
        out.push(right[static_cast<std::size_t>(i)]);
    const auto f0=out.latest();
    const double cents=f0.voiced?1200.0*std::log2(f0.hz/440.0):999.0;
    double maxGhost=0.0;
    for(int i=total-24000;i<total;++i)
        maxGhost=std::max(maxGhost,std::abs(static_cast<double>(left[static_cast<std::size_t>(i)])));
    std::cout<<"stereo_right_only corrected_cents="<<cents
             <<" left_channel_leak_peak="<<maxGhost
             <<" tracker_hz="<<dsp.frequencyHz()<<"\\n";
    return f0.voiced && std::abs(cents)<18.0 && maxGhost<1.0e-6
           && dsp.frequencyHz()>440.0f;
}

// Two independent plugin instances must render exactly the same voice
// with the same state and settings, as required for APEX bounce/freeze.
static bool repeatableWetBounce() {
    constexpr double sr=48000.0;
    constexpr int n=48000, block=128;
    std::vector<float> first(n),second(n);
    for(int i=0;i<n;++i) {
        const auto v=static_cast<float>(0.31*std::sin(6.283185307179586*443.5*i/sr)
                     +0.05*std::sin(6.283185307179586*887.0*i/sr));
        first[static_cast<std::size_t>(i)]=second[static_cast<std::size_t>(i)]=v;
    }
    APEX::TuneLive::DspSettings config;
    config.correction=1.0f;config.retune=0.0f;config.humanize=0.0f;
    config.flexTune=0.0f;
    APEX::TuneLive::DspCore one,two;
    one.prepare(sr,block,1);
    two.prepare(sr,block,1);
    double peakDifference=0.0;
    for(int i=0;i<n;i+=block) {
        float* monoOne[1]={first.data()+i};
        float* monoTwo[1]={second.data()+i};
        const int count=std::min(block,n-i);
        one.process(monoOne,1,count,config);
        two.process(monoTwo,1,count,config);
    }
    for(int i=0;i<n;++i)
        peakDifference=std::max(peakDifference,
            std::abs(static_cast<double>(first[static_cast<std::size_t>(i)])
                   -static_cast<double>(second[static_cast<std::size_t>(i)])));
    std::cout<<"repeated_wet_render_difference_peak="<<peakDifference<<"\\n";
    return peakDifference<=1.0e-7;
}

// A broken upstream plugin must not poison the pitch shifter permanently.
// This test also covers NaN automation values from corrupt saved sessions.
static bool poisonedInputRecovery() {
    constexpr int samples=48000;
    constexpr int quantum=128;
    std::vector<float> voice(samples);
    for(int i=0;i<samples;++i)
        voice[static_cast<std::size_t>(i)]=static_cast<float>(
            0.32*std::sin(6.283185307179586*445.0*i/48000.0));
    voice[1024]=std::numeric_limits<float>::quiet_NaN();
    voice[1025]=std::numeric_limits<float>::infinity();
    voice[1026]=-std::numeric_limits<float>::infinity();
    voice[1030]=1.0e31f;
    APEX::TuneLive::DspCore dsp;
    dsp.prepare(48000.0,quantum,1);
    APEX::TuneLive::DspSettings knobs;
    knobs.wet=std::numeric_limits<float>::quiet_NaN();
    knobs.retune=std::numeric_limits<float>::quiet_NaN();
    knobs.correction=std::numeric_limits<float>::infinity();
    knobs.humanize=std::numeric_limits<float>::quiet_NaN();
    knobs.referenceHz=std::numeric_limits<float>::quiet_NaN();
    for(int offset=0;offset<samples;offset+=quantum) {
        float* ptr[1]={voice.data()+offset};
        dsp.process(ptr,1,std::min(quantum,samples-offset),knobs);
        for(int i=0;i<std::min(quantum,samples-offset);++i) {
            if(!std::isfinite(voice[static_cast<std::size_t>(offset+i)])) {
                std::cerr<<"Poisoned wet sample at "<<offset+i<<"\\n";
                return false;
            }
        }
    }
    if(!std::isfinite(dsp.frequencyHz()) || dsp.frequencyHz()<400.0f
       || dsp.frequencyHz()>500.0f) {
        std::cerr<<"Tracker did not recover after poisoned upstream audio\\n";
        return false;
    }
    std::cout<<"PASS: corrupt input + NaN controls contained, tracker recovered\\n";
    return true;
}

// Stable deterministic unvoiced noise must remain natural (no synthetic
// choir, random musical note, or spurious transposition of sibilants).
// Compare against the original noise after reported PDC.
static bool unvoicedSibilanceFidelity() {
    constexpr int sr=48000, n=sr, block=128;
    std::vector<float> noise(n);
    std::uint32_t rng=0xA17EC25u;
    for(int i=0;i<n;++i) {
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        const float centered=static_cast<float>(rng & 0xffffu)/32768.0f-1.0f;
        noise[static_cast<std::size_t>(i)]=0.07f*centered;
    }
    const auto original=noise;
    APEX::TuneLive::DspCore effect;
    effect.prepare(sr,block,1);
    APEX::TuneLive::DspSettings settings;
    settings.correction=1.0f; settings.retune=0.0f;
    settings.flexTune=0.0f; settings.humanize=0.0f;
    for(int i=0;i<n;i+=block) {
        float* audio[1]={noise.data()+i};
        effect.process(audio,1,std::min(block,n-i),settings);
    }
    const int latency=effect.latencySamples();
    double squaredError=0.0,originalEnergy=0.0;
    int count=0;
    for(int i=sr/2;i<n;++i) {
        if(i<latency)continue;
        const float dry=original[static_cast<std::size_t>(i-latency)];
        const double error=noise[static_cast<std::size_t>(i)]-dry;
        squaredError+=error*error;
        originalEnergy+=static_cast<double>(dry)*dry;
        ++count;
    }
    const double relativeRms=std::sqrt(squaredError/std::max(1.0,originalEnergy));
    std::cout<<"sibilance_unvoiced_relative_error="<<relativeRms
             <<" latency="<<latency<<"\\n";
    // Since this signal has no valid fundamental, almost all of the output
    // must remain the original consonant, delay-aligned without coloration.
    return count>0 && std::isfinite(relativeRms) && relativeRms<0.03;
}

// 0% correction MUST be mathematically PDC-aligned dry even when input is
// fully voiced. Spectral shifters color vowels even at factor=1 and the artist
// must not hear a metallic voice merely because an enabled plugin is idle.
static bool pitchNeutralTransparency() {
    constexpr int sr=48000, n=sr, block=128;
    std::vector<float> voice(n);
    for(int i=0;i<n;++i) {
        const double t=static_cast<double>(i)/sr;
        voice[static_cast<std::size_t>(i)]=static_cast<float>(
            0.24*std::sin(6.283185307179586*440.0*t)
           +0.08*std::sin(6.283185307179586*880.0*t)
           +0.035*std::sin(6.283185307179586*1320.0*t));
    }
    const auto original=voice;
    APEX::TuneLive::DspCore core;
    core.prepare(sr,block,1);
    APEX::TuneLive::DspSettings settings;
    settings.correction=0.0f;
    settings.wet=1.0f;
    settings.bypass=false;
    for(int i=0;i<n;i+=block) {
        float* audio[1]={voice.data()+i};
        core.process(audio,1,std::min(block,n-i),settings);
    }
    const int latency=core.latencySamples();
    double largest=0.0;
    for(int i=sr/2;i<n;++i) {
        largest=std::max(largest,std::abs(static_cast<double>(voice[static_cast<std::size_t>(i)])
           -original[static_cast<std::size_t>(i-latency)]));
    }
    std::cout<<"zero_correction_vowel_transparency_max_error="<<largest<<"\\n";
    return latency<sr/2 && largest<1.0e-6
        && core.debugWetAmount()<0.0001f;
}

int main() {
    bool pass=true;
    pass&=probe(44100.0,64,64,1);
    pass&=probe(48000.0,128,128,1);
    pass&=probe(48000.0,128,257,2); // oversized callback must chunk safely
    pass&=probe(48000.0,64,64,2);
    pass&=probe(48000.0,256,256,2);
    pass&=probe(96000.0,128,128,1);
    pass&=probe(96000.0,256,256,2);
    pass&=rightChannelVocal();
    pass&=repeatableWetBounce();
    pass&=poisonedInputRecovery();
    pass&=unvoicedSibilanceFidelity();
    pass&=pitchNeutralTransparency();
    if(!pass) {std::cerr<<"FAIL: bypass PDC/host block matrix\n";return 1;}
    std::cout<<"PASS: cross-platform PDC, mono/stereo, oversized block matrix\n";
}
