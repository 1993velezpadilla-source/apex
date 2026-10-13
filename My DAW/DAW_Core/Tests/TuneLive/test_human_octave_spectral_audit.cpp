// APEX Tune Live — pitched-real-voice spectral cross-check for false
// octave classifications by a standalone YIN measurement.
//
// Our human-singer #2 had 20 *YIN F0 observations* of apparent -12 semitones
// even with HQ40, but a narrowband FFT showed the 415Hz fundamental and its
// 830Hz,1245Hz harmonics. Before changing singer-approved production DSP,
// inspect true acoustic energy at 207Hz (octave BELOW) vs 415Hz at known
// annotated voice passage; don't treat a detector label as audio proof.
//
// Pinned test passage: Vocadito #2, 5.10..5.25sec, solo vocal around G#4.
// This guard is narrow by design: further vocal/register test cases needed.
// Uses the same portable WAV reader as the actual singer audition.
// No JUCE/editor changes, no 3rd-party FFT, no real-time allocations.
#define main offline_audition_main_not_called
#include "tune_live_offline_audition.cpp"
#undef main
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr double pi=3.1415926535897932384626433832795;
constexpr int windowLength=4096;
double sineProjectionMagnitude(const Wave& wave,std::size_t start,double hz) {
    const double coefficient=2.0*std::cos(2*pi*hz/wave.rate);
    double q1=0.0,q2=0.0;
    for(int i=0;i<windowLength;++i) {
        const auto frame=start+static_cast<std::size_t>(i);
        const float sample=wave.data[frame*static_cast<std::size_t>(wave.channels)];
        // Hann leakage rejection: a stronger 415Hz tone must not leak into
        // a false 207Hz overtone, nor vice versa.
        const double hann=0.5*(1.0-std::cos(2.0*pi*i/(windowLength-1.0)));
        const double q0=static_cast<double>(sample)*hann+coefficient*q1-q2;
        q2=q1;
        q1=q0;
    }
    const double energy=q1*q1+q2*q2-coefficient*q1*q2;
    return std::sqrt(std::max(0.0,energy));
}
double maxNarrowband(const Wave& wave,std::size_t start,double low,double high,double step) {
    double maxValue=0.0;
    for(double hz=low;hz<=high+1e-7;hz+=step)
        maxValue=std::max(maxValue,sineProjectionMagnitude(wave,start,hz));
    return maxValue;
}
struct Profile {
    const char* name;
    const char* suffix;
    int latencySamples;
};
}
int main(int argc,char** argv) {
    try {
        if(argc!=3||std::string(argv[1])!="--prefix")
            throw std::runtime_error("Usage: --prefix vocadito_high");
        const std::string prefix=argv[2];
        const auto dry=readWave(prefix+"_dry.wav");
        const std::array<Profile,3> profiles{{
            {"SHIPPING_HQ40","_hq40.wav",1764},
            {"RESEARCH_18MS","_research18.wav",794},
            {"RESEARCH_12MS","_research12.wav",529}
        }};
        if(dry.rate!=44100||dry.channels!=1||dry.frames()<7*dry.rate)
            throw std::runtime_error("Expected 44.1kHz pinned Vocadito high-register excerpt");
        const std::array<double,4> times{{5.10,5.17,5.22,5.25}};
        for(const auto& profile:profiles) {
            const auto wet=readWave(prefix+profile.suffix);
            if(wet.rate!=dry.rate||wet.channels!=dry.channels||
               wet.frames()!=dry.frames())
                throw std::runtime_error("PDC comparison WAV formats don't match");
            double worstSourceRatio=0.0,worstWetRatio=0.0;
            for(double time:times) {
                const auto dryAt=static_cast<std::size_t>(std::lround(time*dry.rate));
                const auto wetAt=dryAt+static_cast<std::size_t>(profile.latencySamples);
                if(dryAt+windowLength>dry.frames()||
                   wetAt+windowLength>wet.frames())
                    throw std::runtime_error("Out-of-bounds spectral audit");
                // The 415Hz real-source note is strongest in this local
                // region. Fundamental power uses a sweep so vibrato and
                // pitch correction do not look like a missing F0.
                const auto sourceFund=maxNarrowband(dry,dryAt,398,432,2.0);
                const auto sourceHalf=maxNarrowband(dry,dryAt,199,216,1.0);
                const auto wetFund=maxNarrowband(wet,wetAt,398,432,2.0);
                const auto wetHalf=maxNarrowband(wet,wetAt,199,216,1.0);
                if(sourceFund<1.0 || wetFund<1.0)
                    throw std::runtime_error("Pitched human segment silently missing");
                const double sourceRatio=sourceHalf/sourceFund;
                const double wetRatio=wetHalf/wetFund;
                worstSourceRatio=std::max(worstSourceRatio,sourceRatio);
                worstWetRatio=std::max(worstWetRatio,wetRatio);
                std::cout<<"HUMAN_HIGH_SPECTRAL_OCTAVE_CHECK"
                         <<" profile="<<profile.name<<" voice_time_s="<<time
                         <<" PDC_samples="<<profile.latencySamples
                         <<" original_half_to_F0="<<sourceRatio
                         <<" wet_half_to_F0="<<wetRatio<<"\n";
                // Any substantial synthesized 207Hz component can be an
                // audible octave-down artifact. This excerpt is a narrowly
                // audited ~415Hz singer. Tolerate up to -14dB amplitude.
                if(sourceRatio>0.2||wetRatio>0.2)
                    throw std::runtime_error("Unexpected octave-down spectral energy in human high-register segment");
            }
            std::cout<<"HUMAN_HIGH_SPECTRAL_OCTAVE_PROFILE_PASS"
                     <<" name="<<profile.name<<" worst_source="<<worstSourceRatio
                     <<" worst_wet="<<worstWetRatio<<"\n";
        }
        std::cout<<"HUMAN_HIGH_SPECTRAL_OCTAVE_AUDIT_PASS"
                    " — YIN octave flags alone are NOT evidence of audible octave down"
                    " on the checked singer segment. Preserve HQ40 until listening.\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"HUMAN_HIGH_SPECTRAL_OCTAVE_AUDIT_FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
