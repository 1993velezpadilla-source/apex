// APEX Tune Live RESEARCH-only 3-real-singer AB of harmonic note-leap wet
// reseed. Compares PREVIOUS high/mid vs ultra and NEW opt-in dry-reset ultra.
// DOES NOT infer "smoother voice" from logarithmic spectral averages.
// Do not merge this into HQ40 or ship an unlistened option.
// Preserve all existing output variants, and fail only obvious spectral
// damage relative to the validated ultra baseline.
#define APEX_ULTRA_SPECTRAL_HELPER_ONLY 1
#include "test_human_log_spectrum_formant_proxy.cpp"
#undef APEX_ULTRA_SPECTRAL_HELPER_ONLY
#include <iomanip>
#include <fstream>
#include <iostream>
#include <cmath>
int main(int argc,char** argv) {
    try {
        if(argc!=3 || std::string(argv[1])!="--prefix")
            throw std::runtime_error("usage: --prefix vocadito_human|low|high");
        const std::string prefix=argv[2];
        const Wave source=readWave(prefix+"_source.wav");
        const Wave prior=readWave(prefix+"_harmonic_live_high_research.wav");
        const Wave old=readWave(prefix+"_harmonic_live_ultra_research.wav");
        const Wave reset=readWave(prefix+"_harmonic_live_ultra_smooth_research.wav");
        const auto before=compare(source,prior,old);
        const auto after=compare(source,prior,reset);
        if(before.frames!=after.frames || before.frames<15)
            throw std::runtime_error("different source window populations");
        const bool noGrossRegression=
            std::isfinite(after.ultraError) &&
            std::isfinite(after.worstBandChange) &&
            std::isfinite(after.deltaRms) &&
            after.ultraError<=before.ultraError+0.5 &&
            after.worstBandChange<=std::max(12.0,before.worstBandChange+1.0) &&
            after.deltaRms<=before.deltaRms+0.20 &&
            after.deltaRms<=1.0;
        // Better mean spectral error alone is NEVER proof of natural vowels;
        // report it without using it to hide individual transient harm.
        const bool improvedSpectrum=after.ultraError<before.ultraError-0.01;
        std::cout<<std::fixed<<std::setprecision(6)
            <<"ULTRA_DRY_RESET_REAL_SINGER prefix="<<prefix
            <<" voiced_windows="<<before.frames
            <<" old_ultra_vs_source_spectrum_db="<<before.ultraError
            <<" reset_ultra_vs_source_spectrum_db="<<after.ultraError
            <<" old_ultra_max_band_vs_prior_db="<<before.worstBandChange
            <<" reset_ultra_max_band_vs_prior_db="<<after.worstBandChange
            <<" old_ultra_vs_prior_pcm_delta="<<before.deltaRms
            <<" reset_ultra_vs_prior_pcm_delta="<<after.deltaRms
            <<" improved_mean_band_proxy="<<improvedSpectrum
            <<" no_gross_regression="<<noGrossRegression
            <<" NOT_FORMANT_ABX_OR_1MS_PROOF=1\n";
        const std::string report=prefix+"_ULTRA_DRY_RESET_RESEARCH.txt";
        std::ofstream out(report);
        if(!out)throw std::runtime_error("cannot create reset research report");
        out<<std::setprecision(8)
           <<"Ultra dry-leap reset A/B, RESEARCH ONLY, not production\n"
           <<"Old mean band vs dry "<<before.ultraError<<" dB\n"
           <<"Reset mean band vs dry "<<after.ultraError<<" dB\n"
           <<"Old max single band change "<<before.worstBandChange<<" dB\n"
           <<"Reset max single band change "<<after.worstBandChange<<" dB\n"
           <<"Reset safer signal bound "<<noGrossRegression<<"\n"
           <<"No human subjective/formant equivalence or 1ms response proof\n";
        if(!out)throw std::runtime_error("cannot write report");
        return noGrossRegression?0:1;
    }catch(const std::exception& e) {
        std::cerr<<"ULTRA_DRY_RESET_REAL_SINGER_ERROR "<<e.what()<<"\n";
        return 1;
    }
}
