// Research-only A/B on independently licensed singer recordings.
#define main offline_audition_unused
#include "../tune_live_offline_audition.cpp"
#undef main
#include "../../../Source/TuneLiveCore/ApexTuneLiveRealtimeCore.h"
#include <array>
#include <chrono>
#include <iomanip>
#include <memory>
#include <numeric>
namespace {
struct Report {
    Wave wave;
    double maxStep=0,peak=0,p95=0,medianCents=0;
    int invalid=0,budget=0;
};
double median(std::vector<double> samples,double percentile) {
    if(samples.empty())return 0;
    const std::size_t n=static_cast<std::size_t>((samples.size()-1)*percentile);
    std::nth_element(samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(n),samples.end());
    return samples[n];
}
Report render(const Wave& source,bool research) {
    Report result;
    result.wave=source;
    auto engine=std::make_unique<APEX::TuneLive::RealtimeCore>();
    engine->prepare(source.rate,source.channels);
    engine->setResearchPhaseSlew(research);
    APEX::TuneLive::DspSettings p;
    p.correction=p.wet=1;p.retune=p.humanize=p.flexTune=0;
    std::array<float,128> left{},right{};
    std::vector<double> times;
    for(std::size_t start=0;start<source.frames();start+=128) {
        const auto count=static_cast<int>(std::min<std::size_t>(128,source.frames()-start));
        for(int i=0;i<count;++i) {
            const std::size_t k=(start+static_cast<std::size_t>(i))*source.channels;
            left[static_cast<std::size_t>(i)]=source.data[k];
            if(source.channels==2)right[static_cast<std::size_t>(i)]=source.data[k+1];
        }
        float* channels[2]={left.data(),source.channels==2?right.data():nullptr};
        const auto began=std::chrono::steady_clock::now();
        engine->process(channels,source.channels,count,p);
        const double elapsed=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-began).count();
        times.push_back(elapsed);
        if(elapsed>1000.0*count/source.rate)++result.budget;
        for(int i=0;i<count;++i) {
            const std::size_t k=(start+static_cast<std::size_t>(i))*source.channels;
            result.wave.data[k]=left[static_cast<std::size_t>(i)];
            if(source.channels==2)result.wave.data[k+1]=right[static_cast<std::size_t>(i)];
        }
    }
    result.p95=median(times,.95);
    for(std::size_t i=0;i<result.wave.data.size();++i) {
        const double x=result.wave.data[i];
        if(!std::isfinite(x)){++result.invalid;continue;}
        result.peak=std::max(result.peak,std::abs(x));
        if(i>=static_cast<std::size_t>(source.channels))
            result.maxStep=std::max(result.maxStep,std::abs(
                x-result.wave.data[i-source.channels]));
    }
    APEX::TuneLive::PitchTracker detector;
    detector.prepare(source.rate);
    std::vector<double> deviations;
    for(std::size_t i=0;i<result.wave.frames();++i) {
        if(!detector.push(result.wave.data[i*source.channels]))continue;
        const auto pitch=detector.latest();
        if(!pitch.voiced||pitch.hz<=0||!std::isfinite(pitch.hz))continue;
        const double midi=69.0+12*std::log2(pitch.hz/440.0);
        deviations.push_back(100.0*std::abs(midi-std::round(midi)));
    }
    result.medianCents=median(std::move(deviations),.5);
    return result;
}
void print(const char* mode,const Report& r) {
    std::cout<<std::fixed<<std::setprecision(6)
        <<"{\"mode\":\""<<mode<<"\",\"frames\":"<<r.wave.frames()
        <<",\"rate\":"<<r.wave.rate<<",\"channels\":"<<r.wave.channels
        <<",\"max_step\":"<<r.maxStep<<",\"peak\":"<<r.peak
        <<",\"nearest_tet_cents_median\":"<<r.medianCents
        <<",\"callback_p95_ms\":"<<r.p95
        <<",\"callback_over_budget\":"<<r.budget
        <<",\"nonfinite\":"<<r.invalid<<"}\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=5||std::string(argv[1])!="--input"||std::string(argv[3])!="--prefix")
            throw std::runtime_error("usage: --input voice.wav --prefix output");
        const auto dry=readWave(argv[2]);
        if(dry.frames()<dry.rate*2u)throw std::runtime_error("Voice excerpt too short");
        const auto old=render(dry,false),test=render(dry,true);
        if(old.invalid||test.invalid)throw std::runtime_error("DSP generated NaN/Inf");
        print("original",old);print("phase_research",test);
        const std::string prefix=argv[4];
        writeWave24(prefix+"_original.wav",old.wave);
        writeWave24(prefix+"_research.wav",test.wave);
        std::ofstream f(prefix+"_license.txt");
        f<<"Vocadito: Bittner, Pasalo, Bosch, Meseguer-Brocal, Rubinstein.\n"
          "CC BY 4.0 https://zenodo.org/records/5578807\n"
          "Source files are independent licensed recordings; research-only.\n";
        if(!f)throw std::runtime_error("Failed license report");
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"REAL_SINGER_AB_FAIL: "<<e.what()<<"\n";return 1;
    }
}
