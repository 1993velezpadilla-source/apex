// NON-SHIPPING: measure LOCALIZED singer transition changes that aggregate
// 13-band averages hide. Strict differential gate against previous high/mid
// research output. Independent physical PCM, no embedded F0 oracle.
// Time window = 2048 samples, hop = 1024 samples; this is not proof of
// subjective vocal naturalness, phase coherence or formant identity.
// No production code/HQ40 is altered by this test.
#define APEX_ULTRA_SPECTRAL_HELPER_ONLY 1
#include "test_human_log_spectrum_formant_proxy.cpp"
#undef APEX_ULTRA_SPECTRAL_HELPER_ONLY
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc,char** argv) {
    try {
        if(argc!=3 || std::string(argv[1])!="--prefix")
            throw std::runtime_error("Usage: --prefix vocadito_human|vocadito_low|vocadito_high");
        const std::string prefix=argv[2];
        const Wave dry=readWave(prefix+"_source.wav");
        const Wave previous=readWave(prefix+"_harmonic_live_high_research.wav");
        const Wave ultra=readWave(prefix+"_harmonic_live_ultra_research.wav");
        if(dry.frames()!=previous.frames() || dry.frames()!=ultra.frames() ||
           dry.rate!=previous.rate || dry.rate!=ultra.rate ||
           dry.channels!=previous.channels || dry.channels!=ultra.channels)
            throw std::runtime_error("unaligned human WAV files");
        std::size_t accepted=0,above5db=0,above50pct=0;
        double worstDb=0.0,worstTime=-1.0,worstRms=0.0;
        double worstRelativePcm=0.0,firstChangedTime=-1.0;
        for(std::size_t offset=0;offset+kFFT<=dry.frames();offset+=kStride) {
            const auto sourceSpec=analyze(dry,offset);
            if(!sourceSpec.valid || sourceSpec.rms<0.025)continue;
            ++accepted;
            const auto originalSpec=analyze(previous,offset);
            const auto ultraSpec=analyze(ultra,offset);
            double currentWorst=0.0;
            for(int band=0;band<kBands;++band) {
                const auto i=static_cast<std::size_t>(band);
                if(sourceSpec.db[i]<-45.0)continue;
                currentWorst=std::max(currentWorst,std::abs(
                     ultraSpec.db[i]-originalSpec.db[i]));
            }
            double oldPower=0.0,diffPower=0.0;
            for(int j=0;j<kFFT;++j) {
                const auto frame=offset+static_cast<std::size_t>(j);
                for(int ch=0;ch<dry.channels;++ch) {
                    const auto index=frame*static_cast<std::size_t>(dry.channels)
                                     +static_cast<std::size_t>(ch);
                    const double x=previous.data[index],y=ultra.data[index];
                    oldPower+=x*x;
                    diffPower+=(y-x)*(y-x);
                }
            }
            const double relative=std::sqrt(diffPower/
                                            std::max(oldPower,1e-12));
            if(relative>0.025 && firstChangedTime<0)
                firstChangedTime=static_cast<double>(offset)/dry.rate;
            above5db+=(currentWorst>5.0);
            above50pct+=(relative>0.50);
            worstRelativePcm=std::max(worstRelativePcm,relative);
            if(currentWorst>worstDb) {
                worstDb=currentWorst;
                worstTime=static_cast<double>(offset)/dry.rate;
                worstRms=sourceSpec.rms;
            }
        }
        if(accepted<15)throw std::runtime_error("no voiced windows");
        const double fractionBad=static_cast<double>(above5db)/accepted;
        const double fractionPcm=static_cast<double>(above50pct)/accepted;
        const bool valid=std::isfinite(worstDb) &&
            std::isfinite(worstRelativePcm) &&
            worstDb<=12.0 && fractionBad<=0.04 &&
            fractionPcm<=0.025 && worstRelativePcm<=1.2;
        std::cout<<std::fixed<<std::setprecision(5)
            <<"ULTRA_REAL_VOICE_LOCAL_CHANGE prefix="<<prefix
            <<" voiced_windows="<<accepted
            <<" above_5db_windows="<<above5db
            <<" above_5db_fraction="<<fractionBad
            <<" above_half_pcm_windows="<<above50pct
            <<" above_half_pcm_fraction="<<fractionPcm
            <<" worst_13band_delta_db="<<worstDb
            <<" worst_13band_time_seconds="<<worstTime
            <<" worst_source_window_rms="<<worstRms
            <<" worst_relative_pcm_rms="<<worstRelativePcm
            <<" first_noticeable_change_seconds="<<firstChangedTime
            <<" localized_signal_guard="<<valid
            <<" NOT_FORMANT_OR_HUMAN_ABX=1\n";
        const std::string report=prefix+"_ULTRA_LOCAL_TRANSIENT_AUDIT.txt";
        std::ofstream out(report);
        if(!out)throw std::runtime_error("cannot write transient report");
        out<<std::fixed<<std::setprecision(7)
           <<"VOCADITO ULTRA HIGH LOCAL CHANGE FORENSICS\n"
           <<"Research-only; NOT artist audio QC or formants\n"
           <<"voiced_windows="<<accepted<<"\n"
           <<"above_5db_windows="<<above5db<<"\n"
           <<"above_5db_fraction="<<fractionBad<<"\n"
           <<"above_half_pcm_windows="<<above50pct<<"\n"
           <<"above_half_pcm_fraction="<<fractionPcm<<"\n"
           <<"worst_13band_delta_db="<<worstDb<<"\n"
           <<"worst_13band_time_seconds="<<worstTime<<"\n"
           <<"worst_relative_pcm_rms="<<worstRelativePcm<<"\n"
           <<"first_noticeable_change_seconds="<<firstChangedTime<<"\n"
           <<"result="<<(valid?"PASS":"FAIL")<<"\n";
        if(!out)throw std::runtime_error("error saving report");
        return valid?0:1;
    }catch(const std::exception& e) {
        std::cerr<<"ULTRA_REAL_VOICE_LOCAL_CHANGE_ERROR "<<e.what()<<"\n";
        return 1;
    }
}
