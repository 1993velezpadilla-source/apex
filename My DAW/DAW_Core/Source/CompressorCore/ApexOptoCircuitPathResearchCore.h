#pragma once
// APEX OPTO 2A — uncalibrated FULL AUDIO-PATH RESEARCH HARNESS.
// INPUT linear transformer -> T4 shunt -> 12AX7 plate-stage approximation ->
// 12BH7 cathode-follower approximation -> OUTPUT linear transformer.
// Optical sidechain is pre-gain-cell, as in the referenced historic schematic.
// This is NOT a shipped DAW insert or measured Teletronix/UAD circuit.
// Driver tubes, transformer hysteresis, tube capacitance, alias protection,
// correct loading, real power-rail feedback and reference fitting remain TODO.
// Does not allocate, lock, solve nonlinear systems, or look ahead in callback.
#include "ApexT4LevelerResearchCore.h"
#include "ApexAnalogCircuitResearchCore.h"
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
    void prepare(double sr) noexcept {
        sr_=std::isfinite(sr)&&sr>=8000&&sr<=768000?sr:48000;
        controlCoeff_=std::exp(-1.0/(sr_*0.005));
        inputLeft_.prepare(sr_);
        inputRight_.prepare(sr_);
        outputLeft_.prepare(sr_);
        outputRight_.prepare(sr_);
        voltageGain_.prepare(ApexTriodeCircuit::ax7VoltageStage());
        follower_.prepare(ApexTriodeCircuit::bh7CathodeFollower());
        t4_.prepare(sr_);
        setParameters(parameters_);
        reset();
    }
    void reset() noexcept {
        inputLeft_.reset();inputRight_.reset();
        outputLeft_.reset();outputRight_.reset();
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
            float opticalLeft=static_cast<float>(inputLeft_.process(dryLeft));
            float opticalRight=static_cast<float>(stereo?
                                        inputRight_.process(dryRight):opticalLeft);
            // Single sample per tick: T4 memory remains continuous regardless
            // of the host's block boundaries or standalone test window sizes.
            t4_.process(&opticalLeft,stereo?&opticalRight:nullptr,1);
            const double wetLeft=outputLeft_.process(
                follower_.process(voltageGain_.process(opticalLeft)));
            const double wetRight=stereo?outputRight_.process(
                follower_.process(voltageGain_.process(opticalRight))):wetLeft;
            currentGain_=controlCoeff_*currentGain_+
                          (1-controlCoeff_)*targetGain;
            currentMix_=controlCoeff_*currentMix_+
                         (1-controlCoeff_)*parameters_.mix;
            currentActive_=controlCoeff_*currentActive_+
                            (1-controlCoeff_)*targetActive;
            const double wet=currentMix_*currentActive_;
            left[i]=static_cast<float>(dryLeft*(1-wet)+wetLeft*currentGain_*wet);
            if(stereo)
                right[i]=static_cast<float>(dryRight*(1-wet)+wetRight*currentGain_*wet);
        }
    }
    double getGainReductionDb() const noexcept{return t4_.getGainReductionDb();}
    double getT4Exposure() const noexcept{return t4_.getOpticalExposure();}
    static constexpr int latencySamples() noexcept{return 0;}
private:
    static double sane(double x,double low,double high,double fallback) noexcept {
        return std::isfinite(x)?std::clamp(x,low,high):fallback;
    }
    static double gain(double db) noexcept {return std::exp(db*0.11512925464970229);}
    ApexT4LevelerResearchCore t4_;
    ApexTransformerResearchCore inputLeft_,inputRight_;
    ApexTransformerResearchCore outputLeft_,outputRight_;
    ApexTriodeResearchCore voltageGain_,follower_;
    ApexOptoCircuitResearchParameters parameters_{};
    double sr_=48000,controlCoeff_=0;
    double currentMix_=1,currentGain_=1,currentActive_=1;
    bool started_=false;
};
} // namespace APEX::Compressor
