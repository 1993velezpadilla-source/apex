#pragma once

// APEX Precision C — independent C++17 laboratory for a REAL bandlimited
// 2x character-saturation path. No JUCE, heap allocations, locks, or state
// sharing with the audio host. This IS NOT wired into the shipping compressor:
// first prove alias rejection, phase/delay, CPU and parallel wet/dry routing.
// In particular, never show an enabled "2x" UI control before host PDC is
// correctly implemented for this processing mode.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace APEX::PrecisionC3
{
class CharacterOversampling2x final
{
public:
    static constexpr int kTaps = 97;
    // 2 FIR filters with (97-1)/2 samples' group delay each at 2x
    // -> (48+48)/2 = 48 ORIGINAL-rate samples at the even decimation phase.
    static constexpr int kLatencySamples = 48;

    void prepare() noexcept
    {
        const double pi = 3.1415926535897932384626433832795;
        constexpr double cutoff = 0.22; // cycles / high-rate sample; below fs/4
        constexpr double beta = 8.0;   // Kaiser ~80 dB stopband
        const double windowNormalizer = bessel0(beta);
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const double offset = i - (kTaps - 1) * 0.5;
            const double sinc = offset == 0.0
                ? 2.0 * cutoff
                : std::sin(2.0 * pi * cutoff * offset) / (pi * offset);
            const double fraction = 2.0 * i / (kTaps - 1) - 1.0;
            const double window = bessel0(beta * std::sqrt(
                std::max(0.0,1.0-fraction*fraction))) / windowNormalizer;
            coeff_[static_cast<std::size_t>(i)] = sinc * window;
            sum += sinc * window;
        }
        // Unity DC gain for BOTH FIR stages. Interpolation stage multiplies
        // by 2 because the inserted-zero train halves its average power.
        for (auto& c : coeff_) c /= sum;
        reset();
    }

    void reset() noexcept
    {
        upHistory_.fill(0.0);
        downHistory_.fill(0.0);
        upWrite_ = downWrite_ = 0;
    }

    // One ORIGINAL-rate input sample and one delayed output. This version
    // supports clean linear verification or a continuous tanh waveshaper.
    // Both modes have the same fixed 48-sample PDC. The lab deliberately
    // restricts itself to this defined nonlinear shape; style matching to
    // any third-party compressor remains wholly unproven.
    double process(double input, double drive, bool nonlinear) noexcept
    {
        if (!std::isfinite(drive)) drive = 1.0;
        drive = std::clamp(drive,0.01,16.0);
        return processCustom(input,[drive,nonlinear](double x) noexcept {
            return nonlinear ? std::tanh(x*drive)/std::tanh(drive) : x;
        });
    }

    // The hosted Precision C character stage uses the same distinct
    // modes as its old single-rate Core::colour(), but at 2× rate.
    // Filtering is applied BEFORE AND AFTER these nonlinear operations.
    // A mode-0 result is still delayed/filtered; the parent Core can
    // use an exact 48-sample delay tap when character is disabled.
    double processColour(double input,int mode,double drivePercent) noexcept
    {
        if (!std::isfinite(drivePercent))drivePercent=0.0;
        const double amount=std::clamp(drivePercent,0.0,100.0)*0.01;
        return processCustom(input,[mode,amount](double sample) noexcept {
            if(mode==0 || amount<=0.0)return sample;
            const double x=sample*(1.0+2.0*amount);
            if(mode==1)
                return (1.0-amount)*sample+amount*std::tanh(x)/(1.0+amount);
            if(mode==2)
                return (1.0-amount)*sample+amount*x/(1.0+std::abs(x));
            return (1.0-amount)*sample+
                amount*(x-(x*x*x)/(3.0+x*x));
        });
    }

private:
    template<class Shaper>
    double processCustom(double input,Shaper&& shape) noexcept
    {
        if (!std::isfinite(input)) input = 0.0;
        input = std::clamp(input,-32.0,32.0);
        double selected = 0.0;
        for (int phase = 0; phase < 2; ++phase)
        {
            push(upHistory_,upWrite_,phase == 0 ? input : 0.0);
            const double reconstructed = 2.0 * convolve(upHistory_,upWrite_);
            const double shaped = shape(reconstructed);
            push(downHistory_,downWrite_,shaped);
            const double lowpassed = convolve(downHistory_,downWrite_);
            // Even high-rate phase makes the cascade's *integer* group delay
            // exactly 48 original-rate samples (including empty startup).
            if (phase == 0) selected = lowpassed;
        }
        return std::isfinite(selected)
            ? std::clamp(selected,-32.0,32.0) : 0.0;
    }

    static double bessel0(double x) noexcept
    {
        // Absolutely convergent I_0 series, finite for x <= 8.
        const double y=(x*x)*0.25;
        double sum=1.0,term=1.0;
        for(int n=1;n<=24;++n)
        {
            term*=y / (n*n);
            sum+=term;
            if (term < 1.0e-16 * sum) break;
        }
        return sum;
    }

    static void push(std::array<double,kTaps>& ring,
                     std::size_t& write,double sample) noexcept
    {
        ring[write]=sample;
        write=(write+1)%kTaps;
    }
    double convolve(const std::array<double,kTaps>& ring,
                    std::size_t write) const noexcept
    {
        double sum=0.0;
        for(int i=0;i<kTaps;++i)
        {
            const auto idx=(write+kTaps-1-static_cast<std::size_t>(i))%kTaps;
            sum+=ring[idx]*coeff_[static_cast<std::size_t>(i)];
        }
        return sum;
    }

    std::array<double,kTaps> coeff_ {};
    std::array<double,kTaps> upHistory_ {};
    std::array<double,kTaps> downHistory_ {};
    std::size_t upWrite_ = 0, downWrite_ = 0;
};
} // namespace APEX::PrecisionC3
