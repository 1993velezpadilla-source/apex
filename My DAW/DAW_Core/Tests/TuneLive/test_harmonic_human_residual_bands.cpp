// Research-only: high-frequency vocal articulation safeguard for the
// causal harmonic residual-preserving LIVE candidate. Uses ACTUAL LICENSED
// Vocadito human-singer PCM recordings, not synthetic tones or silent UI.
// It checks only normalized high-pass energy bands, NOT ABX preference,
// vowel identity, intonation quality, or Apollo Solo device latency.
#define main offline_audition_main_not_called
#include "tune_live_offline_audition.cpp"
#undef main
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct Energy {double rms=0.0,high2k=0.0,high6k=0.0;};
Energy measure(const Wave& file) {
    if(file.frames()<file.rate || file.channels<1 || file.channels>2)
        throw std::runtime_error("empty/malformed real-vocal recording");
    const double two=1.0-std::exp(-2.0*3.14159265358979323846*2000.0/file.rate);
    const double six=1.0-std::exp(-2.0*3.14159265358979323846*6000.0/file.rate);
    std::array<double,2> low2{},low6{};
    double total=0.0,above2=0.0,above6=0.0;
    for(std::size_t sample=0;sample<file.frames();++sample)
        for(int ch=0;ch<file.channels;++ch) {
            const double x=file.data[sample*static_cast<std::size_t>(file.channels)
                                            +static_cast<std::size_t>(ch)];
            if(!std::isfinite(x))
                throw std::runtime_error("nonfinite real-singer signal");
            low2[static_cast<std::size_t>(ch)]+=two*(x-low2[static_cast<std::size_t>(ch)]);
            low6[static_cast<std::size_t>(ch)]+=six*(x-low6[static_cast<std::size_t>(ch)]);
            const double high2=x-low2[static_cast<std::size_t>(ch)];
            const double high6=x-low6[static_cast<std::size_t>(ch)];
            above2+=high2*high2;
            above6+=high6*high6;
            total+=x*x;
        }
    if(total<1.0e-7)throw std::runtime_error("silent human voice waveform");
    Energy result;
    result.rms=std::sqrt(total/(file.frames()*file.channels));
    result.high2k=above2/total;
    result.high6k=above6/total;
    return result;
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3 || std::string(argv[1])!="--prefix")
            throw std::runtime_error("Usage: --prefix vocadito_human|vocadito_low|vocadito_high");
        const std::string prefix=argv[2];
        const Wave reference=readWave(prefix+"_source.wav");
        const Wave candidate=readWave(prefix+"_harmonic_live_residual_research.wav");
        const Wave noResidual=readWave(prefix+"_harmonic_live_research.wav");
        const Wave fastCandidate=readWave(prefix+"_harmonic_live_fast_research.wav");
        if(reference.frames()!=candidate.frames() ||
           reference.frames()!=noResidual.frames() ||
           reference.rate!=candidate.rate || reference.rate!=noResidual.rate ||
           reference.channels!=candidate.channels ||
           reference.channels!=noResidual.channels ||
           reference.frames()!=fastCandidate.frames() ||
           reference.rate!=fastCandidate.rate ||
           reference.channels!=fastCandidate.channels)
            throw std::runtime_error("Candidate and source have unequal PCM length/rate/channels");
        const auto original=measure(reference);
        const auto residual=measure(candidate);
        const auto harmonicOnly=measure(noResidual);
        const auto fastEnergy=measure(fastCandidate);
        const double ratio2=residual.high2k/original.high2k;
        const double ratio6=residual.high6k/original.high6k;
        const double loudness=residual.rms/original.rms;
        // Quantitative guard only: 0.75–1.25 tolerance for two broad
        // high-frequency fractions, preserving natural sibilant energy.
        // A clicky, metallic, or otherwise bad output can still pass.
        const bool passed=ratio2>=0.75 && ratio2<=1.25 &&
                          ratio6>=0.75 && ratio6<=1.25 &&
                          loudness>=0.55 && loudness<=1.6;
        const double fastRatio2=fastEnergy.high2k/original.high2k;
        const double fastRatio6=fastEnergy.high6k/original.high6k;
        const double fastLoudness=fastEnergy.rms/original.rms;
        // Keep a conservative objective signal-integrity safety bound.
        // These filters alone CANNOT prove formants or subjective quality.
        const bool fastBands=fastRatio2>=0.65 && fastRatio2<=1.40 &&
                             fastRatio6>=0.65 && fastRatio6<=1.40 &&
                             fastLoudness>=0.50 && fastLoudness<=1.75;
        double deltaRms=0.0,maxFastStep=0.0,fastPeak=0.0;
        for(std::size_t sample=0;sample<fastCandidate.data.size();++sample) {
            const double fast=fastCandidate.data[sample];
            const double slow=candidate.data[sample];
            const double difference=fast-slow;
            deltaRms+=difference*difference;
            fastPeak=std::max(fastPeak,std::abs(fast));
            if(sample>=static_cast<std::size_t>(fastCandidate.channels)) {
                const auto previous=sample-static_cast<std::size_t>(fastCandidate.channels);
                maxFastStep=std::max(maxFastStep,std::abs(
                                  fast-fastCandidate.data[previous]));
            }
        }
        deltaRms=std::sqrt(deltaRms/fastCandidate.data.size());
        const bool fastSafety=std::isfinite(fastPeak) && fastPeak<1.0 &&
                              maxFastStep<0.98 && deltaRms>1.0e-6;
        const bool allPassed=passed && fastBands && fastSafety;
        std::cout<<std::fixed<<std::setprecision(5)
                 <<"VOCADITO_CAUSAL_RESIDUAL_BANDS source="<<prefix
                 <<" sr="<<reference.rate
                 <<" original_high2k="<<original.high2k
                 <<" original_high6k="<<original.high6k
                 <<" residual_high2k="<<residual.high2k
                 <<" residual_high6k="<<residual.high6k
                 <<" no_residual_high2k="<<harmonicOnly.high2k
                 <<" no_residual_high6k="<<harmonicOnly.high6k
                 <<" source_residual_rms_ratio="<<loudness
                 <<" normalized_high2k_ratio="<<ratio2
                 <<" normalized_high6k_ratio="<<ratio6
                 <<" fast_high2k_ratio="<<fastRatio2
                 <<" fast_high6k_ratio="<<fastRatio6
                 <<" fast_source_rms_ratio="<<fastLoudness
                 <<" fast_peak="<<fastPeak
                 <<" fast_max_adjacent_step="<<maxFastStep
                 <<" fast_minus_slow_rms="<<deltaRms
                 <<" fast_signal_safety="<<fastSafety
                 <<" accepted="<<allPassed
                 <<" (NOT pitch fidelity / <1ms singer response / ABX)\n";
        return allPassed?0:1;
    } catch(const std::exception& e) {
        std::cerr<<"VOCADITO_CAUSAL_RESIDUAL_BANDS_ERROR "<<e.what()<<"\n";
        return 1;
    }
}
