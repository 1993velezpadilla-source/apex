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

    // extraHighRateDelay is an EXACT integer delay at the highest sample
    // rate. This compensates fractional host-frame FIR group delay with no
    // interpolation coloration or per-sample fractional delay filter.
    void prepare(int factor,int maxBaseFrames,int extraHighRateDelay=0)
    {
        // prepare() must not be invoked from the audio processing thread.
        factor_=std::max(1,std::min(kMaxFactor,factor));
        if((factor_ & (factor_-1))!=0)factor_=1;
        stages_=0;
        for(int f=factor_;f>1;f>>=1)++stages_;
        extraHighRateDelay_=std::clamp(extraHighRateDelay,0,kMaxHighRateDelay);
        if(factor_==1)extraHighRateDelay_=0;
        baseChunk_=std::max(1,std::min(512,maxBaseFrames));
        prepareFilter();
        for(auto& stage:stagesState_)stage.reset();
        for(auto& c:alignmentDelay_)c.reset();
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
        for(auto& c:alignmentDelay_)c.reset();
    }
    int factor() const noexcept {return factor_;}
    int stages() const noexcept {return stages_;}
    int maxChunkFrames() const noexcept {return baseChunk_;}

    // Measured/derived group delay. At each 2x stage, interpolation adds
    // (kTaps-1)/2=47 HIGH samples and the downsampler's odd output phase
    // advances the cascade by ONE high sample. Therefore a stage roundtrip
    // is 93 high frames, or 30.5 host frames at the first 2x level.
    // The downsampler's odd output phase must be accounted for exactly.
    double nominalFilterDelayFrames() const noexcept
    {
        if(factor_<=1)return 0.0;
        constexpr double roundtripHigh=static_cast<double>(kTaps-2);
        return roundtripHigh*(1.0-1.0/factor_)
                +static_cast<double>(extraHighRateDelay_)/factor_;
    }
    int alignmentDelayAtHighRate() const noexcept{return extraHighRateDelay_;}

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
            // Delay the actual high-rate wet audio by an integer number of
            // OVERSAMPLED samples. At 32x, 57 high samples add exactly
            // 57/32 host samples to offset the Core/FIR fractional PDC.
            if(extraHighRateDelay_>0)
                for(int ch=0;ch<(stereo?2:1);++ch)
                    delayHighRate(alignmentDelay_[static_cast<size_t>(ch)],
                                  high[static_cast<size_t>(ch)].data(),highCount);

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
    static constexpr int kTaps=95;
    static constexpr int kHistory=128;
    static constexpr int kPhaseTaps=48;
    static constexpr int kMaxHighRateDelay=63;
    static constexpr int kCentreTap=(kTaps-1)/2; // 47
    static constexpr int kOddCentreTap=(kCentreTap-1)/2; // 23
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
    struct HighRateDelay
    {
        std::array<float,kMaxHighRateDelay+1> history{};
        int cursor=0;
        void reset() noexcept {history.fill(0.0f);cursor=0;}
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
        // high-rate cycles/sample). 95-tap Blackman smooths the transition.
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
            // The 48 even-phase taps are mirror-symmetric about 23.5.
            // Pair inputs before multiplying: 24 FIR multiplies instead
            // of 48, with unchanged linear phase and group delay.
            double even=0.0;
            const auto& weights=polyphase_[0];
#if defined(APEX_LIMITER_OS_REFERENCE_FIR)
            // Retained for A/B numerical and CPU comparisons in CI.
            for(int tap=0;tap<kPhaseTaps;++tap)
            {
                const int index=(state.cursor+kHistory-tap)%kHistory;
                even+=state.samples[static_cast<size_t>(index)]
                        *weights[static_cast<size_t>(tap)];
            }
#else
            for(int tap=0;tap<kPhaseTaps/2;++tap)
            {
                const int nearIndex=(state.cursor+kHistory-tap)%kHistory;
                const int farIndex=(state.cursor+kHistory-(kPhaseTaps-1-tap))%kHistory;
                even+=(state.samples[static_cast<size_t>(nearIndex)]
                      +state.samples[static_cast<size_t>(farIndex)])
                      *weights[static_cast<size_t>(tap)];
            }
#endif
            dst[2*i]=static_cast<float>(even);
            // Odd polyphase: only k=23 (the 95-tap halfband centre) is
            // nonzero after DC normalisation. An EXACT delayed sample,
            // with no kernel loop or frame-dependent interpolation drift.
            dst[2*i+1]=static_cast<float>(
                state.samples[static_cast<size_t>(
                    (state.cursor+kHistory-kOddCentreTap)%kHistory)]);
            state.cursor=(state.cursor+1)%kHistory;
        }
    }
    void delayHighRate(HighRateDelay& state,float* samples,int frames) noexcept
    {
        constexpr int size=kMaxHighRateDelay+1;
        for(int i=0;i<frames;++i)
        {
            const int read=(state.cursor+size-extraHighRateDelay_)%size;
            const float original=samples[i];
            samples[i]=state.history[static_cast<size_t>(read)];
            state.history[static_cast<size_t>(state.cursor)]=original;
            state.cursor=(state.cursor+1)%size;
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
                // Sparse symmetric halfband: mirror the 48 nonzero
                // even taps into 24 exact-value pairs; centre unchanged.
                // This conserves FIR length, stop-band and PDC.
#if defined(APEX_LIMITER_OS_REFERENCE_FIR)
                for(int tap=0;tap<kTaps;tap+=2)
                {
                    const int index=(state.cursor+kHistory-tap)%kHistory;
                    value+=state.samples[static_cast<size_t>(index)]
                            *lowpass_[static_cast<size_t>(tap)];
                }
#else
                for(int tap=0;tap<kCentreTap;tap+=2)
                {
                    const int nearIndex=(state.cursor+kHistory-tap)%kHistory;
                    const int farIndex=(state.cursor+kHistory-(kTaps-1-tap))%kHistory;
                    value+=(state.samples[static_cast<size_t>(nearIndex)]
                           +state.samples[static_cast<size_t>(farIndex)])
                           *lowpass_[static_cast<size_t>(tap)];
                }
#endif
                value+=state.samples[static_cast<size_t>(
                            (state.cursor+kHistory-kCentreTap)%kHistory)]
                           *lowpass_[static_cast<size_t>(kCentreTap)];
                dst[output++]=static_cast<float>(value);
            }
            state.phase^=1;
            state.cursor=(state.cursor+1)%kHistory;
        }
    }
    int factor_=1,stages_=0,baseChunk_=128,extraHighRateDelay_=0;
    std::array<HighRateDelay,2> alignmentDelay_{};
    std::array<double,kTaps> lowpass_{};
    std::array<std::array<double,kPhaseTaps>,2> polyphase_{};
    std::array<Stage,kMaxStages> stagesState_{};
    std::array<std::array<std::vector<float>,2>,kMaxStages+1> work_{};
};

}} // namespace APEX::Limiter
