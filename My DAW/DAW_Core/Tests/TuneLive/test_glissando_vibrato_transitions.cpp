// APEX Tune Live: real shipping HQ40 glissando + vibrato transition gauntlet.
// Production DspCore/VoiceControl/GUI/settings are NOT changed.
// Singing-like synthetic signal with continuous phase:
//   A3 hold -> 0.6s A3/A4 rising glide -> A4 hold ->
//   0.6s A4/G3 falling glide -> G3 hold -> 0.6s G3/C4 rising glide.
// Each stage contains independent 4.8-Hz, +/- 14-cent natural vibrato.
// Compare identical source rendered with normal and irregular host callbacks,
// including callbacks larger than the prepared 128 samples.
// Detect lost vocals, octave-size scheduling drift, NaNs/Inf, severe clicks,
// and untracked final notes. This is NOT an audible quality/ASIO certificate.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {
constexpr int rate=48000,prepared=128;
constexpr int stage=rate*3/5, stages=6, total=stage*stages;
constexpr double pi=3.1415926535897932384626433832795;
constexpr std::array<double,3> notes{{220.0,440.0,196.0}};
struct Result {
    std::vector<float> samples;
    std::array<double,3> stableCents{{9999,9999,9999}};
    double peak=0, maxSampleStep=0;
    int callbacks=0, pdc=0;
    bool finite=true;
};
double actualFrequency(int sample) {
    const int segment=sample/stage;
    const double fraction=static_cast<double>(sample%stage)/stage;
    const double a3=220.0*std::pow(2.0,30.0/1200.0);
    const double a4=440.0*std::pow(2.0,-25.0/1200.0);
    const double g3=196.0*std::pow(2.0,-20.0/1200.0);
    double base=a3;
    switch(segment) {
        case 0: base=a3;break;
        case 1: base=a3*std::pow(a4/a3,fraction);break;
        case 2: base=a4;break;
        case 3: base=a4*std::pow(g3/a4,fraction);break;
        case 4: base=g3;break;
        default: base=g3*std::pow(261.625565/g3,fraction);break;
    }
    const double time=static_cast<double>(sample)/rate;
    return base*std::pow(2.0,
          (14.0*std::sin(2*pi*4.8*time))/1200.0);
}
std::vector<float> source() {
    std::vector<float> voice(total);
    double phase=0;
    for(int i=0;i<total;++i) {
        phase+=2*pi*actualFrequency(i)/rate;
        voice[static_cast<std::size_t>(i)]=static_cast<float>(
            0.28*std::sin(phase)+0.055*std::sin(2*phase));
    }
    return voice;
}
Result render(const std::vector<float>& input,int channels,bool irregular) {
    Result out;
    out.samples=input;
    std::vector<float> right(total);
    if(channels==2)
        for(int i=0;i<total;++i)
            right[static_cast<std::size_t>(i)]=0.83f*input[static_cast<std::size_t>(i)];
    APEX::TuneLive::DspCore dsp;
    dsp.prepare(rate,prepared,channels);
    APEX::TuneLive::DspSettings settings;
    settings.correction=1.0f;
    settings.wet=1.0f;
    settings.retune=0.0f;
    settings.humanize=0.35f;
    settings.flexTune=0.0f;
    settings.stability=0.30f;
    settings.preserveFormants=true;
    const std::array<int,6> chunks{{1,7,17,64,257,521}};
    for(int offset=0;offset<total;) {
        const int desired=irregular?chunks[
            static_cast<std::size_t>(out.callbacks)%chunks.size()]:128;
        const int n=std::min(desired,total-offset);
        float* audio[2]={out.samples.data()+offset,
                          channels==2?right.data()+offset:nullptr};
        dsp.process(audio,channels,n,settings);
        offset+=n;
        ++out.callbacks;
    }
    out.pdc=dsp.latencySamples();
    for(int i=0;i<total;++i) {
        const double a=out.samples[static_cast<std::size_t>(i)];
        const double b=channels==2?right[static_cast<std::size_t>(i)]:a;
        out.finite=out.finite && std::isfinite(a) && std::isfinite(b);
        out.peak=std::max({out.peak,std::abs(a),std::abs(b)});
        if(i>0) {
            out.maxSampleStep=std::max(out.maxSampleStep,std::abs(
              a-out.samples[static_cast<std::size_t>(i-1)]));
            if(channels==2)
                out.maxSampleStep=std::max(out.maxSampleStep,std::abs(
                  b-right[static_cast<std::size_t>(i-1)]));
        }
    }
    // Re-initialize an independent F0 observer on each steady-note ending.
    // Inspect the last 200 ms only; rapid glides are analyzed separately.
    for(int k=0;k<3;++k) {
        APEX::TuneLive::PitchTracker tracker;
        tracker.prepare(rate);
        const int end=(2*k+1)*stage;
        const int start=end-rate/5;
        for(int i=start;i<end;++i)
            tracker.push(out.samples[static_cast<std::size_t>(i)]);
        const auto p=tracker.latest();
        out.stableCents[static_cast<std::size_t>(k)]=p.voiced
          ?1200.0*std::log2(p.hz/notes[static_cast<std::size_t>(k)])
          :9999.0;
    }
    std::cout<<"TUNE_LIVE_GLISSANDO_HQ40_RUN channels="<<channels
             <<" partition="<<(irregular?"irregular":"fixed128")
             <<" callbacks="<<out.callbacks<<" PDC="<<out.pdc
             <<" peak="<<out.peak
             <<" max_adjacent_step="<<out.maxSampleStep<<"\\n";
    for(int k=0;k<3;++k)
        std::cout<<"TUNE_LIVE_GLISSANDO_STABLE_NOTE"
                 <<" channels="<<channels
                 <<" partition="<<(irregular?"irregular":"fixed128")
                 <<" note="<<(k==0?"A3":k==1?"A4":"G3")
                 <<" cents="<<out.stableCents[static_cast<std::size_t>(k)]
                 <<"\\n";
    return out;
}
bool compare(const Result& reference,const Result& candidate,int channels) {
    if(!reference.finite||!candidate.finite||
       reference.pdc!=1920||candidate.pdc!=1920||
       candidate.peak<0.05||candidate.peak>1.5||
       candidate.maxSampleStep>0.30)return false;
    bool good=true;
    for(int k=0;k<3;++k) {
        const auto i=static_cast<std::size_t>(k);
        const double drift=std::abs(reference.stableCents[i]-candidate.stableCents[i]);
        std::cout<<"TUNE_LIVE_GLISSANDO_ENDNOTE_DRIFT"
                 <<" channels="<<channels<<" note_index="<<k
                 <<" cents="<<drift<<"\\n";
        good=good && std::isfinite(drift)&&drift<5.0
                  && std::isfinite(candidate.stableCents[i])
                  && std::abs(candidate.stableCents[i])<110.0;
    }
    // Compare independent YIN observer at the SAME OUTPUT SAMPLE TIMES
    // for both host partitions. Pitch differs naturally over each glide,
    // so do not compare to a fixed note. The schedules should agree.
    APEX::TuneLive::PitchTracker fixed,variable;
    fixed.prepare(rate);variable.prepare(rate);
    int glidePaired=0,close=0,octaveDrift=0;
    for(int i=0;i<total;++i) {
        const bool update=fixed.push(reference.samples[static_cast<std::size_t>(i)]);
        variable.push(candidate.samples[static_cast<std::size_t>(i)]);
        if(!update)continue;
        const int segment=i/stage;
        if(segment!=1&&segment!=3&&segment!=5)continue;
        if(i%stage<rate/8)continue; // detector startup at each leap
        const auto a=fixed.latest(),b=variable.latest();
        if(!a.voiced||!b.voiced||a.hz<=0||b.hz<=0)continue;
        ++glidePaired;
        const double cents=std::abs(1200.0*std::log2(a.hz/b.hz));
        if(cents<150.0)++close;
        if(cents>650.0)++octaveDrift;
    }
    const double agreement=glidePaired>0 ?
         static_cast<double>(close)/glidePaired:0.0;
    std::cout<<"TUNE_LIVE_GLISSANDO_HOST_PARTITION_AGREEMENT"
             <<" channels="<<channels
             <<" voiced_pairs="<<glidePaired
             <<" within_150_cents_ratio="<<agreement
             <<" octave_size_disagreements="<<octaveDrift<<"\\n";
    return good && glidePaired>=100 && agreement>0.80 && octaveDrift<4;
}
}
int main() {
    const auto voice=source();
    bool passed=true;
    for(int channels=1;channels<=2;++channels) {
        const auto fixed=render(voice,channels,false);
        const auto irregular=render(voice,channels,true);
        passed=compare(fixed,irregular,channels)&&passed;
    }
    if(!passed) {
        std::cerr<<"FAIL: wet HQ40 glissando or note transition depends on callback partition\\n";
        return 1;
    }
    std::cout<<"TUNE_LIVE_GLISSANDO_VIBRATO_PASS actual 40ms production DSP,"
                " 3 glides + 3 steady notes, live wet mono/stereo, irregular"
                " 1..521 sample host callbacks; GUI/audio policy unchanged\\n";
    return 0;
}
