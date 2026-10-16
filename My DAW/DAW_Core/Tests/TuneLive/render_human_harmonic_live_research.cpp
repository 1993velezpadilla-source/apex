// Nonshipping human-singer listening reference for the near-zero-lookahead
// harmonic prediction hypothesis. Reuse the existing, bounded WAV reader.
// No private microphone takes, no host/UI modifications, no <=1ms claim.
#define main offline_audition_main_not_called
#include "tune_live_offline_audition.cpp"
#undef main
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
using APEX::TuneLive::Research::HarmonicLivePrototype;
constexpr int blockSize=128;

Wave renderLive(const Wave& input, bool early, bool residual,
                bool fastEngagement=false, bool fastTransitions=false,
                bool highRegister=false, bool midRegister=false,
                bool ultraHigh=false) {
    Wave out=input;
    HarmonicLivePrototype engine;
    engine.prepare(input.rate,input.channels);
    APEX::TuneLive::VoiceSettings settings;
    settings.root=9;settings.scale=0;settings.correction=1.0f;
    settings.referenceHz=440.0f;settings.humanize=0.0f;
    settings.flexTune=0.0f;settings.noteHysteresis=0.10f;
    engine.setVoiceSettings(settings);
    engine.enableEarlyOnset(early);
    engine.enableResidualPreservation(residual);
    engine.enableFastEngagement(fastEngagement);
    engine.enableHighRegisterOnset(highRegister);
    engine.enableMidRegisterOnset(midRegister);
    engine.enableUltraHighRegisterOnset(ultraHigh);
    engine.enableExperimentalEarlyTransitions(fastTransitions);
    std::array<float,blockSize> left{},right{};
    for(std::size_t offset=0;offset<input.frames();offset+=blockSize) {
        const int n=static_cast<int>(std::min<std::size_t>(
                                  blockSize,input.frames()-offset));
        for(int i=0;i<n;++i) {
            const auto frame=offset+static_cast<std::size_t>(i);
            left[static_cast<std::size_t>(i)]=input.data[frame*input.channels];
            if(input.channels==2)
                right[static_cast<std::size_t>(i)]=input.data[frame*2+1];
        }
        float* buffers[2]={left.data(),input.channels==2?right.data():nullptr};
        engine.process(buffers,input.channels,n);
        for(int i=0;i<n;++i) {
            const auto frame=offset+static_cast<std::size_t>(i);
            out.data[frame*input.channels]=left[static_cast<std::size_t>(i)];
            if(input.channels==2)
                out.data[frame*2+1]=right[static_cast<std::size_t>(i)];
        }
    }
    std::cout<<"VOCADITO_CAUSAL_LIVE residual="<<residual<<" early="<<early
             <<" fast_wet="<<fastEngagement
             <<" fast_note_transitions="<<fastTransitions
             <<" ultra_high="<<ultraHigh
             <<" first_ultra_ms="<<(engine.firstUltraSample()>=0?
                  1000.0*engine.firstUltraSample()/input.rate:-1.0)
             <<" high_register="<<highRegister
             <<" mid_register="<<midRegister
             <<" first_mid_ms="<<(engine.firstMidSample()>=0?
                 1000.0*engine.firstMidSample()/input.rate:-1.0)
             <<" first_high_ms="<<(engine.firstHighSample()>=0?
                 1000.0*engine.firstHighSample()/input.rate:-1.0)
             <<" first_F0_ms="<<1000.0*engine.firstVoiceSample()/input.rate
             <<" first_halfwet_ms="<<1000.0*engine.firstHalfWetSample()/input.rate
             <<" final_source_hz="<<engine.sourceHz()<<"\n";
    return out;
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3 || std::string(argv[1])!="--prefix")
            throw std::runtime_error("usage: --prefix vocadito_human|vocadito_low|vocadito_high");
        const std::string prefix=argv[2];
        const auto source=readWave(prefix+"_source.wav");
        if(source.frames()<source.rate || source.channels<1 || source.channels>2)
            throw std::runtime_error("Unsupported short/empty real singer WAV");
        const Wave candidate=renderLive(source,true,false);
        const Wave preserved=renderLive(source,true,true);
        const Wave fast=renderLive(source,true,true,true,true);
        // Combined guarded high/mid notes, STILL research-only.
        const Wave high=renderLive(source,true,true,true,true,true,true);
        const Wave ultra=renderLive(source,true,true,true,true,true,true,true);
        const auto levels=level(candidate);
        const auto residualLevels=level(preserved);
        const auto fastLevels=level(fast);
        const auto highLevels=level(high);
        const auto ultraLevels=level(ultra);
        if(!std::isfinite(levels.peak) || !std::isfinite(levels.rms) ||
           levels.rms<1.0e-7 ||
           !std::isfinite(residualLevels.peak) ||
           !std::isfinite(residualLevels.rms) || residualLevels.rms<1.0e-7 ||
           !std::isfinite(fastLevels.peak) ||
           !std::isfinite(fastLevels.rms) || fastLevels.rms<1.0e-7 ||
           !std::isfinite(highLevels.peak) ||
           !std::isfinite(highLevels.rms) || highLevels.rms<1.0e-7 ||
           !std::isfinite(ultraLevels.peak) ||
           !std::isfinite(ultraLevels.rms) || ultraLevels.rms<1.0e-7)
            throw std::runtime_error("Predictive LIVE generated invalid/silent human audio");
        const std::string result=prefix+"_harmonic_live_research.wav";
        writeWave24(result,candidate);
        const std::string residualFile=prefix+"_harmonic_live_residual_research.wav";
        writeWave24(residualFile,preserved);
        const std::string fastFile=prefix+"_harmonic_live_fast_research.wav";
        writeWave24(fastFile,fast);
        const std::string highFile=prefix+"_harmonic_live_high_research.wav";
        writeWave24(highFile,high);
        const std::string ultraFile=prefix+"_harmonic_live_ultra_research.wav";
        writeWave24(ultraFile,ultra);
        const Wave validated=readWave(result);
        const Wave residualValidated=readWave(residualFile);
        const Wave fastValidated=readWave(fastFile);
        const Wave highValidated=readWave(highFile);
        const Wave ultraValidated=readWave(ultraFile);
        if(validated.frames()!=source.frames() ||
           validated.rate!=source.rate || validated.channels!=source.channels ||
           residualValidated.frames()!=source.frames() ||
           residualValidated.rate!=source.rate ||
           residualValidated.channels!=source.channels ||
           fastValidated.frames()!=source.frames() ||
           fastValidated.rate!=source.rate ||
           fastValidated.channels!=source.channels ||
           highValidated.frames()!=source.frames() ||
           highValidated.rate!=source.rate ||
           highValidated.channels!=source.channels ||
           ultraValidated.frames()!=source.frames() ||
           ultraValidated.rate!=source.rate ||
           ultraValidated.channels!=source.channels)
            throw std::runtime_error("LIVE human PCM24 round-trip corrupted format");
        const std::string report=prefix+"_HARMONIC_LIVE_RESEARCH.txt";
        std::ofstream out(report);
        if(!out)throw std::runtime_error("Cannot open research evidence report");
        out<<std::setprecision(8)
           <<"NON-SHIPPING CAUSAL HARMONIC VOCAL TEST — HQ40 UNCHANGED\n"
           <<"Public singer provenance: VOCADITO_LICENSE_AND_EVIDENCE.txt or "
             "VOCADITO_REGISTER_MATRIX_LICENSE.txt\n"
           <<"Rate="<<source.rate<<" channels="<<source.channels
           <<" frames="<<source.frames()<<"\n"
           <<"Peak="<<levels.peak<<" RMS="<<levels.rms
           <<" clipped_frames="<<levels.clips<<"\n"
           <<"Residual replacement peak="<<residualLevels.peak
           <<" RMS="<<residualLevels.rms
           <<" clipped_frames="<<residualLevels.clips<<"\n"
           <<"Opt-in 1-period fast-engagement peak="<<fastLevels.peak
           <<" RMS="<<fastLevels.rms
           <<" clipped_frames="<<fastLevels.clips<<"\n"
           <<"High-register accelerated peak="<<highLevels.peak
           <<" RMS="<<highLevels.rms
           <<" clipped_frames="<<highLevels.clips<<"\n"
           <<"Ultra 80-frame research peak="<<ultraLevels.peak
           <<" RMS="<<ultraLevels.rms
           <<" clipped_frames="<<ultraLevels.clips<<"\n"
           <<"Dry input and output have same frame count, but this is NOT "
             "proof of 0ms perceived voice correction, formants or note reaction.\n"
           <<"Compare separately level-matched to approved HQ40 and dry; "
             "singing with Apollo Solo and real APEX host remains mandatory.\n"
           <<(levels.clips>0
               ?"WARNING: candidate clips; do not use this take for blind sound-quality ranking.\n"
               :"No PCM24 output clipping detected in this take.\n");
        std::cout<<"REAL_HARMONIC_LIVE_RENDER "<<result
                 <<" residual_file="<<residualFile
                 <<" fast_wet_file="<<fastFile
                 <<" high_register_file="<<highFile
                 <<" ultra_high_file="<<ultraFile
                 <<" peak="<<levels.peak<<" clips="<<levels.clips<<"\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"HARMONIC_LIVE_HUMAN_ERROR: "<<e.what()<<"\n";
        return 1;
    }
}
