// RESEARCH: objective A3 vs A4 tuning residual, independently of the
// 512-sample YIN detector used by APEX Tune Live itself.
//
// The singer-approved HQ40 uses the SAME audio engine here. A previous
// regression measured 20c remaining at A3 vs 14c at A4 for a +30c input.
// That might be actual residual shift, or a pitch-tracker bias. Compare a
// frequency-scanned Hann-windowed harmonic with the YIN result and the
// processor's desired/applied semitone shift, before changing production
// tuning or falsifying thresholds.
//
// Not a listening, formant, microphone roundtrip, or subjective quality test.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
constexpr int sampleRate=48000,frames=sampleRate*2,block=128,nfft=8192;
constexpr double pi=3.14159265358979323846;
double narrowband(const std::vector<float>& samples,int start,double hz) {
    const double coefficient=2.0*std::cos(2*pi*hz/sampleRate);
    double q1=0.0,q2=0.0;
    for(int i=0;i<nfft;++i) {
        const double hann=0.5*(1.0-std::cos(2*pi*i/(nfft-1.0)));
        const double q0=samples[static_cast<std::size_t>(start+i)]*hann
                        +coefficient*q1-q2;
        q2=q1;q1=q0;
    }
    return std::sqrt(std::max(0.0,q1*q1+q2*q2-coefficient*q1*q2));
}
struct Scan { double cents=0.0; double amplitude=0.0; };
Scan fundamental(const std::vector<float>& signal,double expectedHz) {
    Scan output;
    const int begin=static_cast<int>(signal.size())-nfft-512;
    for(int cents=-70;cents<=70;++cents) {
        const double frequency=expectedHz*std::pow(2.0,cents/1200.0);
        const double amp=narrowband(signal,begin,frequency);
        if(amp>output.amplitude) {
            output.amplitude=amp;
            output.cents=static_cast<double>(cents);
        }
    }
    return output;
}
bool probe(double targetHz,double inputCents) {
    using namespace APEX::TuneLive;
    std::vector<float> dry(frames),wet(frames);
    const double inputHz=targetHz*std::pow(2.0,inputCents/1200.0);
    double phase=0.0;
    for(int i=0;i<frames;++i) {
        phase+=2*pi*inputHz/sampleRate;
        dry[static_cast<std::size_t>(i)]=static_cast<float>(
              0.28*std::sin(phase)+0.06*std::sin(2*phase));
    }
    wet=dry;
    DspCore dsp;
    dsp.prepare(sampleRate,block,1);
    DspSettings s;
    s.correction=1.0f;s.retune=0.0f;
    s.humanize=0.0f;s.flexTune=0.0f;
    s.stability=0.20f;s.wet=1.0f;
    for(int start=0;start<frames;start+=block) {
        float* audio[1]={wet.data()+start};
        dsp.process(audio,1,std::min(block,frames-start),s);
    }
    PitchTracker tracker;
    tracker.prepare(sampleRate);
    for(int i=frames-sampleRate/3;i<frames;++i)
        tracker.push(wet[static_cast<std::size_t>(i)]);
    const auto measured=tracker.latest();
    const double yinCents=measured.voiced
          ?1200.0*std::log2(measured.hz/targetHz) : 9999.0;
    const Scan source=fundamental(dry,targetHz);
    const Scan corrected=fundamental(wet,targetHz);
    double maxPeak=0.0;
    for(float sample:wet) {
        if(!std::isfinite(sample))return false;
        maxPeak=std::max(maxPeak,std::abs(static_cast<double>(sample)));
    }
    std::cout<<"APEX_TUNE_LOW_REGISTER_SPECTRAL_DIAGNOSTIC"
             <<" target_hz="<<targetHz
             <<" original_cents="<<inputCents
             <<" source_spectral_cents="<<source.cents
             <<" corrected_spectral_cents="<<corrected.cents
             <<" corrected_yin_cents="<<yinCents
             <<" desired_shift_semitones="<<dsp.debugTargetShiftSemitones()
             <<" smooth_shift_semitones="<<dsp.debugAppliedShiftSemitones()
             <<" effective_wet="<<dsp.debugWetAmount()
             <<" latency_samples="<<dsp.latencySamples()
             <<" output_peak="<<maxPeak<<"\\n";
    // The independent spectral tone must identify the input and give a
    // usable, non-silent reading. Deviations are REPORTED, not silently
    // 'corrected' in a production DSP version already approved by singer.
    return measured.voiced && std::isfinite(yinCents)
        && std::abs(source.cents-inputCents)<=3.0
        && corrected.amplitude>1.0 && source.amplitude>1.0
        && maxPeak>0.01 && maxPeak<2.0
        && dsp.latencySamples()==1920;
}
}
int main() {
    bool ok=true;
    for(double hz:std::array<double,3>{{110.0,220.0,440.0}})
        for(double cents:std::array<double,2>{{-30.0,30.0}})
            ok=probe(hz,cents) && ok;
    if(!ok) {
        std::cerr<<"FAIL low register diagnostic fixture or spectral estimate\\n";
        return 1;
    }
    std::cout<<"APEX_TUNE_LOW_REGISTER_SPECTRAL_DIAGNOSTIC_PASS"
                " (six actual 40ms processed signals; descriptive only)\\n";
    return 0;
}
