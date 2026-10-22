#pragma once
// APEX-native stereo loudness summary: BS.1770 K-weighted, 400 ms blocks
// on 100 ms hops, absolute (-70 LUFS) and relative (-10 LU) integration gates.
// LRA uses gated 3-second observations and 10th/95th percentiles (0.1 LU bins).
// All allocations occur in prepare(); process() is realtime safe.
// Original implementation; no third-party algorithm/source copied.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace APEX { namespace Limiter {

class LoudnessSummary final
{
public:
    void prepare(double sampleRate)
    {
        rate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0 ? sampleRate : 48000.0;
        window400_ = std::max(1,static_cast<int>(std::lround(rate_*0.400)));
        window3s_  = std::max(window400_+1,static_cast<int>(std::lround(rate_*3.0)));
        hop_       = std::max(1,static_cast<int>(std::lround(rate_*0.100)));
        shortHop_  = std::max(1,static_cast<int>(std::lround(rate_)));
        ring_.assign(static_cast<size_t>(window3s_),0.0);
        for(int ch=0;ch<2;++ch)
        {
            filter_[ch][0].configure(rate_,true);
            filter_[ch][1].configure(rate_,false);
        }
        reset();
    }

    void reset() noexcept
    {
        std::fill(ring_.begin(),ring_.end(),0.0);
        for(auto& channel:filter_)
            for(auto& stage:channel)stage.z1=stage.z2=0.0;
        for(auto& bin:blocks_)bin=Bin{};
        shortBins_.fill(0);
        write_=0;
        samples_=0;
        sum400_=sum3s_=0.0;
        totalEnergy_=0.0;
        totalCount_=0;
        integrated_=-std::numeric_limits<float>::infinity();
        lra_=0.0f;
    }

    void process(const float* left,const float* right,int count) noexcept
    {
        if(!left || count<=0 || ring_.empty()) return;
        for(int i=0;i<count;++i)
        {
            double e=0.0;
            for(int ch=0;ch<(right?2:1);++ch)
            {
                const double sample=static_cast<double>(ch==0?left[i]:right[i]);
                const double input=std::isfinite(sample)?sample:0.0;
                const double a=filter_[ch][0].process(input);
                const double y=filter_[ch][1].process(a);
                e+=y*y;
            }
            if(!std::isfinite(e))e=0.0;
            const size_t old400=(write_+ring_.size()
                                 -static_cast<size_t>(window400_))%ring_.size();
            const double outgoing400=ring_[old400];
            const double outgoing3s=ring_[write_];
            sum400_=std::max(0.0,sum400_+e-outgoing400);
            sum3s_=std::max(0.0,sum3s_+e-outgoing3s);
            ring_[write_]=e;
            write_=(write_+1)%ring_.size();
            ++samples_;

            if(samples_>=static_cast<uint64_t>(window400_)
               && (samples_-static_cast<uint64_t>(window400_))%
                   static_cast<uint64_t>(hop_)==0)
            {
                const double mean=sum400_/window400_;
                const double loudness=toLufs(mean);
                if(loudness>kMinimumLufs)
                {
                    const int index=binFor(loudness);
                    blocks_[static_cast<size_t>(index)].energy+=mean;
                    ++blocks_[static_cast<size_t>(index)].count;
                    totalEnergy_+=mean;
                    ++totalCount_;
                }
                updateIntegrated();
            }
            if(samples_>=static_cast<uint64_t>(window3s_)
               && (samples_-static_cast<uint64_t>(window3s_))%
                   static_cast<uint64_t>(shortHop_)==0)
            {
                const double shortLufs=toLufs(sum3s_/window3s_);
                if(shortLufs>kMinimumLufs)
                    ++shortBins_[static_cast<size_t>(binFor(shortLufs))];
                updateRange();
            }
        }
    }

