#pragma once
// APEX Bubblegum Delay — original delay DSP, not Waves source or an H-Delay emulation.
// C++17 standalone engine. Allocate ONLY in prepare(), never in process().
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace APEX::Delay
{
struct Settings
{
    // sync: 0=manual milliseconds, 1=host BPM, 2=manual BPM.
    int sync = 1;
    float timeMs = 375.0f;
    float manualBpm = 120.0f;
    int note = 5;          // eighth-note dotted, selected from kNoteBeats.
    float feedback = 0.45f; // 0..2; >1 permits rising repeats with safety saturation.
    float mix = 0.35f;      // 0..1. Sends can set 1.
    float outputDb = 0.0f;
    float highpassHz = 20.0f;
    float lowpassHz = 20000.0f;
    float modulationDepth = 0.0f; // 0..1; up to +/- 8ms pitch modulation.
    float modulationHz = 0.4f;
    int analog = 0;        // original colour curves 0=clean, 1..4=tape-like
    bool lofi = false;
    bool pingPong = false;
    bool phaseLeft = false;
    bool phaseRight = false;
    bool bypass = false;

    static Settings sanitise(Settings s) noexcept
    {
        auto safe=[](float x,float otherwise) { return std::isfinite(x) ? x : otherwise; };
        s.sync=std::clamp(s.sync,0,2);
        s.note=std::clamp(s.note,0,12);
        s.timeMs=std::clamp(safe(s.timeMs,375.0f),1.0f,3500.0f);
        s.manualBpm=std::clamp(safe(s.manualBpm,120.0f),30.0f,300.0f);
        s.feedback=std::clamp(safe(s.feedback,0.45f),0.0f,2.0f);
        s.mix=std::clamp(safe(s.mix,0.35f),0.0f,1.0f);
        s.outputDb=std::clamp(safe(s.outputDb,0.0f),-18.0f,18.0f);
        s.highpassHz=std::clamp(safe(s.highpassHz,20.0f),20.0f,20000.0f);
        s.lowpassHz=std::clamp(safe(s.lowpassHz,20000.0f),20.0f,20000.0f);
        s.modulationDepth=std::clamp(safe(s.modulationDepth,0.0f),0.0f,1.0f);
        s.modulationHz=std::clamp(safe(s.modulationHz,0.4f),0.1f,12.0f);
        s.analog=std::clamp(s.analog,0,4);
        return s;
    }
};

class Engine final
{
public:
    static constexpr int kNoteCount=13;
    // Number of quarter-note beats, including straight, triplet and dotted options.
    static constexpr std::array<float,kNoteCount> kNoteBeats {{
        1.0f/12.0f, 0.125f, 1.0f/6.0f, 0.25f, 1.0f/3.0f, 0.75f,
        0.5f, 2.0f/3.0f, 1.0f, 1.5f, 2.0f, 4.0f, 8.0f
    }};
    void prepare(double sampleRate)
    {
        rate_=(std::isfinite(sampleRate) && sampleRate>=8000.0)
            ? sampleRate : 48000.0;
        const auto count=static_cast<std::size_t>(std::ceil(rate_*3.55))+8u;
        for (auto& channel : ring_) channel.assign(count,0.0f);
        smoothCoeff_=std::exp(-1.0/(rate_*0.008));
        timeCoeff_=std::exp(-1.0/(rate_*0.018));
        reset();
    }
    void reset() noexcept
    {
        for(auto& c:ring_) std::fill(c.begin(),c.end(),0.0f);
        highpassState_.fill(0.0);
        lowpassState_.fill(0.0);
        lofiHold_.fill(0.0f);
        write_=0;
        sampleCounter_=0;
        phase_=0.0;
        processed_=false;
        currentTime_=targetSamples();
        currentMix_=settings_.mix;
        currentFeedback_=settings_.feedback;
        currentOutput_=std::pow(10.0,settings_.outputDb/20.0);
        bypassBlend_=settings_.bypass?0.0:1.0;
    }
    void setSettings(Settings requested) noexcept
    {
        settings_=Settings::sanitise(requested);
        if (! processed_)
        {
            currentTime_=targetSamples();
            currentMix_=settings_.mix;
            currentFeedback_=settings_.feedback;
            currentOutput_=std::pow(10.0,settings_.outputDb/20.0);
            bypassBlend_=settings_.bypass?0.0:1.0;
        }
    }
    void setHostTempo(double bpm) noexcept
    {
        if(std::isfinite(bpm) && bpm>1.0 && bpm<1000.0) hostBpm_=bpm;
    }
    Settings getSettings() const noexcept { return settings_; }
    int getLatencySamples() const noexcept { return 0; }
    double getTimeMs() const noexcept { return targetSamples()*1000.0/rate_; }
    void process(float* left,float* right,int frames) noexcept
    {
        if(left==nullptr || frames<=0 || ring_[0].empty()) return;
        processed_=true;
        const bool stereo=right!=nullptr && right!=left;
        const double desiredTime=targetSamples();
        const float desiredFeedback=settings_.feedback;
        const float desiredMix=settings_.mix;
        const double desiredOutput=std::pow(10.0,settings_.outputDb/20.0);
        const double hpCoeff=std::exp(-twoPi*settings_.highpassHz/rate_);
        const double highCut=std::min(static_cast<double>(settings_.lowpassHz),rate_*0.45);
        const double lowCoeff=std::exp(-twoPi*highCut/rate_);
        const bool highEnabled=settings_.highpassHz>20.01f;
        const bool lowEnabled=settings_.lowpassHz<19999.0f;
        const std::size_t length=ring_[0].size();
        for(int i=0;i<frames;++i)
        {
            const float inputL=finite(left[i]);
            const float inputR=stereo?finite(right[i]):inputL;
            currentTime_=timeCoeff_*currentTime_+(1.0-timeCoeff_)*desiredTime;
            currentFeedback_=smoothCoeff_*currentFeedback_+(1.0-smoothCoeff_)*desiredFeedback;
            currentMix_=smoothCoeff_*currentMix_+(1.0-smoothCoeff_)*desiredMix;
            currentOutput_=smoothCoeff_*currentOutput_+(1.0-smoothCoeff_)*desiredOutput;
            const double desiredBypass=settings_.bypass?0.0:1.0;
            bypassBlend_=smoothCoeff_*bypassBlend_+(1.0-smoothCoeff_)*desiredBypass;
            phase_+=twoPi*settings_.modulationHz/rate_;
            if(phase_>=twoPi) phase_-=twoPi;
            const double mod=std::sin(phase_)*settings_.modulationDepth*rate_*0.008;
            const double offset=std::clamp(currentTime_+mod,1.0,
                                        static_cast<double>(length-3));
            double pos=static_cast<double>(write_)-offset;
            if(pos<0.0) pos+=static_cast<double>(length);
            const std::size_t a=static_cast<std::size_t>(pos);
            const std::size_t b=(a+1)%length;
            const float fraction=static_cast<float>(pos-static_cast<double>(a));
            const float delayedL=ring_[0][a]+(ring_[0][b]-ring_[0][a])*fraction;
            const float delayedR=stereo
                ?ring_[1][a]+(ring_[1][b]-ring_[1][a])*fraction:delayedL;
            const float wetL=colourAndFilter(delayedL,0,hpCoeff,lowCoeff,highEnabled,lowEnabled);
            const float wetR=stereo
                ?colourAndFilter(delayedR,1,hpCoeff,lowCoeff,highEnabled,lowEnabled):wetL;
            float feedL=0.0f,feedR=0.0f;
            if (settings_.pingPong && stereo)
            {
                feedL=0.5f*(inputL+inputR)+static_cast<float>(currentFeedback_)*wetR;
                feedR=static_cast<float>(currentFeedback_)*wetL;
            }
            else
            {
                feedL=inputL+static_cast<float>(currentFeedback_)*wetL;
                feedR=inputR+static_cast<float>(currentFeedback_)*wetR;
            }
            ring_[0][write_]=boundFeedback(feedL);
            if(stereo) ring_[1][write_]=boundFeedback(feedR);
            ++write_;
            if(write_==length) write_=0;
            const float markedL=settings_.phaseLeft?-wetL:wetL;
            const float markedR=settings_.phaseRight?-wetR:wetR;
            const float gain=static_cast<float>(currentOutput_);
            const float mix=static_cast<float>(currentMix_);
            const float active=static_cast<float>(bypassBlend_);
            const float processedL=gain*((1.0f-mix)*inputL+mix*markedL);
            left[i]=finite(inputL+active*(processedL-inputL));
            if(stereo)
            {
                const float processedR=gain*((1.0f-mix)*inputR+mix*markedR);
                right[i]=finite(inputR+active*(processedR-inputR));
            }
            ++sampleCounter_;
        }
    }
private:
    // We keep the feedback path bounded even when musical feedback > 100%.
    static float boundFeedback(float x) noexcept
    {
        x=finite(x);
        if(std::abs(x)>3.0f) return 3.0f*std::tanh(x/3.0f);
        return x;
    }
    static float finite(float x) noexcept
    {
        return std::isfinite(x)?std::clamp(x,-64.0f,64.0f):0.0f;
    }
    float colourAndFilter(float s,int ch,double hp,double lp,
                          bool highEnabled,bool lowEnabled) noexcept
    {
        double x=s;
        if(highEnabled)
        {
            highpassState_[ch]=hp*highpassState_[ch]+(1.0-hp)*x;
            x-=highpassState_[ch];
        }
        else highpassState_[ch]=0.0;
        if(lowEnabled)
        {
            lowpassState_[ch]=lp*lowpassState_[ch]+(1.0-lp)*x;
            x=lowpassState_[ch];
        }
        else lowpassState_[ch]=0.0;
        if(settings_.lofi)
        {
            if((sampleCounter_&3u)==0) lofiHold_[ch]=static_cast<float>(
                std::round(x*2048.0)/2048.0);
            x=lofiHold_[ch];
        }
        if(settings_.analog>0)
        {
            // Four APEX-original curves, no attempt at vendor circuit modeling.
            const double drive=1.0+settings_.analog*0.25;
            x=std::tanh(x*drive)/std::tanh(drive);
        }
        return finite(static_cast<float>(x));
    }
    double targetSamples() const noexcept
    {
        const double bpm=settings_.sync==2?settings_.manualBpm:hostBpm_;
        const double ms=settings_.sync==0?settings_.timeMs:
            60000.0*static_cast<double>(kNoteBeats[static_cast<std::size_t>(settings_.note)])/bpm;
        return std::clamp(ms*rate_/1000.0,rate_/1000.0,3.5*rate_);
    }
    static constexpr double twoPi=6.2831853071795864769;
    Settings settings_ {};
    std::array<std::vector<float>,2> ring_;
    std::array<double,2> highpassState_ {}, lowpassState_ {};
    std::array<float,2> lofiHold_ {};
    double rate_=48000.0,hostBpm_=120.0,smoothCoeff_=0.99,timeCoeff_=0.99;
    double currentTime_=18000.0,currentFeedback_=0.45,currentMix_=0.35,currentOutput_=1.0;
    double bypassBlend_=1.0,phase_=0.0;
    std::size_t write_=0;
    std::uint64_t sampleCounter_=0;
    bool processed_=false;
};
} // namespace APEX::Delay
