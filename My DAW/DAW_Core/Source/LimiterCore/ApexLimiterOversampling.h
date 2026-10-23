#pragma once
// APEX Bubblegum Limiter — experimental real AUDIO 2x..32x rate conversion.
// Unlike the ISP detector, this actually generates and processes additional
// audio frames, then filters and decimates before returning to the host rate.
// ORIGINAL portable C++17 DSP; neither an ITU-certified true-peak engine nor
// a production-ready selectable mastering mode. Isolated until QA finishes.
//
// Realtime guarantees: prepare() allocates every buffer and initializes all
// coefficients. process() only reads/writes bounded preallocated memory;
// no locks, I/O, resizing, callbacks to the OS or other host API calls.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace APEX { namespace Limiter {

class AudioRateConverter final
{
public:
    using ProcessHighRateFn = void(*)(void*,float*,float*,int) noexcept;
    static constexpr int kMaxFactor=32;
    static constexpr int kMaxStages=5;

    void prepare(int factor,int maxBaseFrames)
    {
        // prepare() must not be invoked from the audio processing thread.
        factor_=std::max(1,std::min(kMaxFactor,factor));
        if((factor_ & (factor_-1))!=0)factor_=1;
        stages_=0;
        for(int f=factor_;f>1;f>>=1)++stages_;
        baseChunk_=std::max(1,std::min(512,maxBaseFrames));
        prepareFilter();
        for(auto& stage:stagesState_)stage.reset();
        for(int index=0;index<=kMaxStages;++index)
        {
            // Each 2x stage writes to its own preallocated float buffers.
            const int levelFrames=baseChunk_*(1<<index);
            for(auto& channel:work_[static_cast<size_t>(index)])
                channel.assign(static_cast<size_t>(levelFrames),0.0f);
        }
    }
    void reset() noexcept
    {
        for(auto& stage:stagesState_)stage.reset();
    }
    int factor() const noexcept {return factor_;}
    int stages() const noexcept {return stages_;}
    int maxChunkFrames() const noexcept {return baseChunk_;}

    // Approximate FIR roundtrip group delay at base rate, excluding the
    // high-rate processor. An exact integer PDC must be established by the
    // host integration gauntlet; do not publish this as certified latency.
    double nominalFilterDelayFrames() const noexcept
    {
        double delay=0.0;
        for(int level=0;level<stages_;++level)
            delay+=static_cast<double>(kTaps-1)/(1<<level)/2.0;
        return delay;
    }

