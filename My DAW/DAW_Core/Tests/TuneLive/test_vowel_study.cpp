// Synthetic vowel study on the ACTUAL production wet DSP.
// NOT a substitute for sung voice recordings, level-matched auditions,
// listener ABX tests, or formant/timbre certification.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
struct Vowel {
    const char* name;
    double firstFormant, secondFormant;
};
float voicedVowel(double time, double sourceHz, const Vowel& vowel) {
    // A dominant fundamental plus two harmonic formant clusters approximate
    // the vowels used in speech-resynthesis laboratory smoke tests.
    // Real glottal excitation and transient consonants are far more complex.
    double result=0.25*std::sin(2.0*pi*sourceHz*time);
    for(int harmonic=2;harmonic<=20;++harmonic) {
        const double hz=harmonic*sourceHz;
        const double f1=std::exp(-0.5*std::pow((hz-vowel.firstFormant)/210.0,2.0));
        const double f2=std::exp(-0.5*std::pow((hz-vowel.secondFormant)/300.0,2.0));
        const double amplitude=(0.045+0.20*f1+0.13*f2)/harmonic;
        result+=amplitude*std::sin(2.0*pi*hz*time+0.1*harmonic);
    }
    return static_cast<float>(result);
}
bool explore(const Vowel& vowel,double baseHz,double detuneSemis) {
    constexpr int sampleRate=48000, length=48000*2, quantum=128;
    const double inputHz=baseHz*std::pow(2.0,detuneSemis/12.0);
    std::vector<float> samples(static_cast<std::size_t>(length));
    for(int i=0;i<length;++i)
        samples[static_cast<std::size_t>(i)]
            =voicedVowel(static_cast<double>(i)/sampleRate,inputHz,vowel);

    APEX::TuneLive::DspCore processor;
    processor.prepare(sampleRate,quantum,1);
    APEX::TuneLive::DspSettings controls;
    controls.correction=1.0f;
    controls.retune=0.0f;
    controls.wet=1.0f;
    controls.humanize=0.0f;
    controls.flexTune=0.0f;
    controls.preserveFormants=true;
    for(int offset=0;offset<length;offset+=quantum) {
        float* ptr[1]={samples.data()+offset};
        processor.process(ptr,1,std::min(quantum,length-offset),controls);
    }
    APEX::TuneLive::PitchTracker measured;
    measured.prepare(sampleRate);
    float peak=0.0f;
    bool allFinite=true;
    for(int i=length-24000;i<length;++i) {
        const float v=samples[static_cast<std::size_t>(i)];
        allFinite&=std::isfinite(v);
        peak=std::max(peak,std::abs(v));
        measured.push(std::isfinite(v)?v:0.0f);
    }
    const auto f0=measured.latest();
    const double cents=f0.voiced?1200.0*std::log2(f0.hz/baseHz):9999.0;
    std::cout<<"vowel="<<vowel.name
             <<" targetHz="<<baseHz
             <<" detunedSemis="<<detuneSemis
             <<" outputHz="<<f0.hz
             <<" residualCents="<<cents
             <<" peak="<<peak
             <<" sourceTrackedHz="<<processor.frequencyHz()<<"\n";
    // Hard defects (silent output, NaN, pathological clipping or octave
    // jumps) fail CI. The exact cents spread is only exploratory here.
    return allFinite && peak>0.04f && peak<2.0f && f0.voiced
       && std::abs(cents)<70.0;
}
}
int main() {
    constexpr std::array<Vowel,3> vowels{{
        {"AH",750.0,1250.0},{"EE",310.0,2350.0},{"OH",490.0,950.0}
    }};
    bool passed=true;
    for(const auto& vowel:vowels)
        for(const double base:std::array<double,2>{{220.0,440.0}})
            for(const double detune:std::array<double,2>{{-0.30,0.30}})
                passed&=explore(vowel,base,detune);
    if(!passed) {
        std::cerr<<"FAIL: production wet shifter corrupted a vowel or octave\n";
        return 1;
    }
    std::cout<<"PASS: 12 vowel-rich DSP cases (objective safety, not singer quality)\n";
}
