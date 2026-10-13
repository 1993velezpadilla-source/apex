#pragma once
// APEX Opto 2A analogue-style VU indication (not an LA-2A circuit model).
// Display-only IEC-like 300 ms step ballistics using two cascaded smoothers.
// No audio path effect, allocations, locks, or lookahead.
#include <algorithm>
#include <cmath>

namespace APEX::Compressor
{
class OptoVuBallisticsCore final
{
public:
    void prepare(double sampleRate) noexcept
    {
        const auto sr=(std::isfinite(sampleRate) && sampleRate>=8000.0)
            ? sampleRate : 48000.0;
        // 2-pole step response: 1 - (1+t/tau)exp(-t/tau).
        // A 47 ms time constant reaches about 98.7% after 300 ms.
        coefficient_=1.0-std::exp(-1.0/(sr*0.047));
        reset();
    }

    void reset() noexcept { stage1_=stage2_=0.0; }

    void process(const float* left,const float* right,int numSamples) noexcept
    {
        if(left==nullptr || numSamples<=0)return;
        const bool stereo=right!=nullptr && right!=left;
        for(int n=0;n<numSamples;++n)
        {
            auto sample=static_cast<double>(left[n]);
            if(!std::isfinite(sample))sample=0.0;
            double magnitude=std::abs(sample);
            if(stereo)
            {
                auto r=static_cast<double>(right[n]);
                if(!std::isfinite(r))r=0.0;
                magnitude=0.5*(magnitude+std::abs(r));
            }
            // Average-rectified moving-coil equivalent; shape independent
            // of host block boundaries. No clipping or gain added to audio.
            magnitude=std::min(magnitude,64.0);
            stage1_+=coefficient_*(magnitude-stage1_);
            stage2_+=coefficient_*(stage1_-stage2_);
        }
    }

    double getIndicatedRmsDbfs() const noexcept
    {
        // Absolute-sine average --> sine RMS calibration factor.
        // A real VU indicates rectified mean, not true RMS; this is a
        // reference level for the modern +4 / +10 digital display modes.
        constexpr double meanAbsSineToRms=1.1107207345395915;
        return 20.0*std::log10(std::max(1.0e-12,stage2_*meanAbsSineToRms));
    }

    double getIndicatedAmplitude() const noexcept { return stage2_; }

private:
    double coefficient_=0.0;
    double stage1_=0.0,stage2_=0.0;
};
} // namespace APEX::Compressor
