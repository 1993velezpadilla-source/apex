#pragma once

// APEX Opto 2A RESEARCH: complete functional path from audio input to
// a light-driven T4 shunt cell and back to attenuated audio.
//
// Provenance: independently written, original modeling experiment.
// NOT a Teletronix or Universal Audio netlist and not calibrated to UAD.
// The pre-attenuator key is intentional: refer to the actual 1966
// Teletronix schematic and do not confuse the amplifier's negative feedback
// with the optical sidechain tap. Tubes, transformer nonlinearity and
// real R37 network remain outside this prototype.
//
// Zero heap allocations / locks / lookahead on audio thread. C++17.
#include "ApexT4OpticalCellCore.h"
#include "Apex6AQ5DriverResearchCore.h"
#include <algorithm>
#include <cmath>

namespace APEX::Compressor
{
struct T4LevelerResearchParameters
{
    double peakReduction=0.0; // nominal mechanical 0..100, not dB
    double gainDb=0.0; // output compensation
    double parallelMix=1.0;
    double keyEmphasis=0.0; // detector only, does not EQ music
    bool limit=false;
    bool bypass=false;
    bool use6AQ5Driver=false; // opt-in experimental EL stage
};
class ApexT4LevelerResearchCore final
{
public:
    void prepare(double sr,T4Calibration calibration={}) noexcept
    {
        sampleRate_=(std::isfinite(sr) && sr>=8000.0 && sr<=768000.0)?sr:48000.0;
        cell_.prepare(sampleRate_,calibration);
        pentodeDriver_.prepare(sampleRate_);
        detectorCoeff_=std::exp(-1.0/(sampleRate_*0.010));
        keyLowpassCoeff_=std::exp(-2.0*3.141592653589793*320.0/sampleRate_);
        controlCoeff_=std::exp(-1.0/(sampleRate_*0.005));
        reset();
    }
    void reset() noexcept
    {
        cell_.reset();
        pentodeDriver_.reset();
        detector_=lowL_=lowR_=0.0;
        currentOutput_=gain(p_.gainDb);
        currentMix_=p_.parallelMix;
        gate_=p_.bypass ? 0.0 : 1.0;
        started_=false;
    }
    void setParameters(T4LevelerResearchParameters p) noexcept
    {
        p.peakReduction=sane(p.peakReduction,0.0,100.0,0.0);
        p.gainDb=sane(p.gainDb,-24.0,40.0,0.0);
        p.parallelMix=sane(p.parallelMix,0.0,1.0,1.0);
        p.keyEmphasis=sane(p.keyEmphasis,0.0,1.0,0.0);
        p_=p;
        const double normalisedReduction=p_.peakReduction/100.0;
        cachedDrive_=normalisedReduction*normalisedReduction*21.0;
        cachedGain_=gain(p_.gainDb);
        if(!started_)
        {
            currentOutput_=cachedGain_;
            currentMix_=p_.parallelMix;
            gate_=p_.bypass ? 0.0 : 1.0;
        }
    }
    void process(float* left,float* right,int frames) noexcept
    {
        if(!left||frames<=0) return;
        started_=true;
        const bool stereo=right && right!=left;
        const double drive=cachedDrive_;
        for(int n=0;n<frames;++n)
        {
            const double a=std::isfinite(left[n]) ? std::clamp((double)left[n],-16.0,16.0):0.0;
            const double b=stereo ? (std::isfinite(right[n]) ?
                         std::clamp((double)right[n],-16.0,16.0):0.0) : a;
            lowL_=keyLowpassCoeff_*lowL_+(1.0-keyLowpassCoeff_)*a;
            lowR_=keyLowpassCoeff_*lowR_+(1.0-keyLowpassCoeff_)*b;
            const double keyA=a-p_.keyEmphasis*lowL_;
            const double keyB=b-p_.keyEmphasis*lowR_;
            // Linked stereo key matches the stronger channel to prevent
            // gain-image wandering. The physical chassis is mono; linking is
            // an intentional APEX host feature, not a vintage hardware claim.
            const double key=std::max(std::abs(keyA),std::abs(keyB));
            detector_=detectorCoeff_*detector_+(1.0-detectorCoeff_)*key;
            // Stand-in model of the 12AX7 -> 6AQ5 EL driver transfer.
            // No threshold pretending to be measured tube physics.
            const double input=std::clamp(drive*detector_,0.0,1.0);
            const double excitation=p_.limit ? input : input*0.77;
            // The old uncalibrated transfer is the backwards-compatible
            // default. Experimental full-path fixture opts into a DC
            // plate-current/EL transformer+rectifier transfer hypothesis.
            const double elDrive=p_.use6AQ5Driver ?
                pentodeDriver_.tick(excitation):excitation;
            cell_.tick(elDrive);
            currentOutput_=controlCoeff_*currentOutput_+
                (1.0-controlCoeff_)*cachedGain_;
            currentMix_=controlCoeff_*currentMix_+
                (1.0-controlCoeff_)*p_.parallelMix;
            gate_=controlCoeff_*gate_+
                (1.0-controlCoeff_)*(p_.bypass ? 0.0 : 1.0);
            const double mix=currentMix_*gate_;
            const double wetGain=cell_.getAmplitudeGain()*currentOutput_;
            left[n]=static_cast<float>(a*(1.0-mix)+a*wetGain*mix);
            if(stereo)
                right[n]=static_cast<float>(b*(1.0-mix)+b*wetGain*mix);
        }
    }
    double getGainReductionDb() const noexcept{return cell_.getGainReductionDb();}
    double getOpticalExposure() const noexcept{return cell_.getExposureMemory();}
    double getAudioPhotoResistanceOhms() const noexcept{return cell_.getAudioResistanceOhms();}
    double getMeterPhotoResistanceOhms() const noexcept{return cell_.getMeterResistanceOhms();}
    static constexpr int getLatencySamples() noexcept{return 0;}
private:
    static double sane(double v,double lo,double hi,double fallback) noexcept {
        return std::isfinite(v) ? std::clamp(v,lo,hi):fallback;
    }
    static double gain(double db) noexcept {
        return std::exp(db*0.11512925464970229);
    }
    ApexT4OpticalCellCore cell_;
    Apex6AQ5DriverResearchCore pentodeDriver_;
    T4LevelerResearchParameters p_ {};
    double sampleRate_=48000,detectorCoeff_=0,keyLowpassCoeff_=0,controlCoeff_=0;
    double detector_=0,lowL_=0,lowR_=0,currentOutput_=1,currentMix_=1,gate_=1;
    double cachedDrive_=0.0,cachedGain_=1.0;
    bool started_=false;
};
} // namespace APEX::Compressor
