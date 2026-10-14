// Human singing evaluation on WAVs already rendered by the real HQ40
// processor and the isolated causal research prototype.
// This measures measurable F0 behavior, NOT perceptual sound quality.
// Keep checks descriptive; human singers need not sing equal-tempered 12-TET.
// Reuse the exact validated WAV reader without duplicating parser code.
#define main offline_audition_main_not_called
#include "tune_live_offline_audition.cpp"
#undef main
#include <array>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>

namespace {
struct Frame {
    float hz=0.0f;
    float confidence=0.0f;
    int note=-1;
    double absCents=0.0;
    bool voiced=false;
};
Frame measured(APEX::TuneLive::PitchTracker::Result r) {
    Frame f;
    if(!r.voiced || r.hz<=0 || !std::isfinite(r.hz))return f;
    const double midi=69.0+12.0*std::log2(r.hz/440.0);
    f.hz=r.hz;
    f.confidence=r.confidence;
    f.note=static_cast<int>(std::lround(midi));
    f.absCents=std::abs(100.0*(midi-f.note));
    f.voiced=std::isfinite(f.absCents) && f.note>=0 && f.note<=127;
    return f;
}
struct Outcome {
    explicit Outcome(std::string label):title(std::move(label)) {}
    std::string title;
    std::vector<double> rawOffsets,outputOffsets,improvements;
    std::size_t voiced=0,paired=0,octaveDisagreements=0,
                otherNoteDisagreements=0,improved=0,worse=0;
};
double median(std::vector<double> data) {
    if(data.empty())return -1.0;
    std::sort(data.begin(),data.end());
    const std::size_t at=data.size()/2;
    return data.size()%2==0 ? 0.5*(data[at-1]+data[at]) : data[at];
}
void metricReport(std::ostringstream& report,const Outcome& out) {
    const double before=median(out.rawOffsets);
    const double after=median(out.outputOffsets);
    const double improvement=median(out.improvements);
    report<<"\n"<<out.title<<":\n"
          <<"  paired_stable_voiced_frames="<<out.paired<<"\n"
          <<"  output_voiced_frames="<<out.voiced<<"\n"
          <<"  octave_disagreement_observations="<<out.octaveDisagreements<<"\n"
          <<"  other_note_disagreement_observations="<<out.otherNoteDisagreements<<"\n"
          <<"  median_original_abs_nearest_12TET_offset_cents="<<before<<"\n"
          <<"  median_corrected_abs_nearest_12TET_offset_cents="<<after<<"\n"
          <<"  median_paired_cents_improvement="<<improvement<<"\n"
          <<"  paired_frames_improved_more_than_3_cents="<<out.improved<<"\n"
          <<"  paired_frames_worsened_more_than_3_cents="<<out.worse<<"\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3 && argc!=4) {
            std::cerr<<"Usage: "<<argv[0]<<" --prefix rendered-prefix [--exploratory]\n";
            return 2;
        }
        if(std::string(argv[1])!="--prefix" ||
           (argc==4 && std::string(argv[3])!="--exploratory"))
            throw std::runtime_error("Expected --prefix PREFIX [--exploratory]");
        const bool exploratory=argc==4;
        const std::string prefix=argv[2];
        const auto source=readWave(prefix+"_dry.wav");
        const auto hq=readWave(prefix+"_hq40.wav");
        const auto causal=readWave(prefix+"_causal_research.wav");
        if(source.rate!=hq.rate || source.rate!=causal.rate ||
           source.channels!=hq.channels || source.channels!=causal.channels ||
           source.frames()!=hq.frames() || source.frames()!=causal.frames() ||
           source.frames()<source.rate)
            throw std::runtime_error("Invalid or mismatched WAV inputs");
        std::array<APEX::TuneLive::PitchTracker,3> trackers;
        for(auto& t:trackers)t.prepare(source.rate);
        std::array<Outcome,2> results{Outcome{"SHIPPING_HQ40"},Outcome{"CAUSAL_RESEARCH"}};
        int stableSourceCount=0,previousNote=-1;
        std::size_t framesMeasured=0,sourceVoiced=0;
                // Follow independent DSP tracker updates; never provide a known F0
        // oracle. Evaluate only steady, same-note voiced frames to avoid
        // claiming full temporal registration between variable-delay paths.
        for(std::size_t sample=0;sample<source.frames();++sample) {
            const std::size_t i=sample*static_cast<std::size_t>(source.channels);
            const bool complete=trackers[0].push(source.data[i]);
            trackers[1].push(hq.data[i]);
            trackers[2].push(causal.data[i]);
            if(!complete)continue;
            ++framesMeasured;
            const auto original=measured(trackers[0].latest());
            if(original.voiced) {
                ++sourceVoiced;
                if(original.note==previousNote)++stableSourceCount;
                else stableSourceCount=1;
                previousNote=original.note;
            } else {
                previousNote=-1;
                stableSourceCount=0;
            }
            for(int k=1;k<=2;++k) {
                auto& out=results[static_cast<std::size_t>(k-1)];
                const auto output=measured(trackers[static_cast<std::size_t>(k)].latest());
                if(output.voiced)++out.voiced;
                // Require at least 16 *detector updates* (~90ms at
                // 44.1k) of stable SOURCE note, to tolerate unaligned
                // startup and notes crossing output rendering boundaries.
                if(!original.voiced || stableSourceCount<16||!output.voiced)
                    continue;
                if(output.note!=original.note) {
                    if(std::abs(output.note-original.note)==12)
                        ++out.octaveDisagreements;
                    else ++out.otherNoteDisagreements;
                    continue;
                }
                ++out.paired;
                out.rawOffsets.push_back(original.absCents);
                out.outputOffsets.push_back(output.absCents);
                const double gain=original.absCents-output.absCents;
                out.improvements.push_back(gain);
                if(gain>3.0)++out.improved;
                if(gain< -3.0)++out.worse;
            }
        }
        std::ostringstream report;
        report<<std::fixed<<std::setprecision(3);
        report<<"APEX TUNE LIVE — HUMAN SINGER STABLE-NOTE F0 METRICS\n"
              <<"Exact dataset take and license: VOCADITO_LICENSE_AND_EVIDENCE.txt\n"
              <<"Input seconds="<<static_cast<double>(source.frames())/source.rate
              <<" sr="<<source.rate<<" channels="<<source.channels<<"\n"
              <<"Pitch tracker: same independent standalone YIN for all three rendered WAVs.\n"
              <<"No exact input-F0 oracle given to the processing engines.\n"
              <<"Number of voice trackers evaluated="<<framesMeasured
              <<" original_voiced_updates="<<sourceVoiced<<"\n"
              <<"Method: compare output to nearest A440 12-TET chromatic note, "
                "only when source sustains its note for >=16 detector updates.\n"
              <<"No fixed wet/dry alignment is inferred. Missing/unstable "
                "voiced frames are excluded from paired F0 statistics.\n"
              <<"CAUTION: offsets near 12-TET are not the musical ground truth; "
                "vibrato, glides, temperament, timing, sibilance and register "
                "transitions affect results. Improvements do NOT establish "
                "timbre/formant preservation or subjective listening quality.\n";
        for(const auto& o:results)metricReport(report,o);
        report<<"\nResearch QA gate: metric calculation valid, NOT a musical "
                "quality/pass-to-production gate.\n";
        for(const auto& o:results) {
            if(o.paired<20 || !std::isfinite(median(o.outputOffsets)))
                throw std::runtime_error("Insufficient stable voiced pairs for "+o.title);
        }
        // Repeatable, dataset-specific regression bar for the production
        // 40ms engine. This is NOT a musical-quality, human-listening or
        // ASIO-latency certificate, and must not gate the research backend.
        const auto& shipping=results[0];
        const double hqCents=median(shipping.outputOffsets);
        const double improvement=median(shipping.improvements);
        if(!exploratory && (shipping.paired<300 || hqCents>12.0 || improvement<10.0 ||
           shipping.octaveDisagreements>6)) {
            std::cerr<<"REAL_HUMAN_HQ_REGRESSION_GUARD_FAILURE paired="
                     <<shipping.paired<<" median_cents="<<hqCents
                     <<" improvement_cents="<<improvement
                     <<" octave_disagreements="<<shipping.octaveDisagreements<<"\\n";
            throw std::runtime_error("HQ40 changed too much on pinned Vocadito #24 real voice");
        }
        report<<(exploratory
            ? "\\nEXPLORATORY_REGISTER_METRICS_PASS (descriptive only): no fixed singer-specific threshold.\\n"
            : "\\nREAL_HUMAN_HQ_REGRESSION_GUARD_PASS: exemplar #24, "
                 "HQ40 <=12 cents median, >=10 cents paired median improvement, "
                 ">=300 stable pairs, <=6 octave disagreements. "
                 "Not a general human-voice fidelity certificate.\\n");
        std::ofstream output(exploratory
            ? prefix+"_PITCH_COMPARISON.txt"
            : "REAL_HUMAN_PITCH_COMPARISON.txt");
        if(!output)throw std::runtime_error("Cannot write pitch comparison report");
        output<<report.str();
        if(!output)throw std::runtime_error("Failed writing pitch report");
        std::cout<<report.str();
        std::cout<<"HUMAN_VOICE_F0_METRICS_PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"HUMAN_VOICE_F0_METRICS_FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
