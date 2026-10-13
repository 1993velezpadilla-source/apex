#pragma once
// APEX Tune Live processing authority.
// C++17, no JUCE dependency. Allocations only during prepare(), never process().
// Actual live host and portable wet-audio regression share this SAME implementation.
#include "ApexTuneLiveVoiceControl.h"
#include "signalsmith-stretch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace APEX { namespace TuneLive {

struct DspSettings {
    float correction=0.85f;        // 0..1
    float referenceHz=440.0f;      // 430..450 (calibrated A4)
    float retune=0.20f;            // 0 = 1 ms, 1 = 150 ms
    int key=0;                     // C=0, C#=1... B=11
    int scale=0;                   // Chromatic=0, Major=1, NaturalMinor=2
    float wet=1.0f;               // 0..1
    bool bypass=false;
    float humanize=0.70f;         // 0..1; preserve small natural inflections
    float flexTune=0.35f;         // 0..1; musical micro-pitch dead-zone
    float stability=0.40f;        // 0..1 note-target hysteresis
    bool preserveFormants=true;
};

class DspCore final {
public:
    void prepare(double sampleRate,int maxBlockSize,int numChannels) {
        rate_=std::isfinite(sampleRate) && sampleRate>8000.0 ? sampleRate : 48000.0;
        block_=std::max(1,maxBlockSize);
        channels_=std::clamp(numChannels,1,2);
        tracker_.prepare(rate_);
        policy_.reset();
        // Signalsmith analysis/synthesis latency is reported, not assumed.
        stretch_.configure(channels_,
                           std::max(256,static_cast<int>(std::lround(rate_*0.040))),
                           std::max(64,static_cast<int>(std::lround(rate_*0.010))),
                           false);
        stretch_.reset();
        stretch_.setTransposeFactor(1.0f);
        stretch_.setFormantFactor(1.0f,true);
        latency_=std::max(0,stretch_.inputLatency()+stretch_.outputLatency());
        wet_.assign(static_cast<std::size_t>(channels_)*block_,0.0f);
        delayLength_=std::max(1,latency_+1);
        delay_.assign(static_cast<std::size_t>(channels_)*delayLength_,0.0f);
        delayPosition_=0;
        target_=smooth_=frequency_=wetSlew_=voicedSlew_=voicedTarget_=0.0f;
        envelopeL_=envelopeR_=0.0f;
    }
    void reset() {
        tracker_.reset();
        policy_.reset();
        stretch_.reset();
        std::fill(delay_.begin(),delay_.end(),0.0f);
        std::fill(wet_.begin(),wet_.end(),0.0f);
        delayPosition_=0;
        target_=smooth_=frequency_=wetSlew_=voicedSlew_=voicedTarget_=0.0f;
        envelopeL_=envelopeR_=0.0f;
    }
    int latencySamples() const noexcept {return latency_;}
    double sampleRate() const noexcept {return rate_;}
    float frequencyHz() const noexcept {return frequency_;}
    double detectorWindowMs() const noexcept {return tracker_.analysisWindowMs();}

    // planar channelData is both input and output, as for JUCE AudioBuffer pointers.
    // Input to stretch is never the SAME storage as its output.
    void process(float* const* channelData,int numChannels,int n,DspSettings user) {
        if(!channelData || n<=0 || block_<=0) return;
        const int channels=std::min({numChannels,channels_,2});
        if(channels!=channels_ || channels<=0 || !channelData[0]) return;
        if(channels==2 && !channelData[1]) return;
        user.correction=std::clamp(user.correction,0.0f,1.0f);
        user.retune=std::clamp(user.retune,0.0f,1.0f);
        user.wet=std::clamp(user.wet,0.0f,1.0f);
        const VoiceSettings vs{std::clamp(user.key,0,11),
                               std::clamp(user.scale,0,2),
                               user.correction,
                               user.referenceHz,
                               std::clamp(user.humanize,0.0f,1.0f),
                               std::clamp(user.flexTune,0.0f,1.0f),
                               std::clamp(user.stability,0.0f,1.0f)*0.35f,
                               static_cast<float>(tracker_.analysisWindowMs()/8.0)};
        const double retuneMs=1.0+149.0*static_cast<double>(user.retune);
        const float wetStep=static_cast<float>(1.0/(rate_*0.008+1.0));
        for(int offset=0;offset<n;) {
            const int count=std::min(block_,n-offset);
            // Smooth controls at each small chunk, not at a fixed host block rate.
            const float response=static_cast<float>(1.0-std::exp(-count/(rate_*retuneMs*0.001)));
            smooth_+=(target_-smooth_)*response;
            stretch_.setTransposeFactor(std::pow(2.0f,smooth_/12.0f));
            stretch_.setFormantFactor(1.0f,user.preserveFormants);
            stretch_.setFormantBase(frequency_>0 ? frequency_/static_cast<float>(rate_) : 0.0f);
            const float* source[2]={channelData[0]+offset,
                channels==2 ? channelData[1]+offset : nullptr};
            float* processed[2]={wet_.data(),channels==2 ? wet_.data()+block_ : nullptr};
            stretch_.process(source,count,processed,count);
            for(int i=0;i<count;++i) {
                // For stereo vocals, track the predominant microphone
                // channel instead of always assuming a left-side signal.
                // Never average opposite-polarity stereo signals to silence.
                const float left=source[0][i];
                const float right=channels==2 ? source[1][i] : 0.0f;
                const float envelopeAlpha=static_cast<float>(1.0/(rate_*0.010+1.0));
                envelopeL_+=(std::abs(left)-envelopeL_)*envelopeAlpha;
                envelopeR_+=(std::abs(right)-envelopeR_)*envelopeAlpha;
                const float detectorInput=(channels==2 && envelopeR_>1.2f*envelopeL_)
                                         ? right : left;
                if(tracker_.push(detectorInput)) {
                    const auto tracked=tracker_.latest();
                    const auto decision=policy_.update(tracked,vs);
                    frequency_=decision.voiced?tracked.hz:0.0f;
                    target_=decision.shiftSemitones;
                    voicedTarget_=decision.voiced?1.0f:0.0f;
                }
                // Sibilants, breaths and unvoiced transients return to aligned dry.
                voicedSlew_+=(voicedTarget_-voicedSlew_)*wetStep;
                wetSlew_+=((user.bypass?0.0f:user.wet)-wetSlew_)*wetStep;
                const float wetAmount=wetSlew_*voicedSlew_;
                for(int c=0;c<channels;++c) {
                    const std::size_t index=static_cast<std::size_t>(c)*delayLength_
                                             +static_cast<std::size_t>(delayPosition_);
                    delay_[index]=source[c][i];
                    const int readPos=(delayPosition_+1)%delayLength_;
                    const float dry=latency_==0 ? source[c][i]
                        : delay_[static_cast<std::size_t>(c)*delayLength_+static_cast<std::size_t>(readPos)];
                    channelData[c][offset+i]=dry*(1.0f-wetAmount)+processed[c][i]*wetAmount;
                }
                delayPosition_=(delayPosition_+1)%delayLength_;
            }
            offset+=count;
        }
    }
private:
    PitchTracker tracker_;
    VoiceControl policy_;
    signalsmith::stretch::SignalsmithStretch<float> stretch_;
    std::vector<float> wet_,delay_;
    double rate_=48000.0;
    float target_=0, smooth_=0, frequency_=0, wetSlew_=0,voicedSlew_=0,voicedTarget_=0;
    float envelopeL_=0,envelopeR_=0;
    int block_=0,channels_=2,latency_=0,delayPosition_=0,delayLength_=1;
};

}} // namespace APEX::TuneLive
