#pragma once
// APEX Opto 2A research: simplified 6AQ5-like beam-power pentode -> EL driver.
// NOT a validated Teletronix circuit, measured 6AQ5 plate curves, or UA DSP.
// An independently designed DC pentode/current + load + cathode feedback
// hypothesis, followed by a rectified/smoothed EL drive voltage. The actual
// LA-2A electrical AC drive, transformers and 6AQ5 screen supply require
// schematic-level validation and calibrated measurements.
//
// Expensive electrical solves execute only in prepare() to fill a 2049-point
// LUT. tick() is causal, fixed-work, C++17, no allocations/locks/solvers.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace APEX::Compressor {
struct Apex6AQ5DriverCircuit
{
    // Provisional engineering parameters, NOT asserted original BOM values.
    double plateSupplyVolts=250.0, screenSupplyVolts=250.0;
    double cathodeOhms=220.0, plateLoadOhms=10000.0;
    double gridBiasVolts=-2.0, gridSwingVolts=4.0;
    double perveance=0.00035;  // amps/volts^(3/2), fitted later
    double screenMu=80.0;      // effective screen control of space charge
    double plateKneeVolts=28.0;
    double elPrimaryOhms=2400.0; // simplified EL drive transfer impedance
    double elTurnOnVolts=0.25, elFullScaleVolts=5.0;
    double rectifierAttackSec=0.0007, rectifierReleaseSec=0.002;
};
class Apex6AQ5DriverResearchCore final
{
public:
    void prepare(double sampleRate,Apex6AQ5DriverCircuit c={}) noexcept
    {
        sampleRate_=std::isfinite(sampleRate)&&sampleRate>=8000&&sampleRate<=768000
                  ? sampleRate:48000.0;
        c_=sanitize(c);
        baseCurrent_=solve(c_.gridBiasVolts);
        for(std::size_t k=0;k<table_.size();++k)
        {
            const double position=static_cast<double>(k)/(table_.size()-1);
            const double current=solve(c_.gridBiasVolts+
                                       position*c_.gridSwingVolts);
            // Plate AC delta to EL transformer input. Approximation only:
            // real waveforms use output transformer, AC drive and feedback.
            const double volts=std::max(0.0,current-baseCurrent_)*
                               c_.elPrimaryOhms;
            const double range=c_.elFullScaleVolts-c_.elTurnOnVolts;
            table_[k]=std::clamp((volts-c_.elTurnOnVolts)/range,0.0,1.0);
        }
        attackCoef_=pole(c_.rectifierAttackSec);
        releaseCoef_=pole(c_.rectifierReleaseSec);
        reset();
    }
    void reset() noexcept {elEnvelope_=0.0;}
    // input = 0..1 normalized drive from the sidechain, NOT audio input.
    double tick(double input) noexcept
    {
        if(!std::isfinite(input))input=0;
        const double index=std::clamp(input,0.0,1.0)*(table_.size()-1);
        const auto low=static_cast<std::size_t>(index);
        const auto high=std::min(low+1,table_.size()-1);
        const double fraction=index-low;
        const double target=table_[low]+fraction*(table_[high]-table_[low]);
        const double coeff=target>elEnvelope_?attackCoef_:releaseCoef_;
        elEnvelope_=coeff*elEnvelope_+(1-coeff)*target;
        return elEnvelope_;
    }
    double elLevel() const noexcept {return elEnvelope_;}
    double idlePlateCurrentAmps() const noexcept{return baseCurrent_;}
    double currentAtGridVolts(double volts) const noexcept {
        return std::isfinite(volts)?solve(volts):0.0;
    }
    static constexpr int latencySamples() noexcept {return 0;}
private:
    static double sane(double x,double low,double high,double fallback) noexcept {
        return std::isfinite(x)?std::clamp(x,low,high):fallback;
    }
    static Apex6AQ5DriverCircuit sanitize(Apex6AQ5DriverCircuit c) noexcept
    {
        c.plateSupplyVolts=sane(c.plateSupplyVolts,20,800,250);
        c.screenSupplyVolts=sane(c.screenSupplyVolts,0,600,250);
        c.cathodeOhms=sane(c.cathodeOhms,1,1.e6,220);
        c.plateLoadOhms=sane(c.plateLoadOhms,1,1.e7,10000);
        c.gridBiasVolts=sane(c.gridBiasVolts,-100,0,-2);
        c.gridSwingVolts=sane(c.gridSwingVolts,0.001,100,4);
        c.perveance=sane(c.perveance,1.e-9,.1,.00035);
        c.screenMu=sane(c.screenMu,2,1000,80);
        c.plateKneeVolts=sane(c.plateKneeVolts,.01,300,28);
        c.elPrimaryOhms=sane(c.elPrimaryOhms,1,1.e7,2400);
        c.elTurnOnVolts=sane(c.elTurnOnVolts,0,100,0.25);
        c.elFullScaleVolts=sane(c.elFullScaleVolts,
                                 c.elTurnOnVolts+0.01,500,
                                 std::max(5.0,c.elTurnOnVolts+1.0));
        c.rectifierAttackSec=sane(c.rectifierAttackSec,.00001,1,.0007);
        c.rectifierReleaseSec=sane(c.rectifierReleaseSec,.00001,1,.002);
        return c;
    }
    double pole(double seconds) const noexcept {
        return std::exp(-1.0/(sampleRate_*seconds));
    }
    double currentLaw(double gridVolts,double current) const noexcept {
        const double cathode=std::max(0.0,current*c_.cathodeOhms);
        const double plate=std::max(0.0,c_.plateSupplyVolts-
                                             current*c_.plateLoadOhms-cathode);
        const double screen=std::max(0.0,c_.screenSupplyVolts-cathode);
        const double effective=gridVolts-cathode+screen/c_.screenMu;
        if(effective<=0 || plate<=0)return 0;
        const double softKnee=1.0-std::exp(-plate/c_.plateKneeVolts);
        return c_.perveance*effective*std::sqrt(effective)*softKnee;
    }
    double solve(double gridVolts) const noexcept
    {
        double low=0.0;
        const double maxCurrent=c_.plateSupplyVolts/
                             (c_.plateLoadOhms+c_.cathodeOhms);
        double high=std::max(1.e-12,maxCurrent);
        for(int i=0;i<46;++i)
        {
            const double midpoint=(low+high)*.5;
            if(currentLaw(gridVolts,midpoint)>midpoint)low=midpoint;
            else high=midpoint;
        }
        return (low+high)*.5;
    }
    Apex6AQ5DriverCircuit c_{};
    std::array<double,2049> table_{};
    double sampleRate_=48000,attackCoef_=0,releaseCoef_=0;
    double baseCurrent_=0,elEnvelope_=0;
};
} // namespace APEX::Compressor
