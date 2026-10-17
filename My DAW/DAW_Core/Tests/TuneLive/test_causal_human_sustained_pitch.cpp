// NON-SHIPPING reality check: independent YIN analysis on actual licensed
// Vocadito singers comparing DRY, HQ40, causal fast and high-register mode.
// Measures sustained paired voiced notes, false octaves, cents and correction
// direction. These are DIAGNOSTICS, not proof of natural voice/formants,
// reliable onsets, 1ms pitch response, or ASIO mic-to-ear latency.
//
// NOTE: never judge a professional singer by distance from A440 12-TET:
// this is only a known target for the chromatic research configuration.
#define main offline_audition_main_not_called
#include "tune_live_offline_audition.cpp"
#undef main
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kVariants=9;
struct Note {int nearest=-1;double cents=0.0;bool voiced=false;};
Note decode(APEX::TuneLive::PitchTracker::Result r) {
    if(!r.voiced || r.hz<55.0f || !std::isfinite(r.hz))return {};
    const double midi=69.0+12.0*std::log2(r.hz/440.0);
    const int key=static_cast<int>(std::lround(midi));
    return {key,100.0*std::abs(midi-key),key>=0&&key<128};
}
double median(std::vector<double> v) {
    if(v.empty())return -1.0;
    std::sort(v.begin(),v.end());
    const auto i=v.size()/2;
    return (v.size()%2==0)?0.5*(v[i]+v[i-1]):v[i];
}
struct Outcome {
    std::size_t paired=0,unvoiced=0,octave=0,otherWrongNote=0;
    std::vector<double> correctedCents,improvements;
};
}
int main(int argc,char** argv) {
    try {
        if(argc!=3 || std::string(argv[1])!="--prefix")
            throw std::runtime_error("Usage: --prefix vocadito_human|vocadito_low|vocadito_high");
        const std::string prefix=argv[2];
        const std::array<std::string,kVariants> paths{{
            prefix+"_source.wav",
            prefix+"_hq40.wav",
            prefix+"_harmonic_live_residual_research.wav",
            prefix+"_harmonic_live_fast_research.wav",
            prefix+"_harmonic_live_high_research.wav",
            prefix+"_harmonic_live_ultra_research.wav",
            prefix+"_harmonic_live_ultra_4hop_research.wav",
            prefix+"_harmonic_live_ultra_consensus_research.wav",
            prefix+"_harmonic_live_halfcycle_research.wav"
        }};
        std::array<Wave,kVariants> waves{{
            readWave(paths[0]),readWave(paths[1]),
            readWave(paths[2]),readWave(paths[3]),
            readWave(paths[4]),readWave(paths[5]),readWave(paths[6]),
            readWave(paths[7]),readWave(paths[8])
        }};
        const auto& dry=waves[0];
        if(dry.frames()<dry.rate || (dry.channels!=1&&dry.channels!=2))
            throw std::runtime_error("Invalid human source WAV");
        for(int i=1;i<kVariants;++i) {
            const auto& w=waves[static_cast<std::size_t>(i)];
            if(w.frames()!=dry.frames()||w.rate!=dry.rate||
               w.channels!=dry.channels)
                throw std::runtime_error("Output and dry timebase differ");
        }
        std::array<APEX::TuneLive::PitchTracker,kVariants> detectors{};
        for(auto& detector:detectors)detector.prepare(dry.rate);
        std::array<Outcome,kVariants-1> measures{};
        int stableFrames=0,previousKey=-1;
        std::size_t totalUpdates=0,stableVoicedUpdates=0;
        for(std::size_t frame=0;frame<dry.frames();++frame) {
            std::array<bool,kVariants> fresh{};
            for(int version=0;version<kVariants;++version) {
                const auto& wav=waves[static_cast<std::size_t>(version)];
                const float sample=wav.data[frame*static_cast<std::size_t>(wav.channels)];
                fresh[static_cast<std::size_t>(version)]=
                    detectors[static_cast<std::size_t>(version)].push(sample);
            }
            if(!fresh[0])continue;
            ++totalUpdates;
            const Note original=decode(detectors[0].latest());
            if(!original.voiced) {stableFrames=0;previousKey=-1;continue;}
            if(original.nearest!=previousKey) {
                stableFrames=1;
                previousKey=original.nearest;
            } else ++stableFrames;
            // Skip note attacks (~90ms), first-phrase history and changing
            // scale notes; unlike fixed-HQ audio these modes are causal.
            if(stableFrames<16)continue;
            ++stableVoicedUpdates;
            for(int i=1;i<kVariants;++i) {
                auto& result=measures[static_cast<std::size_t>(i-1)];
                const Note corrected=decode(detectors[static_cast<std::size_t>(i)].latest());
                if(!corrected.voiced){++result.unvoiced;continue;}
                if(corrected.nearest!=original.nearest) {
                    if(std::abs(corrected.nearest-original.nearest)==12)
                        ++result.octave;
                    else ++result.otherWrongNote;
                    continue;
                }
                ++result.paired;
                result.correctedCents.push_back(corrected.cents);
                result.improvements.push_back(original.cents-corrected.cents);
            }
        }
        std::ostringstream report;
        report<<std::fixed<<std::setprecision(4)
              <<"REAL_SINGER_CAUSAL_F0_RESEARCH prefix="<<prefix
              <<" sr="<<dry.rate<<" original_updates="<<totalUpdates
              <<" stable_source_updates="<<stableVoicedUpdates<<"\n";
        const std::array<const char*,8> labels{{"HQ40","NORMAL","FAST","FAST_HIGH","ULTRA_HIGH","ULTRA_4HOP","ULTRA_CONSENSUS","HALF_CYCLE"}};
        bool valid=stableVoicedUpdates>=20;
        for(int i=0;i<kVariants-1;++i) {
            const auto& x=measures[static_cast<std::size_t>(i)];
            const double cents=median(x.correctedCents);
            const double improvement=median(x.improvements);
            const double octaveRate=stableVoicedUpdates?
                static_cast<double>(x.octave)/stableVoicedUpdates:0.0;
            const double otherNoteRate=stableVoicedUpdates?
                static_cast<double>(x.otherWrongNote)/stableVoicedUpdates:0.0;
            // Diagnostic validity only. Bad musical quality MUST be
            // reported, not silently hidden behind lenient green CI.
            valid &=x.paired>=20 && std::isfinite(cents) &&
                    std::isfinite(improvement);
            report<<"REAL_SINGER_CAUSAL_F0 option="<<labels[static_cast<std::size_t>(i)]
                  <<" stable_paired="<<x.paired
                  <<" stable_unvoiced="<<x.unvoiced
                  <<" octave_errors="<<x.octave
                  <<" wrong_note="<<x.otherWrongNote
                  <<" octave_fraction="<<octaveRate
                  <<" wrong_note_fraction="<<otherNoteRate
                  <<" median_cents_from_nearest_12TET="<<cents
                  <<" median_change_vs_dry_cents="<<improvement
                  <<"\n";
        }
        // Dataset-pinned REAL low-register regression: the accelerated
        // 1-period crossfade previously caused 21 measured false octaves
        // where the conservative 3-period causal mix caused zero. Prevent
        // a future "faster" research change silently bringing them back.
        // Does NOT certify the singer's identity, perception, ASIO timing
        // or musical truth from 12-TET alone.
        if(prefix=="vocadito_low") {
            for(int k=2;k<=4;++k) {
                const auto& candidate=measures[static_cast<std::size_t>(k)];
                const double cents=median(candidate.correctedCents);
                const bool safe=candidate.paired>=500 &&
                    candidate.octave<=2 &&
                    candidate.unvoiced<=30 &&
                    candidate.otherWrongNote<=6 &&
                    cents>=0.0 && cents<=12.0;
                valid &=safe;
                report<<"REAL_SINGER_LOW_REGISTER_OCTAVE_GUARD option="
                      <<(k==2?"FAST":(k==3?"FAST_HIGH":"ULTRA_HIGH"))
                      <<" protected="<<safe
                      <<" octave_errors="<<candidate.octave
                      <<" paired="<<candidate.paired
                      <<" median_12TET_cents="<<cents<<"\\n";
            }
        }
        // Optional ultra-high onset MUST NOT regress real measured stable
        // singer notes compared with the prior 96-frame high/mid renderer.
        // Counts derive from independent output F0, not detector metadata.
        const auto& prior=measures[3];
        const auto& ultra=measures[4];
        const auto& fastHop=measures[5];
        const auto& consensus=measures[6];
        const auto& halfCycle=measures[7];
        const bool ultraStable=
            ultra.octave<=prior.octave+2 &&
            ultra.otherWrongNote<=prior.otherWrongNote+3 &&
            ultra.unvoiced<=prior.unvoiced+10 &&
            ultra.paired+15>=prior.paired;
        valid &=ultraStable;
        // Fast hop is a separate OFF-by-default experiment. Require it
        // to keep real singer stable-note accuracy within an explicit
        // noninferiority budget relative to the already-probed 8-hop mode.
        // This cannot detect brief consonant or note-onset artifacts.
        const bool fastHopStable=
            fastHop.octave<=ultra.octave+1 &&
            fastHop.otherWrongNote<=ultra.otherWrongNote+2 &&
            fastHop.unvoiced<=ultra.unvoiced+5 &&
            fastHop.paired+8>=ultra.paired;
        valid &=fastHopStable;
        const bool consensusStable=consensus.octave<=fastHop.octave+1 &&
            consensus.otherWrongNote<=fastHop.otherWrongNote+2 &&
            consensus.unvoiced<=fastHop.unvoiced+5 &&
            consensus.paired+8>=fastHop.paired;
        valid &=consensusStable;
        const bool halfCycleStable=halfCycle.octave<=consensus.octave+1 &&
            halfCycle.otherWrongNote<=consensus.otherWrongNote+2 &&
            halfCycle.unvoiced<=consensus.unvoiced+5 &&
            halfCycle.paired+8>=consensus.paired;
        valid &=halfCycleStable;
        report<<"REAL_SINGER_HALF_CYCLE_F0 prefix="<<prefix
              <<" reference_octave="<<consensus.octave
              <<" candidate_octave="<<halfCycle.octave
              <<" reference_wrong_note="<<consensus.otherWrongNote
              <<" candidate_wrong_note="<<halfCycle.otherWrongNote
              <<" reference_unvoiced="<<consensus.unvoiced
              <<" candidate_unvoiced="<<halfCycle.unvoiced
              <<" accepted="<<halfCycleStable
              <<" NOT_HUMAN_LISTENING_OR_FORMANT_CERTIFICATE=1\n";
        report<<"REAL_SINGER_ULTRA_CONSENSUS_F0 prefix="<<prefix
              <<" fast_hop_octave="<<fastHop.octave
              <<" guarded_octave="<<consensus.octave
              <<" fast_hop_wrong_note="<<fastHop.otherWrongNote
              <<" guarded_wrong_note="<<consensus.otherWrongNote
              <<" accepted="<<consensusStable
              <<" NOT_FORMANT_OR_TRANSIENT_CERTIFICATE=1\n";
        report<<"REAL_SINGER_4HOP_STABLE_F0 prefix="<<prefix
              <<" prior_octave="<<ultra.octave
              <<" four_hop_octave="<<fastHop.octave
              <<" prior_wrong_note="<<ultra.otherWrongNote
              <<" four_hop_wrong_note="<<fastHop.otherWrongNote
              <<" prior_unvoiced="<<ultra.unvoiced
              <<" four_hop_unvoiced="<<fastHop.unvoiced
              <<" accepted="<<fastHopStable
              <<" NOT_A_HUMAN_LISTENING_CERTIFICATE=1\\n";
        report<<"REAL_SINGER_ULTRA_F0_REGRESSION prefix="<<prefix
              <<" prior_octave="<<prior.octave
              <<" ultra_octave="<<ultra.octave
              <<" prior_wrong_note="<<prior.otherWrongNote
              <<" ultra_wrong_note="<<ultra.otherWrongNote
              <<" accepted="<<ultraStable<<"\\n";
        report<<"REAL_SINGER_CAUSAL_F0_DATA_VALID="<<valid
              <<" NOT_1MS_ACQUISITION_OR_FORMANT_CERTIFICATE=1\n";
        const std::string target=prefix+"_FAST_HUMAN_F0_COMPARISON.txt";
        std::ofstream output(target);
        if(!output)throw std::runtime_error("Could not create singer metric report");
        output<<report.str();
        if(!output)throw std::runtime_error("Failed to store metric report");
        std::cout<<report.str();
        return valid?0:1;
    }catch(const std::exception& e) {
        std::cerr<<"REAL_SINGER_CAUSAL_F0_DATA_ERROR: "<<e.what()<<"\n";
        return 1;
    }
}
