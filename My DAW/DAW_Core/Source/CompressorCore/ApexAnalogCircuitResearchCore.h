#pragma once
// APEX Opto 2A: independent, provisional circuit-component models.
// NOT a calibrated Teletronix/UA circuit, UAD DSP, nor complete SPICE netlist.
// A bounded 3/2-power triode plate/current equation and resistive cathode
// feedback are PRECOMPUTED into a fixed LUT in prepare(). Audio thread uses
// interpolation only. The transformers currently emulate LINEAR magnetizing
// and leakage poles: no hysteresis, saturation, oversampling or claim of 1:1.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace APEX::Compressor {
struct ApexTriodeCircuit {
    double mu=100.0, currentScale=0.0006, plateSupplyVolts=250.0;
    double plateLoadOhms=100000.0, cathodeOhms=1500.0;
    double gridDcVolts=0.0, gridVoltsPeak=0.35;
    bool cathodeFollower=false;
    static ApexTriodeCircuit ax7VoltageStage() noexcept { return {}; }
    static ApexTriodeCircuit bh7CathodeFollower() noexcept {
        ApexTriodeCircuit c;
        c.mu=17; c.currentScale=0.00062; c.plateLoadOhms=0;
        c.cathodeOhms=15000; c.gridDcVolts=95; c.gridVoltsPeak=10;
        c.cathodeFollower=true; return c;
    }
};
class ApexTriodeResearchCore final {
public:
    void prepare(ApexTriodeCircuit c) noexcept {
        c_=sanitize(c);
        dcOutput_=dcOutput(c_.gridDcVolts);
        const double step=std::max(1.e-6,c_.gridVoltsPeak*1.e-3);
        const double slope=(dcOutput(c_.gridDcVolts+step)-
                            dcOutput(c_.gridDcVolts-step))/(2*step);
        slope_=std::abs(slope)>1.e-12?slope:1.0;
        for(std::size_t i=0;i<lut_.size();++i) {
            const double x=-1.0+2.0*i/(lut_.size()-1);
            lut_[i]=(dcOutput(c_.gridDcVolts+x*c_.gridVoltsPeak)-dcOutput_)/
                    (slope_*c_.gridVoltsPeak);
        }
    }
    double process(double input) const noexcept {
        if(!std::isfinite(input))return 0;
        const double index=(std::clamp(input,-1.0,1.0)+1)*0.5*(lut_.size()-1);
        const auto left=static_cast<std::size_t>(index);
        const auto right=std::min(left+1,lut_.size()-1);
        return lut_[left]+(index-left)*(lut_[right]-lut_[left]);
    }
    double plateCurrentAmps(double grid) const noexcept {
        return std::isfinite(grid)?solve(grid):0;
    }
    double dcOutputVolts() const noexcept {return dcOutput_;}
private:
    static double sane(double x,double lo,double hi,double fallback) noexcept {
        return std::isfinite(x)?std::clamp(x,lo,hi):fallback;
    }
    static ApexTriodeCircuit sanitize(ApexTriodeCircuit c) noexcept {
        c.mu=sane(c.mu,1,1000,100);
        c.currentScale=sane(c.currentScale,1.e-10,0.1,0.0006);
        c.plateSupplyVolts=sane(c.plateSupplyVolts,20,1000,250);
        c.plateLoadOhms=sane(c.plateLoadOhms,0,1.e7,100000);
        c.cathodeOhms=sane(c.cathodeOhms,1,1.e7,1500);
        c.gridDcVolts=sane(c.gridDcVolts,-500,500,0);
        c.gridVoltsPeak=sane(c.gridVoltsPeak,1.e-5,100,0.35);
        return c;
    }
    double tubeCurrent(double grid,double current) const noexcept {
        const double cathode=current*c_.cathodeOhms;
        const double plate=std::max(cathode,c_.plateSupplyVolts-
                                          current*c_.plateLoadOhms);
        const double over=grid-cathode+(plate-cathode)/c_.mu;
        return over>0?c_.currentScale*over*std::sqrt(over):0.0;
    }
    double solve(double grid) const noexcept {
        double low=0;
        double high=std::min(.1,c_.plateSupplyVolts/
                                std::max(1.0,c_.plateLoadOhms+c_.cathodeOhms));
        for(int n=0;n<45;++n) {
            const double mid=(low+high)*0.5;
            if(tubeCurrent(grid,mid)>mid)low=mid;else high=mid;
        }
        return (low+high)*0.5;
    }
    double dcOutput(double grid) const noexcept {
        const double current=solve(grid);
        return c_.cathodeFollower?current*c_.cathodeOhms:
                                  c_.plateSupplyVolts-current*c_.plateLoadOhms;
    }
    ApexTriodeCircuit c_{};
    std::array<double,4097> lut_ {};
    double dcOutput_=0,slope_=1;
};
struct ApexTransformerCircuit {
    double sourceOhms=600, loadOhms=600, copperOhms=35;
    double magnetizingHenries=5, leakageHenries=0.005, turnsRatio=1;
};
class ApexTransformerResearchCore final {
public:
    void prepare(double sr,ApexTransformerCircuit c={}) noexcept {
        sr_=std::isfinite(sr)&&sr>=8000&&sr<=768000?sr:48000;
        const auto safe=[](double x,double lo,double hi,double fallback){
            return std::isfinite(x)?std::clamp(x,lo,hi):fallback;
        };
        c.sourceOhms=safe(c.sourceOhms,1,1.e6,600);
        c.loadOhms=safe(c.loadOhms,1,1.e6,600);
        c.copperOhms=safe(c.copperOhms,0,1.e6,35);
        c.magnetizingHenries=safe(c.magnetizingHenries,1.e-4,1.e5,5);
        c.leakageHenries=safe(c.leakageHenries,1.e-8,100,0.005);
        c.turnsRatio=safe(c.turnsRatio,0.01,100,1);
        const double fLow=(c.sourceOhms+c.copperOhms)/(2*pi*c.magnetizingHenries);
        const double fHigh=c.loadOhms/(2*pi*c.leakageHenries);
        lowCoeff_=std::exp(-2*pi*fLow/sr_);
        highCoeff_=std::exp(-2*pi*fHigh/sr_);
        const double insertion=c.turnsRatio*c.loadOhms/
                          (c.loadOhms+c.sourceOhms+c.copperOhms);
        const double hp=1/std::sqrt(1+(fLow/1000)*(fLow/1000));
        const double lp=1/std::sqrt(1+(1000/fHigh)*(1000/fHigh));
        const double reference=insertion*hp*lp;
        outputScale_=insertion/std::max(1.e-12,reference);
        reset();
    }
    void reset() noexcept {lowState_=highState_=0;}
    double process(double input) noexcept {
        input=std::isfinite(input)?std::clamp(input,-16.0,16.0):0.0;
        lowState_=lowCoeff_*lowState_+(1-lowCoeff_)*input;
        highState_=highCoeff_*highState_+
                   (1-highCoeff_)*(input-lowState_);
        return highState_*outputScale_;
    }
    static constexpr int latencySamples() noexcept {return 0;}
private:
    static constexpr double pi=3.14159265358979323846;
    double sr_=48000,lowCoeff_=0,highCoeff_=0;
    double lowState_=0,highState_=0,outputScale_=1;
};
} // namespace APEX::Compressor
