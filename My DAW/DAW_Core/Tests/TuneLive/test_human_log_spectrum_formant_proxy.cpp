// APEX Tune LIVE research: independent REAL-singer spectral-envelope PROXY.
// Compare the new 80-frame ultra-high candidate against the previously
// validated 96/128-frame predictor and original source, using 2048-point
// Hann FFT, voiced-energy windows, normalized logarithmic frequency bands.
// This detects BROAD envelope damage missed by total >2k/>6k energy gates.
// 2048-point spectra contain harmonics, pitch shifting and singer phoneme
// changes; spectral similarity is NOT proof of correct formants/vowel
// identity, sibilant naturalness, ABX listening, or <=1ms pitch latency.
// No shipping/HQ40 change, no pitch reference oracle used in processing.
#define main offline_audition_main_unused
#include "tune_live_offline_audition.cpp"
#undef main
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kFFT=2048, kStride=1024, kBands=13;
constexpr double kPi=3.1415926535897932384626433832795;
constexpr std::array<double,kBands+1> bounds{{
    100.,160.,250.,390.,610.,950.,1480.,2300.,3550.,5150.,
    6800.,8500.,11000.,14000.
}};
using Complex=std::complex<double>;
using Spectrum=std::array<double,kBands>;

void fft(std::array<Complex,kFFT>& x) {
    for(int i=1,j=0;i<kFFT;++i) {
        int bit=kFFT>>1;
        while(j&bit){j^=bit;bit>>=1;}
        j^=bit;
        if(i<j)std::swap(x[static_cast<std::size_t>(i)],
                          x[static_cast<std::size_t>(j)]);
    }
    for(int len=2;len<=kFFT;len<<=1) {
        const double a=-2.0*kPi/len;
        const Complex step{std::cos(a),std::sin(a)};
        for(int i=0;i<kFFT;i+=len) {
            Complex w{1.0,0.0};
            for(int k=0;k<len/2;++k) {
                const Complex odd=x[static_cast<std::size_t>(i+k+len/2)]*w;
                const Complex even=x[static_cast<std::size_t>(i+k)];
                x[static_cast<std::size_t>(i+k)]=even+odd;
                x[static_cast<std::size_t>(i+k+len/2)]=even-odd;
                w*=step;
            }
        }
    }
}
struct WindowSpectrum {
    Spectrum db{};
    double rms=0.0;
    bool valid=false;
};
WindowSpectrum analyze(const Wave& w,std::size_t start) {
    if(w.frames()-start<kFFT)throw std::runtime_error("short STFT window");
    std::array<Complex,kFFT> input{};
    double power=0.0;
    for(int i=0;i<kFFT;++i) {
        double mono=0.0;
        for(int ch=0;ch<w.channels;++ch) {
            const auto index=(start+static_cast<std::size_t>(i))*
                 static_cast<std::size_t>(w.channels)+static_cast<std::size_t>(ch);
            const double sample=w.data[index];
            if(!std::isfinite(sample))throw std::runtime_error("nonfinite WAV");
            mono+=sample/w.channels;
        }
        power+=mono*mono;
        const double hann=0.5-0.5*std::cos(2.0*kPi*i/(kFFT-1));
        input[static_cast<std::size_t>(i)]=Complex(mono*hann,0.0);
    }
    fft(input);
    Spectrum energy{};
    for(int i=1;i<kFFT/2;++i) {
        const double hz=static_cast<double>(i)*w.rate/kFFT;
        for(int b=0;b<kBands;++b) {
            if(hz>=bounds[static_cast<std::size_t>(b)] &&
               hz<bounds[static_cast<std::size_t>(b+1)]) {
                energy[static_cast<std::size_t>(b)]+=
                    std::norm(input[static_cast<std::size_t>(i)]);
                break;
            }
        }
    }
    const double sum=std::accumulate(energy.begin(),energy.end(),0.0);
    WindowSpectrum out;
    out.rms=std::sqrt(power/kFFT);
    out.valid=sum>1.0e-7;
    for(int b=0;b<kBands;++b)
        out.db[static_cast<std::size_t>(b)]=10.0*std::log10(
            (energy[static_cast<std::size_t>(b)]+sum*1.0e-9)/
             (sum+sum*1.0e-9));
    return out;
}
struct Stats {
    double ultraError=0.0,priorError=0.0,deltaRms=0.0;
    double worstBandChange=0.0;
    std::size_t frames=0;
};
Stats compare(const Wave& dry,const Wave& prior,const Wave& ultra) {
    if(dry.frames()!=prior.frames()||dry.frames()!=ultra.frames()||
       dry.channels!=prior.channels||dry.channels!=ultra.channels||
       dry.rate!=prior.rate||dry.rate!=ultra.rate)
        throw std::runtime_error("unaligned source/prior/ultra WAV shapes");
    Stats stats;
    for(std::size_t start=0;start+kFFT<=dry.frames();start+=kStride) {
        const auto x=analyze(dry,start);
        // Exclude silence and near-silence: comparing normalized spectral
        // shapes there is not a meaningful singer/vowel metric.
        if(!x.valid || x.rms<0.025)continue;
        const auto a=analyze(prior,start);
        const auto b=analyze(ultra,start);
        if(!a.valid||!b.valid)throw std::runtime_error("lost voiced spectral window");
        double priorError=0.0,ultraError=0.0;
        for(int band=0;band<kBands;++band) {
            const auto i=static_cast<std::size_t>(band);
            // Ignore bands with no useful energy in the dry original.
            // Both candidates are computed at same sample alignment.
            if(x.db[i]<-45.0)continue;
            const double oldDeviation=std::abs(a.db[i]-x.db[i]);
            const double newDeviation=std::abs(b.db[i]-x.db[i]);
            priorError+=oldDeviation;
            ultraError+=newDeviation;
            stats.worstBandChange=std::max(stats.worstBandChange,
                                           std::abs(b.db[i]-a.db[i]));
        }
        stats.priorError+=priorError/kBands;
        stats.ultraError+=ultraError/kBands;
        ++stats.frames;
    }
    if(stats.frames==0)throw std::runtime_error("no measurable voiced segments");
    stats.priorError/=stats.frames;
    stats.ultraError/=stats.frames;
    double difference=0.0,power=0.0;
    for(std::size_t i=0;i<prior.data.size();++i) {
        const double d=prior.data[i]-ultra.data[i];
        difference+=d*d;
        power+=static_cast<double>(prior.data[i])*prior.data[i];
    }
    stats.deltaRms=std::sqrt(difference/std::max(1e-10,power));
    return stats;
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3 || std::string(argv[1])!="--prefix")
            throw std::runtime_error("expected --prefix vocadito_human|low|high");
        const std::string prefix=argv[2];
        const auto dry=readWave(prefix+"_source.wav");
        const auto old=readWave(prefix+"_harmonic_live_high_research.wav");
        const auto now=readWave(prefix+"_harmonic_live_ultra_research.wav");
        const auto stats=compare(dry,old,now);
        // Guard against severe BROAD spectral damage over the previous
        // research path; unlike a 2/6k high-pass gate this sees 13 bands.
        // It still cannot measure true formants or sound quality.
        const bool valid=stats.frames>=15 &&
            std::isfinite(stats.priorError) &&
            std::isfinite(stats.ultraError) &&
            std::isfinite(stats.worstBandChange) &&
            stats.ultraError<=stats.priorError+2.0 &&
            stats.worstBandChange<=16.0 &&
            std::isfinite(stats.deltaRms) && stats.deltaRms<=0.75;
        std::cout<<std::fixed<<std::setprecision(5)
            <<"VOCADITO_FORMANT_PROXY prefix="<<prefix
            <<" accepted_voiced_windows="<<stats.frames
            <<" previous_13band_mean_abs_error_db="<<stats.priorError
            <<" ultra_13band_mean_abs_error_db="<<stats.ultraError
            <<" max_ultra_vs_previous_band_delta_db="<<stats.worstBandChange
            <<" normalized_ultra_vs_previous_pcm_rms="<<stats.deltaRms
            <<" spectral_guard="<<valid
            <<" NOT_actual_formant_identity_or_ABX=1"
            <<" NO_1MS_PITCH_CLAIM=1\n";
        const auto report=prefix+"_LOG_SPECTRAL_ENVELOPE_DIAGNOSTIC.txt";
        std::ofstream out(report);
        if(!out)throw std::runtime_error("cannot write report");
        out<<"APEX TUNE LIVE: licensed human-voice spectral-envelope"
              " comparative proxy, NOT real formants or audible equivalence\n";
        out<<"Voiced windows: "<<stats.frames<<"\n";
        out<<"Previous abs error dB: "<<stats.priorError<<"\n";
        out<<"Ultra abs error dB: "<<stats.ultraError<<"\n";
        out<<"Largest band delta dB: "<<stats.worstBandChange<<"\n";
        out<<"Ultra vs previous PCM relative RMS: "<<stats.deltaRms<<"\n";
        out<<"Result: "<<(valid?"PASS":"FAIL")<<"\n";
        return valid?0:1;
    }catch(const std::exception& e) {
        std::cerr<<"VOCADITO_FORMANT_PROXY_ERROR: "<<e.what()<<"\n";
        return 1;
    }
}
