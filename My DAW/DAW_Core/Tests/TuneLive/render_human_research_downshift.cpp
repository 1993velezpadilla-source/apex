// Public-licensed real singing A/B of unchanged HQ40 vs non-shipping
// generated research downshift compensation. ZERO production DSP/GUI edits.
//
// Inputs: existing Vocadito original WAV + pinned production HQ40 PCM24 WAV.
// The generator READS production header, and produces an opt-in research
// class only in the CI workspace; this program proves OFF == HQ40 bit-exact
// before rendering ON. Additional reported pitch metrics are informative;
// musical tone, formant stability and ASIO suitability still need listening.
#define main offline_audition_main_not_called
#include "tune_live_offline_audition.cpp"
#undef main
#include "_generated_research_downshift_dsp.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
using ResearchDsp=APEX::TuneLiveResearch::ResearchCompensatedDspCore;
using ResearchSettings=APEX::TuneLiveResearch::DspSettings;
constexpr int blockSize=128;

Wave renderResearch(const Wave& input,bool on) {
    Wave output=input;
    ResearchDsp dsp;
    dsp.prepare(input.rate,blockSize,input.channels);
    dsp.setResearchCompensationEnabled(on);
    ResearchSettings user;
    user.correction=1.0f;
    user.key=9;
    user.scale=0;
    user.wet=1.0f;
    user.retune=0.0f;
    user.humanize=0.0f;
    user.flexTune=0.0f;
    std::array<float,blockSize> left{},right{};
    for(std::size_t offset=0;offset<output.frames();offset+=blockSize) {
        const int n=static_cast<int>(std::min<std::size_t>(
                blockSize,output.frames()-offset));
        for(int i=0;i<n;++i) {
            left[static_cast<std::size_t>(i)] =
                output.data[(offset+static_cast<std::size_t>(i))*output.channels];
            if(output.channels==2)right[static_cast<std::size_t>(i)] =
                output.data[(offset+static_cast<std::size_t>(i))*2+1];
        }
        float* channelData[2]={left.data(),
                      output.channels==2?right.data():nullptr};
        dsp.process(channelData,output.channels,n,user);
        for(int i=0;i<n;++i) {
            const auto sample=offset+static_cast<std::size_t>(i);
            output.data[sample*output.channels]=left[static_cast<std::size_t>(i)];
            if(output.channels==2)
                output.data[sample*2+1]=right[static_cast<std::size_t>(i)];
        }
    }
    const int expectedLatency=static_cast<int>(std::lround(input.rate*0.040));
    if(dsp.latencySamples()!=expectedLatency)
        throw std::runtime_error("Research twin PDC is not approved 40ms");
    return output;
}
struct F0Metric {
    std::vector<double> source,output,gain;
    std::size_t voiced=0,octave=0,otherMismatch=0;
};
double median(std::vector<double> v) {
    if(v.empty())return -1.0;
    std::sort(v.begin(),v.end());
    const auto middle=v.size()/2;
    return v.size()%2 ? v[middle] : (v[middle-1]+v[middle])*0.5;
}
struct Pitch { int note=-1; double cents=0; bool valid=false; };
Pitch measured(APEX::TuneLive::PitchTracker::Result f) {
    if(!f.voiced || !std::isfinite(f.hz) || f.hz<55.0f)return {};
    const double midi=69.0+12.0*std::log2(f.hz/440.0);
    const int note=static_cast<int>(std::lround(midi));
    if(note<0 || note>127)return {};
    return {note,std::abs(midi-note)*100.0,true};
}
void compareMetrics(const Wave& original,const Wave& hq,
                    const Wave& candidate,std::ostream& out) {
    if(original.frames()!=hq.frames() || candidate.frames()!=hq.frames())
        throw std::runtime_error("Human A/B output frame mismatch");
    std::array<APEX::TuneLive::PitchTracker,3> trackers{};
    for(auto& t:trackers)t.prepare(original.rate);
    std::array<F0Metric,2> result{};
    const auto pdc=static_cast<std::size_t>(
        std::lround(original.rate*0.040));
    int stable=0,prev=-1;
    for(std::size_t frame=0;frame<original.frames();++frame) {
        const auto ch=static_cast<std::size_t>(original.channels);
        const float source=frame>=pdc
            ?original.data[(frame-pdc)*ch] : 0.0f;
        const bool complete=trackers[0].push(source);
        trackers[1].push(hq.data[frame*ch]);
        trackers[2].push(candidate.data[frame*ch]);
        if(!complete)continue;
        const auto orig=measured(trackers[0].latest());
        if(orig.valid) {
            stable=orig.note==prev ? stable+1 : 1;prev=orig.note;
        } else {stable=0;prev=-1;}
        if(!orig.valid || stable<16)continue;
        for(int k=1;k<3;++k) {
            auto& m=result[static_cast<std::size_t>(k-1)];
            const auto wet=measured(trackers[static_cast<std::size_t>(k)].latest());
            if(!wet.valid)continue;
            ++m.voiced;
            if(wet.note!=orig.note) {
                if(std::abs(wet.note-orig.note)==12)++m.octave;
                else ++m.otherMismatch;
                continue;
            }
            m.source.push_back(orig.cents);
            m.output.push_back(wet.cents);
            m.gain.push_back(orig.cents-wet.cents);
        }
    }
    out<<std::fixed<<std::setprecision(3);
    for(int k=0;k<2;++k) {
        const auto& m=result[static_cast<std::size_t>(k)];
        out<<(k==0?"SHIPPING_HQ40":"NONSHIPPING_RESEARCH_COMPENSATED")
           <<" valid_paired_frames="<<m.output.size()
           <<" source_abs_nearest_chromatic_median_cents="<<median(m.source)
           <<" output_abs_nearest_chromatic_median_cents="<<median(m.output)
           <<" paired_median_cents_improvement="<<median(m.gain)
           <<" octave_detector_disagreements="<<m.octave
           <<" other_note_detector_disagreements="<<m.otherMismatch
           <<" output_voiced_updates="<<m.voiced<<"\\n";
    }
    if(result[0].output.size()<20 || result[1].output.size()<20)
        throw std::runtime_error("Too few stable human notes for research report");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3 || std::string(argv[1])!="--prefix")
            throw std::runtime_error("Usage: --prefix vocadito_human|vocadito_low|vocadito_high");
        const std::string prefix=argv[2];
        const Wave original=readWave(prefix+"_source.wav");
        const Wave shipping=readWave(prefix+"_hq40.wav");
        if(original.rate!=shipping.rate ||
           original.channels!=shipping.channels ||
           original.frames()!=shipping.frames())
            throw std::runtime_error("Wrong reference shipping HQ40 file");
        // The critical safety condition: same DSP class implementation,
        // same parameters, and OFF must exactly reproduce SHIPPING PCM24.
        const Wave disabled=renderResearch(original,false);
        const std::string identityPath=prefix+"_research_identity_temp.wav";
        writeWave24(identityPath,disabled);
        const Wave roundtrip=readWave(identityPath);
        std::remove(identityPath.c_str());
        if(roundtrip.data.size()!=shipping.data.size())
            throw std::runtime_error("Research twin / shipping mismatch size");
        for(std::size_t i=0;i<shipping.data.size();++i) {
            if(roundtrip.data[i]!=shipping.data[i])
                throw std::runtime_error("Research twin OFF changed APPROVED HQ40 audio at sample "+
                                         std::to_string(i));
        }
        std::cout<<"RESEARCH_TWIN_OFF_SHIPPING_HQ40_BITEXACT_PASS prefix="
                 <<prefix<<" samples="<<roundtrip.data.size()<<"\\n";
        const Wave candidate=renderResearch(original,true);
        const auto levels=level(candidate);
        if(!std::isfinite(levels.peak)||levels.peak<=0||
           levels.peak>=1.0 ||levels.clips!=0)
            throw std::runtime_error("Candidate clipped or became nonfinite");
        const std::string output=prefix+"_research_compensated.wav";
        writeWave24(output,candidate);
        const Wave reopened=readWave(output);
        if(reopened.frames()!=original.frames())
            throw std::runtime_error("Research WAV PCM24 roundtrip mismatch");
        std::ostringstream report;
        report<<"APEX TUNE LIVE — EXPERIMENTAL DOWN-SHIFT / HUMAN SINGING\\n"
              <<"Public license: VOCADITO_REGISTER_MATRIX_LICENSE.txt / "
                "VOCADITO_LICENSE_AND_EVIDENCE.txt\\n"
              <<"Production HQ40 and APEX GUI were NOT CHANGED.\\n"
              <<"Twin OFF reproduced production HQ40 sample-for-sample.\\n"
              <<"NONSHIPPING downshift candidate uses 48kHz stationary synthetic A3/A4 "
                "lookup knots. Human voice results do NOT prove musical quality.\\n"
              <<"Voiced tracker was measured after 40ms sample-alignment on the source. "
                "Steady note needs 16 YIN updates.\\n"
              <<"No latency improvement; no human preference ABX yet.\\n"
              <<"Input sample_rate="<<original.rate
              <<" channels="<<original.channels
              <<" seconds="<<static_cast<double>(original.frames())/original.rate
              <<" candidate_peak="<<levels.peak
              <<" candidate_rms="<<levels.rms
              <<" candidate_clipped_samples="<<levels.clips<<"\\n";
        compareMetrics(original,shipping,reopened,report);
        const std::string reportFile=prefix+"_RESEARCH_COMPENSATION_REPORT.txt";
        std::ofstream out(reportFile);
        if(!out)throw std::runtime_error("Cannot write research report");
        out<<report.str();
        std::cout<<report.str();
        std::cout<<"RESEARCH_HUMAN_DOWNSHIFT_AB_PASS prefix="<<prefix
                 <<" output="<<output<<"\\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"RESEARCH_HUMAN_DOWNSHIFT_AB_FAIL: "<<e.what()<<"\\n";
        return 1;
    }
}
