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

Wave renderLive(const Wave& input, bool early) {
    Wave out=input;
    HarmonicLivePrototype engine;
    engine.prepare(input.rate,input.channels);
    APEX::TuneLive::VoiceSettings settings;
    settings.root=9;settings.scale=0;settings.correction=1.0f;
    settings.referenceHz=440.0f;settings.humanize=0.0f;
    settings.flexTune=0.0f;settings.noteHysteresis=0.10f;
    engine.setVoiceSettings(settings);
    engine.enableEarlyOnset(early);
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
    std::cout<<"VOCADITO_CAUSAL_LIVE early="<<early
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
        const Wave candidate=renderLive(source,true);
        const auto levels=level(candidate);
        if(!std::isfinite(levels.peak) || !std::isfinite(levels.rms) ||
           levels.rms<1.0e-7)
            throw std::runtime_error("Predictive LIVE generated invalid/silent human audio");
        const std::string result=prefix+"_harmonic_live_research.wav";
        writeWave24(result,candidate);
        const Wave validated=readWave(result);
        if(validated.frames()!=source.frames() ||
           validated.rate!=source.rate || validated.channels!=source.channels)
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
           <<"Dry input and output have same frame count, but this is NOT "
             "proof of 0ms perceived voice correction, formants or note reaction.\n"
           <<"Compare separately level-matched to approved HQ40 and dry; "
             "singing with Apollo Solo and real APEX host remains mandatory.\n"
           <<(levels.clips>0
               ?"WARNING: candidate clips; do not use this take for blind sound-quality ranking.\n"
               :"No PCM24 output clipping detected in this take.\n");
        std::cout<<"REAL_HARMONIC_LIVE_RENDER "<<result
                 <<" peak="<<levels.peak<<" clips="<<levels.clips<<"\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"HARMONIC_LIVE_HUMAN_ERROR: "<<e.what()<<"\n";
        return 1;
    }
}
