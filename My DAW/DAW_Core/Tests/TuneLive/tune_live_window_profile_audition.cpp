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
                  const char* name, bool split=false, bool fixed=false, int key=9, int scale=0) {
    constexpr int block=128;
    using namespace APEX::TuneLive;
    DspCore processor;
    processor.prepare(source.rate,block,source.channels,profile,
        fixed?StudioControlClock::ResearchFixed1ms:StudioControlClock::ApprovedHostChunks,split);
    DspSettings knobs;
    knobs.key=key;
    knobs.scale=scale;
    knobs.vibratoPreserve=0.0f;
    knobs.correction=1.0f;
    knobs.retune=0.0f;
    knobs.humanize=0.0f;
    knobs.flexTune=0.0f;
    knobs.wet=1.0f;
    knobs.preserveFormants=true;
    Wave wet=source;
    const int latency=processor.latencySamples();
    const auto frames=source.frames();
    // Flush the delay using zero input; align all new A/B files to the dry
    // time origin without discarding the ending of the sung phrase.
    const auto total=frames+(fixed?static_cast<std::size_t>(latency):0u);
    std::vector<float> left(block),right(block);
    const int sizes[]={32,64,96,128};
    int index=0;
    for(std::size_t begin=0;begin<total;) {
        const int count=static_cast<int>(std::min<std::size_t>(
            fixed?sizes[index++%4]:block,total-begin));
        for(int k=0;k<count;++k) {
            const auto i=(begin+static_cast<std::size_t>(k))*
                          static_cast<std::size_t>(source.channels);
            left[static_cast<std::size_t>(k)]=i<source.data.size()?source.data[i]:0.0f;
            if(source.channels==2)
                right[static_cast<std::size_t>(k)]=i+1<source.data.size()?source.data[i+1]:0.0f;
        }
        float* channels[2]={left.data(),source.channels==2?right.data():nullptr};
        processor.process(channels,source.channels,count,knobs);
        for(int k=0;k<count;++k) {
            const auto sample=begin+static_cast<std::size_t>(k);
            const auto delay=fixed?static_cast<std::size_t>(latency):0u;
            if(sample<delay || sample-delay>=frames)continue;
            const auto i=(sample-delay)*static_cast<std::size_t>(source.channels);
            wet.data[i]=left[static_cast<std::size_t>(k)];
            if(source.channels==2)wet.data[i+1]=right[static_cast<std::size_t>(k)];
        }
        begin+=static_cast<std::size_t>(count);
    }
    const auto l=level(wet);
    double maxStep=0.0;
    for(std::size_t i=static_cast<std::size_t>(source.channels);i<wet.data.size();++i)
        maxStep=std::max(maxStep,std::abs(double(wet.data[i])-wet.data[i-source.channels]));
    std::cout<<"RESEARCH_WINDOW_AUDITION profile="<<name
             <<" PDC_samples="<<processor.latencySamples()
             <<" PDC_ms="<<1000.0*processor.latencySamples()/source.rate
             <<" source_sr="<<source.rate
             <<" split="<<split<<" fixed_clock="<<fixed<<" aligned="<<fixed
             <<" max_step="<<maxStep
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
        using APEX::TuneLive::ShifterProfile;
        for(auto profile:{ShifterProfile::HighQuality40ms,ShifterProfile::Experimental18ms,ShifterProfile::Experimental12ms}) {
            for(bool split:{false,true}) {
                const std::string label="mobile_Csharp_major_"+std::to_string(static_cast<int>(profile))+
                    (split?"_split":"_unsplit");
                const auto audio=renderWindow(original,profile,label.c_str(),split,true,1,1);
                writeWave24(prefix+"_"+label+".wav",audio);
            }
        }
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
