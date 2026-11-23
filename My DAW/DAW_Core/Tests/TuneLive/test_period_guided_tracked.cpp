// Research gate: REAL streaming YIN -> scale decision -> causal prototype.
// Signal synthesis supplies no pitch oracle to the DUT. Accuracy is reported,
// not promoted; this test enforces finite output, actual detector activation,
// correct note selection, stable stereo separation and safe reacquisition.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveTrackedPrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
double autocorrHz(const std::vector<float>& audio,double sr,double expectedHz) {
    const int minLag=std::max(2,static_cast<int>(std::floor(sr/(expectedHz*1.06))));
    const int maxLag=static_cast<int>(std::ceil(sr/(expectedHz*0.94)));
    const int begin=static_cast<int>(audio.size())-8192-maxLag;
    if(begin<0)return 0.0;
    auto corr=[&](int lag){
        double xy=0.0,xx=0.0,yy=0.0;
        for(int j=0;j<8192;++j){
            const double a=audio[static_cast<std::size_t>(begin+j)];
            const double b=audio[static_cast<std::size_t>(begin+j+lag)];
            xy+=a*b;xx+=a*a;yy+=b*b;
        }
        return xy/std::sqrt(std::max(1e-24,xx*yy));
    };
    int best=minLag;
    double score=-2.0;
    for(int lag=minLag;lag<=maxLag;++lag) {
        const double value=corr(lag);
        if(value>score){score=value;best=lag;}
    }
    double lag=best;
    if(best>minLag && best<maxLag) {
        const double l=corr(best-1),m=corr(best),r=corr(best+1);
        const double denom=l-2.0*m+r;
        if(std::abs(denom)>1e-12)
            lag+=std::clamp(0.5*(l-r)/denom,-0.5,0.5);
    }
    return score>0.5?sr/lag:0.0;
}
float harmonic(double sr,double inputHz,int sample) {
    const double t=sample/sr;
    double value=0.27*std::sin(2*pi*inputHz*t);
    for(int h=2;h<=9;++h)
        value+=(0.09/h)*std::sin(2*pi*inputHz*h*t+0.13*h);
    return static_cast<float>(value);
}
APEX::TuneLive::VoiceSettings hardTune(){
    APEX::TuneLive::VoiceSettings knobs;
    knobs.root=9; // A
    knobs.scale=0; // chromatic
    knobs.correction=1.0f;
    knobs.referenceHz=440.0f;
    knobs.humanize=0.0f;
    knobs.flexTune=0.0f;
    knobs.noteHysteresis=0.1f;
    return knobs;
}
}
int main() {
    using APEX::TuneLive::Research::TrackedPrototype;
    bool safe=true;
    int accuracyEligible=0,voicedCases=0;
    for(double sr:std::array<double,2>{44100.0,48000.0})
      for(double target:std::array<double,2>{220.0,440.0})
        for(double cents:std::array<double,2>{-30.0,30.0}){
        const double inputHz=target*std::pow(2.0,cents/1200.0);
        const int count=static_cast<int>(1.3*sr);
        std::vector<float> audio(static_cast<std::size_t>(count));
        for(int i=0;i<count;++i)audio[static_cast<std::size_t>(i)]=harmonic(sr,inputHz,i);
        TrackedPrototype engine;
        engine.prepare(sr);
        engine.setVoiceSettings(hardTune());
        for(int i=0;i<count;i+=128){
            float* mono[1]={audio.data()+i};
            engine.process(mono,1,std::min(128,count-i));
        }
        double peak=0.0;
        for(float value:audio){
            safe &= std::isfinite(value);
            peak=std::max(peak,std::abs(static_cast<double>(value)));
        }
        const double outputHz=autocorrHz(audio,sr,target);
        const double residual=outputHz>0.0?1200.0*std::log2(outputHz/target):9999.0;
        const bool detected=engine.voiced() &&
            std::abs(engine.sourceHz()-inputHz)<inputHz*0.02 &&
            engine.firstVoicedSample()>=0 &&
            engine.firstVoicedSample()<static_cast<int>(sr*0.15) &&
            std::abs(engine.shiftSemitones())>0.18f &&
            engine.estimates()>10;
        safe &= detected && peak>0.005 && peak<2.0;
        voicedCases+=detected;
        const bool accurate=outputHz>0.0 && std::abs(residual)<18.0;
        accuracyEligible+=accurate;
        std::cout<<"TRACKED_RESEARCH sr="<<sr<<" target="<<target
                 <<" detune_cents="<<cents<<" detector_hz="<<engine.sourceHz()
                 <<" first_f0_ms="<<1000.0*engine.firstVoicedSample()/sr
                 <<" source_estimates="<<engine.estimates()
                 <<" applied_target_shift_st="<<engine.shiftSemitones()
                 <<" wet_residual_cents="<<residual
                 <<" acceptable_synthetic_f0="<<accurate<<"\n";
    }
    // A complete, abrupt note change must eventually reacquire the second
    // NOTE with the actual streaming detector and stay finite. This does not
    // certify glitch-free note-transition audio.
    {
        constexpr double sr=48000.0;
        const int frames=96000;
        std::vector<float> audio(static_cast<std::size_t>(frames));
        const double a3=220.0*std::pow(2.0,30.0/1200.0);
        const double a4=440.0*std::pow(2.0,-30.0/1200.0);
        for(int i=0;i<frames;++i){
            const double hz=i<frames/2?a3:a4;
            audio[static_cast<std::size_t>(i)]=harmonic(sr,hz,i);
        }
        TrackedPrototype engine;
        engine.prepare(sr);
        engine.setVoiceSettings(hardTune());
        for(int start=0;start<frames;start+=128){
            float* mono[1]={audio.data()+start};
            engine.process(mono,1,std::min(128,frames-start));
        }
        bool finite=true;
        for(float sample:audio)finite &= std::isfinite(sample);
        const bool reacquired=finite && engine.voiced() &&
                 std::abs(engine.sourceHz()-a4)<a4*0.02 &&
                 std::abs(engine.shiftSemitones())>0.18f;
        safe &= reacquired;
        std::cout<<"TRACKED_NOTE_JUMP reacquired="<<reacquired
                 <<" final_detected_hz="<<engine.sourceHz()<<"\n";
    }
    // Right-mic-only stereo + invalid upstream input + final silence.
    {
        constexpr double sr=48000.0;
        TrackedPrototype engine;
        engine.prepare(sr,2);
        engine.setVoiceSettings(hardTune());
        std::array<float,128> left{},right{};
        float* stereo[2]={left.data(),right.data()};
        bool noLeak=true,finite=true;
        constexpr double hz=440.0*1.017479692;
        for(int k=0;k<650;++k) {
            for(int j=0;j<128;++j){
                left[static_cast<std::size_t>(j)]=0.0f;
                right[static_cast<std::size_t>(j)]=(k<500)
                    ?harmonic(sr,hz,k*128+j):0.0f;
                if(k==6 && j==25)
                    right[static_cast<std::size_t>(j)]=
                        std::numeric_limits<float>::quiet_NaN();
            }
            engine.process(stereo,2,128);
            for(int j=0;j<128;++j){
                finite &= std::isfinite(right[static_cast<std::size_t>(j)]);
                noLeak &= left[static_cast<std::size_t>(j)]==0.0f;
            }
        }
        safe &= finite && noLeak && engine.estimates()>20 && !engine.voiced();
        std::cout<<"TRACKED_STEREO_AND_SILENCE finite="<<finite
                 <<" right_to_left_leak="<<!noLeak
                 <<" voiced_at_end="<<engine.voiced()<<"\n";
    }
    std::cout<<"TRACKED_RESEARCH safety="<<safe<<" detected="<<voicedCases
             <<"/8 synthetic_f0_eligible="<<accuracyEligible
             <<"/8 (not a voice/PDC/formant/Antares claim)\n";
    return safe && voicedCases==8?0:1;
}