    float integratedLufs() const noexcept { return integrated_; }
    float loudnessRange() const noexcept { return lra_; }

private:
    static constexpr int kBins=1000;
    static constexpr double kMinimumLufs=-70.0;
    static constexpr double kBinWidth=0.1;
    struct Bin { double energy=0.0; uint64_t count=0; };
    struct Biquad
    {
        double b0=1.0,b1=0.0,b2=0.0,a1=0.0,a2=0.0,z1=0.0,z2=0.0;
        void configure(double fs,bool shelf) noexcept
        {
            if(std::abs(fs-48000.0)<1.0)
            {
                if(shelf)
                {
                    b0=1.53512485958697; b1=-2.69169618940638; b2=1.19839281085285;
                    a1=-1.69065929318241; a2=0.73248077421585;
                }
                else
                {
                    b0=1.0;b1=-2.0;b2=1.0;
                    a1=-1.99004745483398;a2=0.99007225036621;
                }
            }
            else
            {
                constexpr double pi=3.14159265358979323846;
                if(shelf)
                {
                    const double K=std::tan(pi*1681.974450955533/fs);
                    const double Vh=std::pow(10.0,3.99984385397/20.0);
                    const double Vb=std::sqrt(Vh);
                    const double Q=0.7071752369554193;
                    const double den=1.0+K/Q+K*K;
                    b0=(Vh+Vb*K/Q+K*K)/den;
                    b1=2.0*(K*K-Vh)/den;
                    b2=(Vh-Vb*K/Q+K*K)/den;
                    a1=2.0*(K*K-1.0)/den;
                    a2=(1.0-K/Q+K*K)/den;
                }
                else
                {
                    const double K=std::tan(pi*38.13547087602444/fs);
                    const double Q=0.5003270373238773;
                    const double den=1.0+K/Q+K*K;
                    b0=1.0;b1=-2.0;b2=1.0;
                    a1=2.0*(K*K-1.0)/den;
                    a2=(1.0-K/Q+K*K)/den;
                }
            }
            z1=z2=0.0;
        }
        double process(double x) noexcept
        {
            const double y=b0*x+z1;
            z1=b1*x-a1*y+z2;
            z2=b2*x-a2*y;
            return y;
        }
    };
    static double toLufs(double energy) noexcept
    {
        return energy>0.0 && std::isfinite(energy)
               ? -0.691+10.0*std::log10(energy)
               : -std::numeric_limits<double>::infinity();
    }
    static int binFor(double lufs) noexcept
    {
        return std::clamp(static_cast<int>(std::floor(
            (lufs-kMinimumLufs)/kBinWidth)),0,kBins-1);
    }
    void updateIntegrated() noexcept
    {
        if(totalCount_==0)
        {
            integrated_=-std::numeric_limits<float>::infinity();
            return;
        }
        const double gate=toLufs(totalEnergy_/static_cast<double>(totalCount_))-10.0;
        double gatedEnergy=0.0;
        uint64_t gatedCount=0;
        for(int i=0;i<kBins;++i)
        {
            const double binLow=kMinimumLufs+i*kBinWidth;
            if(binLow<=gate)continue;
            gatedEnergy+=blocks_[static_cast<size_t>(i)].energy;
            gatedCount+=blocks_[static_cast<size_t>(i)].count;
        }
        integrated_=gatedCount>0?static_cast<float>(toLufs(
            gatedEnergy/static_cast<double>(gatedCount)))
            :-std::numeric_limits<float>::infinity();
    }
    void updateRange() noexcept
    {
        if(!std::isfinite(integrated_)){lra_=0.0f;return;}
        // EBU Tech 3342 relative LRA gate is 20 LU below integrated level.
        const double gate=std::max(kMinimumLufs,
                                   static_cast<double>(integrated_)-20.0);
        uint64_t active=0;
        for(int i=0;i<kBins;++i)
            if(kMinimumLufs+i*kBinWidth>gate)
                active+=shortBins_[static_cast<size_t>(i)];
        if(active<2){lra_=0.0f;return;}
        const uint64_t lowRank=std::max<uint64_t>(1,
            static_cast<uint64_t>(std::ceil(active*0.10)));
        const uint64_t highRank=std::max<uint64_t>(1,
            static_cast<uint64_t>(std::ceil(active*0.95)));
        uint64_t observed=0;
        int low=-1,high=-1;
        for(int i=0;i<kBins;++i)
        {
            if(kMinimumLufs+i*kBinWidth<=gate)continue;
            observed+=shortBins_[static_cast<size_t>(i)];
            if(low<0 && observed>=lowRank)low=i;
            if(high<0 && observed>=highRank){high=i;break;}
        }
        lra_=low>=0 && high>=0
             ?static_cast<float>(std::max(0,high-low)*kBinWidth):0.0f;
    }
    double rate_=48000.0;
    int window400_=19200,window3s_=144000,hop_=4800,shortHop_=48000;
    std::array<std::array<Biquad,2>,2> filter_{};
    std::vector<double> ring_;
    size_t write_=0;
    uint64_t samples_=0,totalCount_=0;
    double sum400_=0.0,sum3s_=0.0,totalEnergy_=0.0;
    std::array<Bin,kBins> blocks_{};
    std::array<uint64_t,kBins> shortBins_{};
    float integrated_=-std::numeric_limits<float>::infinity();
    float lra_=0.0f;
};

}} // namespace APEX::Limiter