    void process(float* left,float* right,int frames,void* ctx,
                 ProcessHighRateFn callback) noexcept
    {
        if(!left || frames<=0 || !callback || baseChunk_<1)return;
        if(factor_==1)
        {
            callback(ctx,left,right,frames);
            return;
        }
        const bool stereo=right && right!=left;
        for(int offset=0;offset<frames;)
        {
            const int count=std::min(baseChunk_,frames-offset);
            std::copy_n(left+offset,count,work_[0][0].data());
            if(stereo)std::copy_n(right+offset,count,work_[0][1].data());

            // Interpolated samples feed the actual high-rate DSP callback.
            for(int level=0;level<stages_;++level)
            {
                auto& state=stagesState_[static_cast<size_t>(level)];
                for(int ch=0;ch<(stereo?2:1);++ch)
                    interpolate(state.up[static_cast<size_t>(ch)],
                        work_[static_cast<size_t>(level)][static_cast<size_t>(ch)].data(),
                        work_[static_cast<size_t>(level+1)][static_cast<size_t>(ch)].data(),
                        count*(1<<level));
            }
            const int highCount=count*factor_;
            auto& high=work_[static_cast<size_t>(stages_)];
            callback(ctx,high[0].data(),stereo?high[1].data():nullptr,highCount);

            // True filtering *after* the DSP prevents its ultrasonic content
            // from aliasing when decimating. Stateful across host callbacks.
            for(int level=stages_-1;level>=0;--level)
            {
                auto& state=stagesState_[static_cast<size_t>(level)];
                for(int ch=0;ch<(stereo?2:1);++ch)
                    decimate(state.down[static_cast<size_t>(ch)],
                        work_[static_cast<size_t>(level+1)][static_cast<size_t>(ch)].data(),
                        work_[static_cast<size_t>(level)][static_cast<size_t>(ch)].data(),
                        count*(1<<level));
            }
            std::copy_n(work_[0][0].data(),count,left+offset);
            if(stereo)std::copy_n(work_[0][1].data(),count,right+offset);
            offset+=count;
        }
    }
private:
    static constexpr int kTaps=63;
    static constexpr int kHistory=64;
    static constexpr int kPhaseTaps=32;
    static constexpr int kCentreTap=(kTaps-1)/2; // 31
    static constexpr int kOddCentreTap=(kCentreTap-1)/2; // 15
    struct UpHistory
    {
        std::array<double,kHistory> samples{};
        int cursor=0;
        void reset() noexcept {samples.fill(0.0);cursor=0;}
    };
    struct DownHistory
    {
        std::array<double,kHistory> samples{};
        int cursor=0,phase=0;
        void reset() noexcept {samples.fill(0.0);cursor=0;phase=0;}
    };
    struct Stage
    {
        std::array<UpHistory,2> up{};
        std::array<DownHistory,2> down{};
        void reset() noexcept
        {
            for(auto& c:up)c.reset();
            for(auto& c:down)c.reset();
        }
    };
    void prepareFilter() noexcept
    {
        // Real windowed-sinc antialias LPF with a halfband cutoff (0.25
        // high-rate cycles/sample). 63-tap Blackman smooths the transition.
        // DC is normalized independently for the two interpolation phases.
        constexpr double pi=3.14159265358979323846;
        double sum=0.0;
        for(int k=0;k<kTaps;++k)
        {
            const double t=static_cast<double>(k)-(kTaps-1)/2.0;
            const double base=std::abs(t)<1.0e-12 ? 0.5
                         :std::sin(pi*0.5*t)/(pi*t);
            const double blackman=0.42-0.5*std::cos(2*pi*k/(kTaps-1))
                                 +0.08*std::cos(4*pi*k/(kTaps-1));
            lowpass_[static_cast<size_t>(k)]=base*blackman;
            sum+=lowpass_[static_cast<size_t>(k)];
        }
        if(std::abs(sum)<1.0e-12)sum=1.0;
        // Ideal halfband has every odd tap zero except the centre.
        // Remove tiny floating-point sin(pi*N) residuals so the realtime
        // decoder can skip 30 provably-zero multiplies per output sample.
        for(auto& v:lowpass_)
        {
            v/=sum;
            if(std::abs(v)<1.0e-14)v=0.0;
        }
        for(int phase=0;phase<2;++phase)
        {
            double phaseSum=0.0;
            for(int k=0;2*k+phase<kTaps;++k)
                phaseSum+=lowpass_[static_cast<size_t>(2*k+phase)];
            if(std::abs(phaseSum)<1.0e-12)phaseSum=1.0;
            for(int k=0;k<kPhaseTaps;++k)
            {
                const int tap=2*k+phase;
                polyphase_[static_cast<size_t>(phase)][static_cast<size_t>(k)]
                    =tap<kTaps ? lowpass_[static_cast<size_t>(tap)]/phaseSum:0.0;
            }
        }
    }
    void interpolate(UpHistory& state,const float* src,float* dst,int count) noexcept
    {
        for(int i=0;i<count;++i)
        {
            state.samples[static_cast<size_t>(state.cursor)]=src[i];
            // Even polyphase: all 32 meaningful sinc taps.
            double even=0.0;
            const auto& weights=polyphase_[0];
            for(int tap=0;tap<kPhaseTaps;++tap)
            {
                const int index=(state.cursor+kHistory-tap)%kHistory;
                even+=state.samples[static_cast<size_t>(index)]
                        *weights[static_cast<size_t>(tap)];
            }
            dst[2*i]=static_cast<float>(even);
            // Odd polyphase: only k=15 (the 63-tap halfband centre) is
            // nonzero after DC normalisation. An EXACT delayed sample,
            // with no kernel loop or frame-dependent interpolation drift.
            dst[2*i+1]=static_cast<float>(
                state.samples[static_cast<size_t>(
                    (state.cursor+kHistory-kOddCentreTap)%kHistory)]);
            state.cursor=(state.cursor+1)%kHistory;
        }
    }
    void decimate(DownHistory& state,const float* src,float* dst,int outputCount) noexcept
    {
        int output=0;
        for(int i=0;i<2*outputCount;++i)
        {
            state.samples[static_cast<size_t>(state.cursor)]=src[i];
            if(state.phase==1)
            {
                double value=0.0;
                // Exact halfband sparse FIR: 32 even taps plus one centre.
                for(int tap=0;tap<kTaps;tap+=2)
                {
                    const int index=(state.cursor+kHistory-tap)%kHistory;
                    value+=state.samples[static_cast<size_t>(index)]
                            *lowpass_[static_cast<size_t>(tap)];
                }
                value+=state.samples[static_cast<size_t>(
                            (state.cursor+kHistory-kCentreTap)%kHistory)]
                           *lowpass_[static_cast<size_t>(kCentreTap)];
                dst[output++]=static_cast<float>(value);
            }
            state.phase^=1;
            state.cursor=(state.cursor+1)%kHistory;
        }
    }
    int factor_=1,stages_=0,baseChunk_=128;
    std::array<double,kTaps> lowpass_{};
    std::array<std::array<double,kPhaseTaps>,2> polyphase_{};
    std::array<Stage,kMaxStages> stagesState_{};
    std::array<std::array<std::vector<float>,2>,kMaxStages+1> work_{};
};

}} // namespace APEX::Limiter
