#pragma once
// APEX OPTO 2A — uncalibrated FULL AUDIO-PATH RESEARCH HARNESS.
// INPUT linear transformer -> [sidechain: 6AQ5 EL drive -> T4 shunt]
// -> 12AX7 plate-stage approximation ->
// 12BH7 cathode-follower approximation -> OUTPUT linear transformer.
// Optical sidechain is pre-gain-cell, as in the referenced historic schematic.
// This is NOT a shipped DAW insert or measured Teletronix/UAD circuit.
// Driver tubes, transformer hysteresis, tube capacitance, alias protection,
// correct loading, real power-rail feedback and reference fitting remain TODO.
// Does not allocate, lock, solve nonlinear systems, or look ahead in callback.
#include "ApexT4LevelerResearchCore.h"
#include "ApexAnalogCircuitResearchCore.h"
#include "ApexMagneticTransformerResearchCore.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace APEX::Compressor {
struct ApexOptoCircuitResearchParameters {
    double peakReduction=0.0;
    double gainDb=0.0;
    double mix=1.0;
    double emphasis=0.0;
    bool limit=false;
    bool bypass=false;
};
class ApexOptoCircuitPathResearchCore final {
public:
    // Magnetic mode is PREPARE-TIME only. Each of two FIR stages adds 16
    // host samples; the dry path must then be delayed by 32 samples.
    void prepare(double sr,bool experimentalMagnetic=false) noexcept {
        sr_=std::isfinite(sr)&&sr>=8000&&sr<=768000?sr:48000;
        magneticEnabled_=experimentalMagnetic;
        controlCoeff_=std::exp(-1.0/(sr_*0.005));
        inputLeft_.prepare(sr_);
        inputRight_.prepare(sr_);
        outputLeft_.prepare(sr_);
        outputRight_.prepare(sr_);
        magneticInputLeft_.prepare(sr_);
        magneticInputRight_.prepare(sr_);
        magneticOutputLeft_.prepare(sr_);
        magneticOutputRight_.prepare(sr_);
        voltageGain_.prepare(ApexTriodeCircuit::ax7VoltageStage());
        follower_.prepare(ApexTriodeCircuit::bh7CathodeFollower());
        t4_.prepare(sr_);
        setParameters(parameters_);
        reset();
    }
    void reset() noexcept {
        inputLeft_.reset();inputRight_.reset();
        outputLeft_.reset();outputRight_.reset();
        magneticInputLeft_.reset();magneticInputRight_.reset();
        magneticOutputLeft_.reset();magneticOutputRight_.reset();
        dryDelayL_.fill(0);dryDelayR_.fill(0);dryDelayIndex_=0;
        t4_.reset();
        currentMix_=parameters_.mix;
        currentGain_=gain(parameters_.gainDb);
        currentActive_=parameters_.bypass?0.0:1.0;
        started_=false;
    }
    void setParameters(ApexOptoCircuitResearchParameters desired) noexcept {
        desired.peakReduction=sane(desired.peakReduction,0,100,0);
        desired.gainDb=sane(desired.gainDb,-24,40,0);
        desired.mix=sane(desired.mix,0,1,1);
        desired.emphasis=sane(desired.emphasis,0,1,0);
        parameters_=desired;
        T4LevelerResearchParameters optical;
        optical.peakReduction=desired.peakReduction;
        optical.keyEmphasis=desired.emphasis;
        optical.limit=desired.limit;
        optical.gainDb=0;
        optical.parallelMix=1;
        optical.bypass=false;
        optical.use6AQ5Driver=true; // experimental beam-power EL driver
        t4_.setParameters(optical);
        if(!started_) {
            currentMix_=desired.mix;
            currentGain_=gain(desired.gainDb);
            currentActive_=desired.bypass?0:1;
        }
    }
    void process(float* left,float* right,int count) noexcept {
        if(left==nullptr||count<=0)return;
        started_=true;
        const bool stereo=right!=nullptr&&right!=left;
        const double targetGain=gain(parameters_.gainDb);
        const double targetActive=parameters_.bypass?0:1;
        for(int i=0;i<count;++i) {
            const double dryLeft=std::isfinite(left[i])?(double)left[i]:0.0;
            const double dryRight=stereo?(std::isfinite(right[i])?(double)right[i]:0.0):dryLeft;
            const double inLeft=magneticEnabled_ ?
                magneticInputLeft_.process(dryLeft):inputLeft_.process(dryLeft);
            const double inRight=stereo ? (magneticEnabled_ ?
                magneticInputRight_.process(dryRight):inputRight_.process(dryRight)) : inLeft;
            float opticalLeft=static_cast<float>(inLeft);
            float opticalRight=static_cast<float>(inRight);
            // Single sample per tick: T4 memory remains continuous regardless
            // of the host's block boundaries or standalone test window sizes.
            t4_.process(&opticalLeft,stereo?&opticalRight:nullptr,1);
            const double tubeLeft=follower_.process(voltageGain_.process(opticalLeft));
            const double tubeRight=stereo ?
                follower_.process(voltageGain_.process(opticalRight)):tubeLeft;
            const double wetLeft=magneticEnabled_ ?
                magneticOutputLeft_.process(tubeLeft):outputLeft_.process(tubeLeft);
            const double wetRight=stereo ? (magneticEnabled_ ?
                magneticOutputRight_.process(tubeRight):outputRight_.process(tubeRight)):wetLeft;
            currentGain_=controlCoeff_*currentGain_+
                          (1-controlCoeff_)*targetGain;
            currentMix_=controlCoeff_*currentMix_+
                         (1-controlCoeff_)*parameters_.mix;
            currentActive_=controlCoeff_*currentActive_+
                            (1-controlCoeff_)*targetActive;
            const double wet=currentMix_*currentActive_;
            double alignedL=dryLeft,alignedR=dryRight;
            if(magneticEnabled_) {
                dryDelayL_[dryDelayIndex_]=dryLeft;
                dryDelayR_[dryDelayIndex_]=dryRight;
                const auto read=(dryDelayIndex_+dryDelayL_.size()-32)%dryDelayL_.size();
                alignedL=dryDelayL_[read];
                alignedR=dryDelayR_[read];
                dryDelayIndex_=(dryDelayIndex_+1)%dryDelayL_.size();
            }
            left[i]=static_cast<float>(alignedL*(1-wet)+wetLeft*currentGain_*wet);
            if(stereo)
                right[i]=static_cast<float>(alignedR*(1-wet)+wetRight*currentGain_*wet);
        }
    }
    double getGainReductionDb() const noexcept{return t4_.getGainReductionDb();}
    double getT4Exposure() const noexcept{return t4_.getOpticalExposure();}
    int latencySamples() const noexcept{return magneticEnabled_?32:0;}
private:
    static double sane(double x,double low,double high,double fallback) noexcept {
        return std::isfinite(x)?std::clamp(x,low,high):fallback;
    }
    static double gain(double db) noexcept {return std::exp(db*0.11512925464970229);}
    ApexT4LevelerResearchCore t4_;
    ApexTransformerResearchCore inputLeft_,inputRight_;
    ApexTransformerResearchCore outputLeft_,outputRight_;
    ApexMagneticTransformerResearchCore magneticInputLeft_,magneticInputRight_;
    ApexMagneticTransformerResearchCore magneticOutputLeft_,magneticOutputRight_;
    std::array<double,33> dryDelayL_{},dryDelayR_{};
    std::size_t dryDelayIndex_=0;
    bool magneticEnabled_=false;
    ApexTriodeResearchCore voltageGain_,follower_;
    ApexOptoCircuitResearchParameters parameters_{};
    double sr_=48000,controlCoeff_=0;
    double currentMix_=1,currentGain_=1,currentActive_=1;
    bool started_=false;
};
} // namespace APEX::Compressor
