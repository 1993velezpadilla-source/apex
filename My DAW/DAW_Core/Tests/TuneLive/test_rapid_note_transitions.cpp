// APEX Tune Live HQ40 — singer-grade NOTE TRANSITION regression.
//
// Two independent layers:
//   1. VoiceControl must retarget immediately at a detected distant musical
//      leap, but resist C/D boundary chatter (without inventing portamento).
//   2. Actual production wet DSP must not drop to silence, produce NaN/Inf,
//      leave the previous octave stuck, or create an explosive sample-step
//      during A3 -> A4 -> G3 -> C4 transitions with 1..521 sample callbacks.
// Research/CI only: does NOT alter the approved plugin or synthesize an
// equivalent to a real singing transition / human perception.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {
using namespace APEX::TuneLive;
constexpr int rate=48000;
constexpr int maxPrepared=128;
constexpr int duration=rate*3;
constexpr int stage=duration/4;
constexpr double pi=3.1415926535897932384626433832795;
struct Note {
    double hz;
    double detuneCents;
    int midi;
    const char* name;
};
constexpr std::array<Note,4> song{{
    {220.0,30.0,57,"A3_sharp"},
    {440.0,-25.0,69,"A4_flat"},
    {196.0,-30.0,55,"G3_flat"},
    {261.625565,25.0,60,"C4_sharp"},
}};
PitchTracker::Result perfectNote(float midi) {
    return {440.0f*std::pow(2.0f,(midi-69.0f)/12.0f),0.99f,true};
}
bool testPolicy() {
    VoiceControl policy;
    VoiceSettings s;
    s.root=0;s.scale=1;s.correction=1.0f;s.humanize=0.0f;
    s.flexTune=0.0f;s.noteHysteresis=0.20f;
    bool good=true;
    std::array<int,4> midi{{57,69,55,60}};
    for(const int note:midi) {
        // First tracked frame after a *real* note leap must select the
        // new note, not hold the distant previous target.
        const auto a=policy.update(perfectNote(static_cast<float>(note)+0.18f),s);
        std::cout<<"APEX_NOTE_LEAP_POLICY target="<<note
                 <<" selected="<<a.targetMidi
                 <<" cents_shift="<<a.shiftSemitones*100.0f<<"\\n";
        good=good && a.voiced && a.targetMidi==note &&
             std::abs(a.shiftSemitones)<0.6f;
        for(int i=0;i<20;++i) {
            const float flutter=(i%2==0)?0.05f:-0.05f;
            const auto b=policy.update(
                perfectNote(static_cast<float>(note)+0.18f+flutter),s);
            good=good && b.targetMidi==note;
        }
    }
    policy.reset();
    int flipCount=0, last=-1;
    for(int i=0;i<120;++i) {
        // C-major C4/D4 boundary at MIDI 61. Note oscillates +/-9c;
        // note hysteresis must not toggle scale target every analysis hop.
        const float singer=61.0f+(i%2?0.09f:-0.09f);
        const auto d=policy.update(perfectNote(singer),s);
        if(last!=-1 && d.targetMidi!=last)++flipCount;
        last=d.targetMidi;
    }
    std::cout<<"APEX_NOTE_TRANSITION_HYSTERESIS_FLICKERS="
             <<flipCount<<" total_boundary_updates=120\\n";
    return good && flipCount<=1;
}

