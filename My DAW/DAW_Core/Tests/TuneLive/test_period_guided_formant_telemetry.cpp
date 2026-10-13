// Synthetic harmonic vowel spectral-envelope TELEMETRY, not a perceptual
// formant preservation certificate. Uses real YIN, not an F0 oracle.
// The envelope metric is level-normalized and references the same voiced
// fixture pre-correction. No sampled singer or Antares source is used.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveTrackedPrototype.h"
#include "../../Source/TuneLiveCore/ApexTuneLivePitchTracker.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
constexpr int nfft=8192;
struct Vowel {const char* name;double f1,f2;};
constexpr std::array<Vowel,3> vowels{{{"AH",730.0,1090.0},
                                        {"EE",320.0,2300.0},
                                        {"OH",570.0,840.0}}};
double gaussian(double frequency,double centre,double width) {
    const double z=(frequency-centre)/width;
    return std::exp(-0.5*z*z);
}
std::vector<float> generate(double sr,double hz,int frames,const Vowel& vowel) {
    std::vector<float> wave(static_cast<std::size_t>(frames));
    for(int i=0;i<frames;++i) {
        const double phase=2.0*pi*hz*i/sr;
        double signal=0.24*std::sin(phase);
        for(int h=2;h<=12;++h) {
            const double f=hz*h;
            const double envelope=0.12+
                1.4*gaussian(f,vowel.f1,180.0)
                +1.0*gaussian(f,vowel.f2,260.0);
            signal+=(0.13/h)*envelope*std::sin(phase*h+0.13*h);
        }
        wave[static_cast<std::size_t>(i)]=static_cast<float>(signal);
    }
    return wave;
}
double magnitudeAt(const std::vector<float>& wave,double sr,double freq){
    if(wave.size()<nfft)return 0.0;
    const std::size_t offset=wave.size()-nfft;
    double real=0.0,imag=0.0,norm=0.0;
    for(int i=0;i<nfft;++i) {
        const double hann=0.5-0.5*std::cos(2.0*pi*i/(nfft-1));
        const double phase=2.0*pi*freq*i/sr;
        const double sample=wave[offset+static_cast<std::size_t>(i)]*hann;
        real+=sample*std::cos(phase);
        imag+=sample*std::sin(phase);
        norm+=hann;
    }
    return 2.0*std::hypot(real,imag)/norm;
}
struct Envelope {double meanAbsDb=0.0,peakDb=0.0;int measured=0;};
Envelope envelopeError(const std::vector<float>& before,
                       const std::vector<float>& after,double sr,
                       double inputHz,double outputHz) {
    // First-harmonic normalization excludes broadband volume/gain changes
    // and compares relative spectral shape rather than absolute loudness.
    const double refFund=magnitudeAt(before,sr,inputHz);
    const double outFund=magnitudeAt(after,sr,outputHz);
    Envelope metric;
    if(refFund<0.001 || outFund<0.001) {
        metric.meanAbsDb=9999.0;
        return metric;
    }
    double weightedError=0.0,weightSum=0.0;
    for(int h=2;h<=12;++h){
        const double reference=magnitudeAt(before,sr,inputHz*h);
        const double observed=magnitudeAt(after,sr,outputHz*h);
        if(reference<0.001)continue;
        const double err=20.0*std::log10(
           std::max(observed/outFund,1e-12)/
           std::max(reference/refFund,1e-12));
        if(!std::isfinite(err)){metric.meanAbsDb=9999.0;return metric;}
        const double importance=std::min(1.0,reference/0.015);
        weightedError+=importance*std::abs(err);
        weightSum+=importance;
        metric.peakDb=std::max(metric.peakDb,std::abs(err));
        ++metric.measured;
    }
    metric.meanAbsDb=weightSum>0.0?weightedError/weightSum:9999.0;
    return metric;
}
APEX::TuneLive::VoiceSettings hardTune(){
    APEX::TuneLive::VoiceSettings knobs;
    knobs.root=9;knobs.scale=0;knobs.correction=1.0f;
    knobs.humanize=0.0f;knobs.flexTune=0.0f;
    knobs.noteHysteresis=0.10f;
    return knobs;
}
}
int main(){
    using APEX::TuneLive::Research::TrackedPrototype;
    constexpr double sr=48000.0;
    constexpr int frames=48000;
    bool safety=true;
    int cases=0;
    for(const Vowel& vowel:vowels)
      for(double target:std::array<double,2>{220.0,440.0})
        for(double detune:std::array<double,2>{-30.0,30.0}) {
        const double sourceHz=target*std::pow(2.0,detune/1200.0);
        const auto source=generate(sr,sourceHz,frames,vowel);
        std::vector<float> output=source;
        const auto identity=envelopeError(source,source,sr,sourceHz,sourceHz);
        // Validate the metric against an identity case before interpreting
        // its nonzero numbers; avoid a misleading broken diagnostic.
        safety &= identity.measured>=3 && identity.meanAbsDb<0.001;
        TrackedPrototype engine;
        engine.prepare(sr);
        engine.setVoiceSettings(hardTune());
        engine.enableExperimentalEarlyOnset(true);
        for(int start=0;start<frames;start+=128) {
            float* mono[1]={output.data()+start};
            engine.process(mono,1,std::min(128,frames-start));
        }
        double peak=0.0;
        bool finite=true;
        for(float sample:output) {
            finite &= std::isfinite(sample);
            if(std::isfinite(sample))
                peak=std::max(peak,std::abs(static_cast<double>(sample)));
        }
        const auto metric=envelopeError(source,output,sr,sourceHz,target);
        APEX::TuneLive::PitchTracker check;
        check.prepare(sr);
        for(int i=frames-static_cast<int>(sr*0.3);i<frames;++i)
            check.push(output[static_cast<std::size_t>(i)]);
        const auto pitch=check.latest();
        const double cents=pitch.voiced?
             1200.0*std::log2(pitch.hz/target):9999.0;
        const bool safe=finite && peak>0.01 && peak<2.0 &&
             engine.voiced() && engine.estimates()>10 &&
             pitch.voiced && std::abs(cents)<=18.0 &&
             metric.measured>=3 && std::isfinite(metric.meanAbsDb);
        safety &= safe;
        ++cases;
        std::cout<<"FORMANT_RESEARCH vowel="<<vowel.name
                 <<" target="<<target<<" detune_cents="<<detune
                 <<" wet_f0_residual_cents="<<cents
                 <<" spectral_envelope_mean_abs_db="<<metric.meanAbsDb
                 <<" spectral_envelope_peak_db="<<metric.peakDb
                 <<" harmonics_measured="<<metric.measured
                 <<" output_peak="<<peak<<" safety="<<safe<<"\n";
    }
    std::cout<<"FORMANT_RESEARCH safety="<<safety<<" cases="<<cases
             <<" (synthetic vowels, harmonic envelope ONLY; no singer ABX)\n";
    return safety && cases==12?0:1;
}
