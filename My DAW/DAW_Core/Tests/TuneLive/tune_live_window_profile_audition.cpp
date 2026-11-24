// Research-only APEX Tune Live real-human shifter-window auditions.
//
// Do NOT promote 18ms or 12ms profiles into the shipping JUCE processor.
// This utility runs the exact production DspCore class with explicit
// experimental prepare-time shifter profiles, for blind QUALITY evaluation.
// No GUI edits, no network/audio upload, no unaligned latency claims.
#define main offline_audition_main_not_called
#include "tune_live_offline_audition.cpp"
#undef main
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
Wave renderWindow(const Wave& source,APEX::TuneLive::ShifterProfile profile,
                  const char* name) {
    constexpr int block=128;
    using namespace APEX::TuneLive;
    DspCore processor;
    processor.prepare(source.rate,block,source.channels,profile);
    DspSettings knobs;
    knobs.key=9;
    knobs.scale=0;
    knobs.correction=1.0f;
    knobs.retune=0.0f;
    knobs.humanize=0.0f;
    knobs.flexTune=0.0f;
    knobs.wet=1.0f;
    knobs.preserveFormants=true;
    Wave wet=source;
    std::vector<float> left(block),right(block);
    for(std::size_t begin=0;begin<source.frames();begin+=block) {
        const int count=static_cast<int>(std::min<std::size_t>(
            block,source.frames()-begin));
        for(int k=0;k<count;++k) {
            const auto i=(begin+static_cast<std::size_t>(k))*
                          static_cast<std::size_t>(source.channels);
            left[static_cast<std::size_t>(k)]=wet.data[i];
            if(source.channels==2)
                right[static_cast<std::size_t>(k)]=wet.data[i+1];
        }
        float* channels[2]={left.data(),source.channels==2?right.data():nullptr};
        processor.process(channels,source.channels,count,knobs);
        for(int k=0;k<count;++k) {
            const auto i=(begin+static_cast<std::size_t>(k))*
                          static_cast<std::size_t>(source.channels);
            wet.data[i]=left[static_cast<std::size_t>(k)];
            if(source.channels==2)wet.data[i+1]=right[static_cast<std::size_t>(k)];
        }
    }
    const auto l=level(wet);
    std::cout<<"RESEARCH_WINDOW_AUDITION profile="<<name
             <<" PDC_samples="<<processor.latencySamples()
             <<" PDC_ms="<<1000.0*processor.latencySamples()/source.rate
             <<" source_sr="<<source.rate
             <<" frames="<<source.frames()
             <<" peak="<<l.peak<<" rms="<<l.rms
             <<" export_clipped_samples="<<l.clips<<"\n";
    if(l.clips>0 || !std::isfinite(l.peak) || l.peak<=0)
        throw std::runtime_error("Invalid or clipping research profile output");
    return wet;
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=5 || std::string(argv[1])!="--input" ||
           std::string(argv[3])!="--prefix")
            throw std::runtime_error("Usage: --input human.wav --prefix name");
        const Wave original=readWave(argv[2]);
        const std::string prefix=argv[4];
        if(prefix.empty())throw std::runtime_error("Invalid empty prefix");
        const Wave p18=renderWindow(original,
            APEX::TuneLive::ShifterProfile::Experimental18ms,"experimental18");
        const Wave p12=renderWindow(original,
            APEX::TuneLive::ShifterProfile::Experimental12ms,"experimental12");
        writeWave24(prefix+"_research18.wav",p18);
        writeWave24(prefix+"_research12.wav",p12);
        if(readWave(prefix+"_research18.wav").frames()!=original.frames() ||
           readWave(prefix+"_research12.wav").frames()!=original.frames())
            throw std::runtime_error("Short-window PCM24 roundtrip mismatch");
        std::cout<<"RESEARCH_WINDOW_AUDITION_PASS: 18ms,12ms non-shipping shifter "
                    "profiles, real processed WAV; formant/timbre quality "
                    "NOT certified.\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"RESEARCH_WINDOW_AUDITION_FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