struct Render {
    std::array<double,4> cents{{999,999,999,999}};
    std::array<double,4> wetAtEnd{{0,0,0,0}};
    double peak=0;
    double maxAdjacent=0;
    int pdc=0;
    int callbacks=0;
    bool finite=true;
};
Render render(int channels,bool irregular) {
    std::vector<float> originalL(duration),originalR(duration);
    double phase=0;
    for(int i=0;i<duration;++i) {
        const auto& n=song[static_cast<std::size_t>(i/stage)];
        const double t=static_cast<double>(i)/rate;
        const double slowVibrato=6.0*std::sin(2*pi*4.8*t);
        const double frequency=n.hz*std::pow(2.0,
                  (n.detuneCents+slowVibrato)/1200.0);
        phase+=2*pi*frequency/rate;
        const float sample=static_cast<float>(
            0.29*std::sin(phase)+0.06*std::sin(2*phase));
        originalL[static_cast<std::size_t>(i)]=sample;
        originalR[static_cast<std::size_t>(i)]=sample*0.79f;
    }
    auto left=originalL, right=originalR;
    DspCore dsp;
    dsp.prepare(rate,maxPrepared,channels);
    DspSettings user;
    user.correction=1.0f;user.humanize=0.0f;user.flexTune=0.0f;
    user.retune=0.0f;user.stability=0.25f;user.wet=1.0f;
    user.key=0;user.scale=0;
    const std::array<int,5> blocks{{1,7,64,257,521}};
    Render report;
    report.pdc=dsp.latencySamples();
    int position=0,callback=0;
    while(position<duration) {
        const int boundary=std::min(duration,
                        ((position/stage)+1)*stage);
        const int desired=irregular?
             blocks[static_cast<std::size_t>(callback%blocks.size())] : 128;
        const int n=std::min({desired,boundary-position,
                              duration-position});
        float* data[2]={left.data()+position,
                        channels==2?right.data()+position:nullptr};
        dsp.process(data,channels,n,user);
        position+=n;
        ++callback;
        // Inspect the LIVE target on the last callback of each sung note.
        // Measured input must have converged to the new octave / pitch.
        if(position%stage==0) {
            const auto which=static_cast<std::size_t>(position/stage-1);
            const auto expected=song[which].hz*
                std::pow(2.0,song[which].detuneCents/1200.0);
            const double f0=dsp.frequencyHz();
            const double relativeCents=
                f0>0 ? 1200.0*std::log2(f0/expected):9999.0;
            report.wetAtEnd[which]=dsp.debugWetAmount();
            if(!std::isfinite(relativeCents) ||
               std::abs(relativeCents)>85.0)
                report.finite=false;
        }
    }
    report.callbacks=callback;
    // Use the SAME tracker implementation on both output schedules, but
    // start a fresh tracker on each stabilized note to exclude earlier leap
    // windows. This metric checks octaves and gross pitch, not timbre.
    for(std::size_t stageIndex=0;stageIndex<song.size();++stageIndex) {
        PitchTracker tracker;
        tracker.prepare(rate);
        const int begin=static_cast<int>((stageIndex+1)*stage)-rate/4;
        const int end=static_cast<int>((stageIndex+1)*stage);
        for(int i=begin;i<end;++i)
            tracker.push(left[static_cast<std::size_t>(i)]);
        const auto measured=tracker.latest();
        report.cents[stageIndex]=measured.voiced?
             1200.0*std::log2(measured.hz/song[stageIndex].hz) : 9999.0;
    }
    for(int i=0;i<duration;++i) {
        const double leftSample=left[static_cast<std::size_t>(i)];
        const double rightSample=channels==2?
              right[static_cast<std::size_t>(i)]:leftSample;
        report.finite=report.finite && std::isfinite(leftSample) &&
                     std::isfinite(rightSample);
        report.peak=std::max({report.peak,std::abs(leftSample),
                                                std::abs(rightSample)});
        if(i>0) {
            report.maxAdjacent=std::max(report.maxAdjacent,std::abs(
                leftSample-left[static_cast<std::size_t>(i-1)]));
            if(channels==2)
                report.maxAdjacent=std::max(report.maxAdjacent,std::abs(
                    rightSample-right[static_cast<std::size_t>(i-1)]));
        }
    }
    std::cout<<"APEX_REAL_WET_NOTE_LEAP_CALLBACKS"
             <<" channels="<<channels
             <<" scheduling="<<(irregular?"irregular":"fixed128")
             <<" callback_count="<<report.callbacks
             <<" PDC="<<report.pdc
             <<" peak="<<report.peak
             <<" max_adjacent_sample_step="<<report.maxAdjacent<<"\\n";
    for(std::size_t i=0;i<song.size();++i) {
        std::cout<<"APEX_REAL_WET_NOTE_LEAP"
             <<" channels="<<channels
             <<" scheduling="<<(irregular?"irregular":"fixed128")
             <<" note="<<song[i].name
             <<" late_note_cents="<<report.cents[i]
             <<" late_effective_wet="<<report.wetAtEnd[i]<<"\\n";
    }
    return report;
}
bool check(const Render& sample,const Render& reference) {
    if(!sample.finite || sample.pdc!=1920 ||
       sample.peak<0.05 || sample.peak>2.0 ||
       sample.maxAdjacent>0.65)return false;
    for(std::size_t i=0;i<song.size();++i) {
        const double absCents=std::abs(sample.cents[i]);
        const double drift=std::abs(sample.cents[i]-reference.cents[i]);
        std::cout<<"APEX_NOTE_LEAP_CALLBACK_DRIFT"
                 <<" note="<<song[i].name<<" drift_cents="<<drift<<"\\n";
        if(!std::isfinite(absCents)||absCents>65.0 ||
           !std::isfinite(drift)||drift>4.0 ||
           sample.wetAtEnd[i]<0.50)return false;
    }
    return true;
}
}
int main() {
    if(!testPolicy()) {
        std::cerr<<"FAIL VoiceControl octave leaps or note-boundary hysteresis\\n";
        return 1;
    }
    bool ok=true;
    for(int channels=1;channels<=2;++channels) {
        const auto reference=render(channels,false);
        const auto irregular=render(channels,true);
        ok=check(reference,reference)&&check(irregular,reference)&&ok;
    }
    if(!ok) {
        std::cerr<<"FAIL production HQ40 note-leap audio or host scheduling consistency\\n";
        return 1;
    }
    std::cout<<"APEX_TUNE_LIVE_RAPID_NOTE_TRANSITIONS_PASS"
                " C4/D4 hysteresis, A3-A4-G3-C4 real wet leaps,"
                " 1..521 irregular callback consistency, 40ms HQ untouched\\n";
    return 0;
}
