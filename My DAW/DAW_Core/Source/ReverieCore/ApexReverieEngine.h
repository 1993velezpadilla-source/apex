#pragma once
// APEX Reverie: original modulated 8-line Hadamard FDN algorithmic reverb.
// Pure C++17 DSP; prepare() owns all allocation. process() is realtime safe.
// Inspired by published FDN/diffusion techniques, not a third-party code port.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace APEX::Reverie
{
struct Settings
{
    int mode = 0;          // Plate, Vintage Plate, Hall, Chamber, Room, Ambient
    int era = 2;           // 70s, 80s, Modern
    float predelayMs = 35.0f, decaySeconds = 2.4f, size = 0.60f;
    float dampingHz = 8500.0f, lowCutHz = 180.0f, highCutHz = 14000.0f;
    float modulation = 0.30f, width = 1.0f, mix = 0.25f, ducking = 0.0f;
    bool bypass = false;
};

class Engine final
{
    struct Delay
    {
        std::vector<float> samples;
        int write = 0;
        void prepare(int capacity) { samples.assign(static_cast<size_t>(capacity),0.0f); write=0; }
        void reset() noexcept { std::fill(samples.begin(),samples.end(),0.0f); write=0; }
        float tap(float delay) const noexcept
        {
            const int n=static_cast<int>(samples.size());
            if(n<2) return 0.0f;
            const float d=std::max(0.0f,std::min(delay,static_cast<float>(n-1)));
            float p=static_cast<float>(write-1)-d;
            if(p<0.0f) p+=static_cast<float>(n);
            const int a=static_cast<int>(p);
            const int b=(a+1==n)?0:a+1;
            const float f=p-static_cast<float>(a);
            return samples[static_cast<size_t>(a)]*(1.0f-f)
                +samples[static_cast<size_t>(b)]*f;
        }
        void push(float input) noexcept
        {
            samples[static_cast<size_t>(write)]=input;
            if(++write==static_cast<int>(samples.size())) write=0;
        }
    };
    struct Diffuser
    {
        Delay line;
        float process(float input,float feedback) noexcept
        {
            const float old=line.tap(static_cast<float>(static_cast<int>(line.samples.size())-1));
            const float output=old-feedback*input;
            line.push(input+feedback*output);
            return output;
        }
    };

public:
    void prepare(double sampleRate)
    {
        sampleRate_=std::max(8000.0,std::min(384000.0,std::isfinite(sampleRate)?sampleRate:48000.0));
        const int predelayCapacity=static_cast<int>(std::ceil(sampleRate_*0.42))+8;
        for(auto& p:pre_) p.prepare(predelayCapacity);
        for(auto& d:fdn_) d.prepare(static_cast<int>(std::ceil(sampleRate_*0.29))+64);
        constexpr float offsets[4]={4.1f,5.7f,8.3f,11.3f};
        for(int c=0;c<2;++c)
            for(int k=0;k<4;++k)
                inputDiffusers_[c][k].line.prepare(
                    std::max(4,static_cast<int>(std::round(
                        sampleRate_*(offsets[k]+c*1.1f)*0.001f))));
        setSettings(settings_);
        reset();
    }

    void reset() noexcept
    {
        for(auto& p:pre_) p.reset();
        for(auto& f:fdn_) f.reset();
        for(auto& side:inputDiffusers_) for(auto& d:side) d.line.reset();
        lowInput_.fill(0); dampState_.fill(0); highOutput_.fill(0);
        lowOutput_.fill(0); phase_.fill(0); smoothedDelay_=delayTargets_;
        duckEnvelope_=0;
        predelayCurrent_=static_cast<float>(sampleRate_*settings_.predelayMs/1000.0f);
        mixCurrent_=settings_.mix;
    }

    void setSettings(Settings s) noexcept
    {
        s.mode=std::max(0,std::min(5,s.mode)); s.era=std::max(0,std::min(2,s.era));
        s.predelayMs=clamp(s.predelayMs,0.0f,250.0f);
        s.decaySeconds=clamp(s.decaySeconds,0.2f,18.0f);
        s.size=clamp(s.size,0.0f,1.0f);
        s.dampingHz=clamp(s.dampingHz,750.0f,20000.0f);
        s.lowCutHz=clamp(s.lowCutHz,20.0f,1500.0f);
        s.highCutHz=clamp(s.highCutHz,1500.0f,20000.0f);
        s.modulation=clamp(s.modulation,0.0f,1.0f);
        s.width=clamp(s.width,0.0f,2.0f);
        s.mix=clamp(s.mix,0.0f,1.0f);
        s.ducking=clamp(s.ducking,0.0f,1.0f);
        settings_=s;

        // Modes are independently voiced: higher diffusion for plates,
        // longer separated reflections for halls, compact timing for rooms.
        static constexpr float modeScale[6]={0.82f,0.92f,1.48f,1.12f,0.59f,1.70f};
        static constexpr float baseMs[8]={29.7f,37.1f,43.7f,53.3f,
                                          61.9f,71.3f,83.1f,97.9f};
        const float scale=modeScale[s.mode]*(0.60f+0.68f*s.size);
        for(int i=0;i<8;++i)
        {
            const float delay=static_cast<float>(sampleRate_)*baseMs[i]*0.001f*scale;
            delayTargets_[i]=delay;
            feedback_[i]=std::pow(0.001f,delay/
                (static_cast<float>(sampleRate_)*s.decaySeconds));
        }
        const float nyquist=static_cast<float>(sampleRate_)*0.46f;
        float eraLimit=s.era==0?9000.0f:(s.era==1?14500.0f:22000.0f);
        // Damping operates inside feedback, EQ outside the feedback network.
        dampAlpha_=onePole(std::min(nyquist,std::min(s.dampingHz,eraLimit)));
        lowCutAlpha_=onePole(std::min(nyquist,s.lowCutHz));
        highCutAlpha_=onePole(std::min(nyquist,std::min(s.highCutHz,eraLimit)));
        smoothAlpha_=1.0f-std::exp(-1.0f/static_cast<float>(sampleRate_*0.024));
        duckAttack_=1.0f-std::exp(-1.0f/static_cast<float>(sampleRate_*0.003));
        duckRelease_=1.0f-std::exp(-1.0f/static_cast<float>(sampleRate_*0.22));
    }

    void process(float& left,float& right) noexcept
    {
        // No allocation, locks, JUCE access, or branches on GUI state.
        const float dryL=std::isfinite(left)?left:0.0f;
        const float dryR=std::isfinite(right)?right:0.0f;
        const float feedL=std::max(-8.0f,std::min(8.0f,dryL));
        const float feedR=std::max(-8.0f,std::min(8.0f,dryR));
        const float targetPre=settings_.predelayMs*0.001f*static_cast<float>(sampleRate_);
        predelayCurrent_+=smoothAlpha_*(targetPre-predelayCurrent_);
        mixCurrent_+=smoothAlpha_*(settings_.mix-mixCurrent_);

        // Store then tap: zero pre-delay admits the current sample.
        pre_[0].push(feedL); pre_[1].push(feedR);
        float preL=pre_[0].tap(predelayCurrent_);
        float preR=pre_[1].tap(predelayCurrent_);
        const float inputGain=settings_.bypass?0.0f:1.0f;
        preL*=inputGain; preR*=inputGain;
        const float input[2]={preL,preR};
        float diffuse[2]={};
        const float diffusion=(settings_.mode<=1)?0.72f:
            (settings_.mode==4?0.43f:0.62f);
        for(int c=0;c<2;++c)
        {
            const float hp=input[c]-lowInput_[c];
            lowInput_[c]+=lowCutAlpha_*(input[c]-lowInput_[c]);
            float x=hp;
            for(int k=0;k<4;++k) x=inputDiffusers_[c][k].process(x,diffusion);
            diffuse[c]=x;
        }
        std::array<float,8> taps{};
        for(int i=0;i<8;++i)
        {
            smoothedDelay_[i]+=smoothAlpha_*(delayTargets_[i]-smoothedDelay_[i]);
            const float rate=0.16f+0.047f*static_cast<float>(i);
            phase_[i]+=rate/static_cast<float>(sampleRate_);
            if(phase_[i]>=1.0f) phase_[i]-=1.0f;
            // Smooth sub-Hz modulation of fractionally interpolated FDN taps.
            const float wobble=std::sin(6.283185307179586f*phase_[i])
                *settings_.modulation*static_cast<float>(sampleRate_)
                *0.00022f*(settings_.era==2?0.75f:1.0f);
            float sampled=fdn_[i].tap(smoothedDelay_[i]+wobble);
            dampState_[i]+=dampAlpha_*(sampled-dampState_[i]);
            taps[i]=dampState_[i];
        }

        // Normalized 8x8 Hadamard feedback mixing (orthogonal, bounded).
        std::array<float,8> mixed=taps;
        for(int step=1;step<8;step*=2)
            for(int base=0;base<8;base+=step*2)
                for(int k=0;k<step;++k)
                {
                    const float a=mixed[base+k],b=mixed[base+k+step];
                    mixed[base+k]=a+b; mixed[base+k+step]=a-b;
                }
        constexpr float invSqrt8=0.35355339059327376f;
        for(int i=0;i<8;++i)
        {
            // Alternating L/R injection breaks symmetric mono collapse.
            const float inject=diffuse[i&1]*0.22f+
                diffuse[(i+1)&1]*(i%3==0?0.035f:-0.035f);
            const float value=feedback_[i]*mixed[i]*invSqrt8+inject;
            fdn_[i].push(std::isfinite(value)?std::max(-32.0f,std::min(32.0f,value)):0.0f);
        }
        float wetL=(taps[0]+taps[2]-taps[4]+taps[6])*0.35f;
        float wetR=(taps[1]+taps[3]-taps[5]+taps[7])*0.35f;
        // Four decorrelated early reflections per channel (no other buffers).
        constexpr float erMs[4]={8.7f,19.3f,37.9f,59.1f};
        float erL=0,erR=0;
        for(int k=0;k<4;++k)
        {
            const float tapSamples=predelayCurrent_
                +static_cast<float>(sampleRate_)*erMs[k]*0.001f;
            const float level=0.14f-0.021f*static_cast<float>(k);
            erL+=pre_[k&1].tap(tapSamples)*level;
            erR+=pre_[(k+1)&1].tap(tapSamples+static_cast<float>(sampleRate_)*0.0035f)*level;
        }
        const float erMix=(settings_.mode==4?0.76f:(settings_.mode<=1?0.21f:0.43f));
        wetL+=erL*erMix*inputGain; wetR+=erR*erMix*inputGain;

        const float wetIn[2]={wetL,wetR};
        float wet[2]={};
        for(int c=0;c<2;++c)
        {
            highOutput_[c]+=highCutAlpha_*(wetIn[c]-highOutput_[c]);
            lowOutput_[c]+=lowCutAlpha_*(highOutput_[c]-lowOutput_[c]);
            wet[c]=highOutput_[c]-lowOutput_[c];
        }
        const float mid=0.5f*(wet[0]+wet[1]);
        const float side=0.5f*(wet[0]-wet[1])*settings_.width;
        wetL=mid+side; wetR=mid-side;
        const float detector=std::max(std::fabs(dryL),std::fabs(dryR));
        const float envelopeAlpha=detector>duckEnvelope_?duckAttack_:duckRelease_;
        duckEnvelope_+=envelopeAlpha*(detector-duckEnvelope_);
        const float duckGain=1.0f-settings_.ducking*
            std::min(0.84f,duckEnvelope_*3.0f);
        // Linear gain crossfade: exact dry at MIX=0, exact wet at MIX=100.
        const float wetGain=mixCurrent_*duckGain;
        const float dryGain=1.0f-mixCurrent_;
        left=settings_.bypass?dryL:dryL*dryGain+wetL*wetGain;
        right=settings_.bypass?dryR:dryR*dryGain+wetR*wetGain;
    }

private:
    static float clamp(float x,float lo,float hi) noexcept
    {
        return std::isfinite(x)?std::max(lo,std::min(hi,x)):lo;
    }
    float onePole(float hz) const noexcept
    {
        return 1.0f-std::exp(-6.283185307179586f*hz/
                            static_cast<float>(sampleRate_));
    }
    Settings settings_{};
    double sampleRate_=48000.0;
    std::array<Delay,2> pre_{};
    std::array<Delay,8> fdn_{};
    std::array<std::array<Diffuser,4>,2> inputDiffusers_{};
    std::array<float,8> delayTargets_{},smoothedDelay_{},feedback_{},dampState_{},phase_{};
    std::array<float,2> lowInput_{},highOutput_{},lowOutput_{};
    float dampAlpha_=0.7f,lowCutAlpha_=0.02f,highCutAlpha_=0.6f;
    float smoothAlpha_=0.001f,duckAttack_=0.005f,duckRelease_=0.0001f;
    float predelayCurrent_=0.0f,mixCurrent_=0.25f,duckEnvelope_=0.0f;
};
} // namespace APEX::Reverie
