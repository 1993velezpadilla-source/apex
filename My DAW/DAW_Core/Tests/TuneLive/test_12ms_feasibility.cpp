// EXPERIMENTAL 12-ms research, not a selectable production profile.
// Measure the SAME shipped DspCore at 12 vs 18 ms on vowel-rich input.
// Do not claim better singer sound just because plugin PDC falls.
// A weak pitch result rejects the candidate for release; safety/PDC failures
// fail CI outright. The 40-ms production default must remain unchanged.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
constexpr int block=128;
struct Result {
    int pdc=0;
    double cents=0.0,peak=0.0,correlationCents=9999.0;
    float commandedShift=0.0f,appliedShift=0.0f,wetAmount=0.0f;
    bool voiced=false,finite=true,bypassCorrect=false;
};
// Independent long-window autocorrelation measures the ACTUAL wet signal,
// so a shared YIN detector bias cannot masquerade as pitch correction.
// Constrain the period search to a musical semitone around the known test
// note; this is a synthetic regression oracle, not a real-time detector.
double correlationHz(const std::vector<float>& wave,double sr,double expectedHz) {
    const int minLag=std::max(2,static_cast<int>(std::floor(sr/(expectedHz*1.06))));
    const int maxLag=static_cast<int>(std::ceil(sr/(expectedHz*0.94)));
    const int begin=static_cast<int>(wave.size())-8192-maxLag;
    if(begin<0)return 0.0;
    const int count=8192;
    auto score=[&](int lag) {
        double ab=0.0,aa=0.0,bb=0.0;
        for(int i=0;i<count;++i) {
            const double a=wave[static_cast<std::size_t>(begin+i)];
            const double b=wave[static_cast<std::size_t>(begin+i+lag)];
            ab+=a*b;aa+=a*a;bb+=b*b;
        }
        return ab/std::sqrt(std::max(1.0e-24,aa*bb));
    };
    int best=minLag;
    double peak=-2.0;
    for(int lag=minLag;lag<=maxLag;++lag) {
        const double value=score(lag);
        if(value>peak){peak=value;best=lag;}
    }
    double lag=best;
    if(best>minLag && best<maxLag) {
        const double left=score(best-1),mid=score(best),right=score(best+1);
        const double denom=left-2.0*mid+right;
        if(std::abs(denom)>1.0e-12)
            lag+=std::clamp(0.5*(left-right)/denom,-0.5,0.5);
    }
    return (peak>0.5 && lag>0.0)?sr/lag:0.0;
}
// Use the raw pinned pitch shifter as a control experiment. If this
// under-corrects in a 12ms window despite an explicit transpose ratio,
// changing YIN thresholds or note-selection policy cannot fix it.
// This is offline test code only; it is NOT a new production DSP path.
double rawShifterCents(double sr,double targetHz,double detune,
                       APEX::TuneLive::ShifterProfile profile) {
    using namespace APEX::TuneLive;
    const int frames=static_cast<int>(sr*2.0);
    const double sourceHz=targetHz*std::pow(2.0,detune/12.0);
    std::vector<float> source(static_cast<std::size_t>(frames));
    std::vector<float> output(static_cast<std::size_t>(frames));
    for(int i=0;i<frames;++i) {
        const double time=static_cast<double>(i)/sr;
        double value=0.29*std::sin(2*pi*sourceHz*time);
        for(int h=2;h<=13;++h) {
            const double frequency=sourceHz*h;
            const double envelope=0.025
                +0.20*std::exp(-0.5*std::pow((frequency-750.0)/210.0,2))
                +0.12*std::exp(-0.5*std::pow((frequency-1250.0)/280.0,2));
            value+=envelope/h*std::sin(2*pi*frequency*time+0.13*h);
        }
        source[static_cast<std::size_t>(i)]=static_cast<float>(value);
    }
    const double seconds=profile==ShifterProfile::Experimental12ms?0.012
        :profile==ShifterProfile::Experimental18ms?0.018:0.040;
    const int window=std::max(256,static_cast<int>(std::lround(sr*seconds)));
    const int interval=std::max(64,static_cast<int>(std::lround(window*0.25)));
    signalsmith::stretch::SignalsmithStretch<float> shifter{0x41504558L};
    shifter.configure(1,window,interval,false);
    shifter.reset();
    shifter.setTransposeFactor(static_cast<float>(std::pow(2.0,-detune/12.0)));
    shifter.setFormantFactor(1.0f,true);
    shifter.setFormantBase(static_cast<float>(sourceHz/sr));
    for(int start=0;start<frames;start+=block) {
        const int count=std::min(block,frames-start);
        const float* in[1]={source.data()+start};
        float* out[1]={output.data()+start};
        shifter.process(in,count,out,count);
    }
    const double wetHz=correlationHz(output,sr,targetHz);
    return wetHz>0.0?1200.0*std::log2(wetHz/targetHz):9999.0;
}
Result probe(double sr,double targetHz,double detune,
             APEX::TuneLive::ShifterProfile profile,
             float referenceHz=440.0f) {
    using namespace APEX::TuneLive;
    const int frames=static_cast<int>(sr*2.0);
    const double hz=targetHz*std::pow(2.0,detune/12.0);
    std::vector<float> input(static_cast<std::size_t>(frames));
    for(int i=0;i<frames;++i) {
        const double time=i/sr;
        double v=0.29*std::sin(2*pi*hz*time);
        // Harmonics approximate the onset of voiced formant contours,
        // not a complete real glottal/vowel acoustics model.
        for(int h=2;h<=13;++h) {
            const double fh=hz*h;
            const double envelope=0.025
                +0.20*std::exp(-0.5*std::pow((fh-750.0)/210.0,2))
                +0.12*std::exp(-0.5*std::pow((fh-1250.0)/280.0,2));
            v+=envelope/h*std::sin(2*pi*fh*time+0.13*h);
        }
        input[static_cast<std::size_t>(i)]=static_cast<float>(v);
    }
    DspCore dsp;
    dsp.prepare(sr,block,1,profile);
    DspSettings cfg;
    cfg.referenceHz=referenceHz;
    // Restrict the musical targets to A, not A# when the concert-pitch
    // sweep makes an originally +30-cent note >50c above shifted A.
    cfg.key=9;   // A
    cfg.scale=1; // A major, nearest allowed note here is always A
    cfg.correction=1.0f;
    cfg.retune=0.0f;
    cfg.humanize=0.0f;
    cfg.flexTune=0.0f;
    cfg.wet=1.0f;
    for(int start=0;start<frames;start+=block) {
        float* channels[1]={input.data()+start};
        dsp.process(channels,1,std::min(block,frames-start),cfg);
    }
    PitchTracker tracker;
    tracker.prepare(sr);
    Result result;
    result.pdc=dsp.latencySamples();
    result.commandedShift=dsp.debugTargetShiftSemitones();
    result.appliedShift=dsp.debugAppliedShiftSemitones();
    result.wetAmount=dsp.debugWetAmount();
    const double independentHz=correlationHz(input,sr,targetHz);
    if(independentHz>0.0)
        result.correlationCents=1200.0*std::log2(independentHz/targetHz);
    for(int i=frames-static_cast<int>(sr*0.5);i<frames;++i) {
        const float v=input[static_cast<std::size_t>(i)];
        result.finite&=std::isfinite(v);
        result.peak=std::max(result.peak,std::abs(static_cast<double>(v)));
        tracker.push(std::isfinite(v)?v:0.0f);
    }
    const auto note=tracker.latest();
    result.voiced=note.voiced;
    result.cents=note.voiced ? 1200.0*std::log2(note.hz/targetHz):9999.0;
    // Transparent bypass must reproduce any source exactly at sample PDC.
    dsp.reset();
    cfg.bypass=true;
    for(int i=0;i<frames;++i)
        input[static_cast<std::size_t>(i)]=
            static_cast<float>(0.23*std::sin(2*pi*hz*i/sr));
    for(int i=0;i<frames;i+=block) {
        float* channels[1]={input.data()+i};
        dsp.process(channels,1,std::min(block,frames-i),cfg);
    }
    double maxErr=0;
    for(int i=frames-static_cast<int>(sr*0.25);i<frames;++i) {
        const double expected=0.23*std::sin(2*pi*hz*(i-result.pdc)/sr);
        maxErr=std::max(maxErr,std::abs(input[static_cast<std::size_t>(i)]-expected));
    }
    result.bypassCorrect=maxErr<1.0e-6;
    return result;
}
}
int main() {
    using namespace APEX::TuneLive;
    DspCore shipping;
    shipping.prepare(48000.0,block,1);
    if(shipping.profile()!=ShifterProfile::HighQuality40ms
       ||shipping.latencySamples()!=1920) {
        std::cerr<<"FAIL: experimental study changed default shipping latency\n";
        return 1;
    }
    int safe=0,acceptable12ms=0,total=0;
    for(const double sr:std::array<double,2>{{44100.0,48000.0}})
      for(const double target:std::array<double,2>{{220.0,440.0}})
       for(const double detune:std::array<double,2>{{-0.30,0.30}}) {
        const auto faster=probe(sr,target,detune,ShifterProfile::Experimental12ms);
        const auto baseline=probe(sr,target,detune,ShifterProfile::Experimental18ms);
        const int expected12=static_cast<int>(std::lround(sr*0.012));
        const int expected18=static_cast<int>(std::lround(sr*0.018));
        const bool valid=faster.finite && baseline.finite
            && faster.peak>0.005 && baseline.peak>0.005
            && faster.peak<2.0 && baseline.peak<2.0
            && faster.bypassCorrect && baseline.bypassCorrect
            && std::abs(faster.pdc-expected12)<=4
            && std::abs(baseline.pdc-expected18)<=4;
        if(!valid) {
            std::cerr<<"FAIL: 12/18ms candidate nonfinite, silent, clipped or false PDC\n";
            return 1;
        }
        ++safe;
        const bool eligible=faster.voiced && std::abs(faster.cents)<=18.0
          && std::abs(faster.cents)<std::abs(detune*100.0);
        if(eligible)++acceptable12ms;
        ++total;
        std::cout<<"12ms_vs_18ms sr="<<sr<<" target_hz="<<target
                 <<" detune_cents="<<detune*100.0
                 <<" experimental12_pdc="<<faster.pdc
                 <<" experimental18_pdc="<<baseline.pdc
                 <<" experimental12_residual_cents="<<faster.cents
                 <<" experimental12_autocorr_cents="<<faster.correlationCents
                 <<" experimental18_residual_cents="<<baseline.cents
                 <<" experimental18_autocorr_cents="<<baseline.correlationCents
                 <<" actual_target_shift_st="<<faster.commandedShift
                 <<" applied_shift_st="<<faster.appliedShift
                 <<" actual_wet="<<faster.wetAmount
                 <<" voiced12="<<faster.voiced
                 <<" release_accuracy_candidate="<<eligible<<"\n";
    }
    // Compare independent output measurements against the same short
    // window with a fixed, ideal command bypassing the policy and tracker.
    // Do not assume that the short-window spectral backend obeys a
    // transpose request merely because its parameter reads correctly.
    for(const double target:std::array<double,2>{{220.0,440.0}})
        for(const double detune:std::array<double,2>{{-0.30,0.30}}) {
            const double direct12=rawShifterCents(48000.0,target,detune,
                                      ShifterProfile::Experimental12ms);
            const double direct40=rawShifterCents(48000.0,target,detune,
                                      ShifterProfile::HighQuality40ms);
            std::cout<<"raw_shifter_control target_hz="<<target
                     <<" source_detune_cents="<<detune*100.0
                     <<" direct12_residual_cents="<<direct12
                     <<" direct40_residual_cents="<<direct40<<"\\n";
        }
    // Research-only control-response sweep. This measures whether the
    // short-window backend genuinely follows *more negative* requests,
    // rather than blindly applying a guessed gain to the artist's pitch.
    // Reference frequency is changed only in these offline probes; the
    // hosted APEX plugin remains at its selected concert-pitch reference.
    for(const double target:std::array<double,2>{{220.0,440.0}})
        for(const float ref:std::array<float,5>{{430.0f,435.0f,440.0f,445.0f,450.0f}}) {
            const auto r=probe(48000.0,target,0.30,
                               ShifterProfile::Experimental12ms,ref);
            if(!r.finite || !r.bypassCorrect || r.peak>2.0 || r.peak<0.005) {
                std::cerr<<"FAIL: control response candidate violated safety/PDC\\n";
                return 1;
            }
            const double desiredCents=1200.0*std::log2(ref/440.0);
            std::cout<<"12ms_control_sweep target="<<target
                     <<" reference_A4="<<ref
                     <<" desired_offset_cents="<<desiredCents
                     <<" command_st="<<r.commandedShift
                     <<" applied_st="<<r.appliedShift
                     <<" wet="<<r.wetAmount
                     <<" yin_output_cents="<<r.cents
                     <<" autocorr_output_cents="<<r.correlationCents<<"\\n";
        }
    std::cout<<"12ms_research_safety_pass="<<safe
             <<" sound_accuracy_eligible="<<acceptable12ms<<"/"<<total
             <<" (f0 only, not vowel/formant certification)\n";
    std::cout<<"PASS: 12-ms feasibility and exact PDC; "
                "EXPERIMENTAL ONLY, NOT RELEASE-READY\n";
}
