#pragma once
// APEX Tune Live processing authority.
// C++17, no JUCE dependency. Allocations only during prepare(), never process().
// Actual live host and portable wet-audio regression share this SAME implementation.
#include "ApexTuneLiveVoiceControl.h"
// Pinned signalsmith-linear fft.h uses std::memcpy without including <cstring>.
// APEX owns this include-order compatibility shim; do not edit vendored code.
#include <cstring>
#include "signalsmith-stretch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

// Profile is selected on the non-RT prepare thread, NEVER automated mid-buffer.
// Experimental 18ms and 12ms research candidates; no Antares parity
// claim. Shipping hosted APEX Tune Live continues to use the 40ms HQ path.
enum class ShifterProfile { HighQuality40ms, Experimental18ms, Experimental12ms };

class DspCore final {
public:
    void prepare(double sampleRate,int maxBlockSize,int numChannels,
                 ShifterProfile profile=ShifterProfile::HighQuality40ms) {
        profile_=profile;
        rate_=std::isfinite(sampleRate) && sampleRate>8000.0 ? sampleRate : 48000.0;
        block_=std::max(1,maxBlockSize);
        channels_=std::clamp(numChannels,1,2);
        tracker_.prepare(rate_);
        policy_.reset();
        // Short-window candidate runs the exact same production pitch
        // shifter. Both window and interval must scale together. Causal
        // pitch tracking still requires multiple source cycles and the
        // measured shifter PDC is not the whole device round trip.
        const double windowSeconds=(profile_==ShifterProfile::Experimental12ms)
                                      ?0.012
                                  : (profile_==ShifterProfile::Experimental18ms)
                                      ?0.018 : 0.040;
        const int window=std::max(256,static_cast<int>(std::lround(rate_*windowSeconds)));
        const int interval=std::max(64,static_cast<int>(std::lround(window*0.25)));
        stretch_.configure(channels_,window,interval,false);
        stretch_.reset();
        stretch_.setTransposeFactor(1.0f);
        stretch_.setFormantFactor(1.0f,true);
        latency_=std::max(0,stretch_.inputLatency()+stretch_.outputLatency());
        wet_.assign(static_cast<std::size_t>(channels_)*block_,0.0f);
        delayLength_=std::max(1,latency_+1);
        delay_.assign(static_cast<std::size_t>(channels_)*delayLength_,0.0f);
        delayPosition_=0;
        // Capture-rate input envelope smoothing coefficient, set off the RT thread.
        envelopeAlpha_=static_cast<float>(1.0/(rate_*0.010+1.0));
        // Constant smoothing coefficients are independent of user controls.
        // Compute once on the prepare thread, not on every ASIO callback.
        wetStep_=static_cast<float>(1.0/(rate_*0.008+1.0));
        neutralStep_=static_cast<float>(1.0/(rate_*0.012+1.0));
        target_=smooth_=frequency_=wetSlew_=voicedSlew_=voicedTarget_=0.0f;
        pitchActiveSlew_=0.0f;
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
        pitchActiveSlew_=0.0f;
        envelopeL_=envelopeR_=0.0f;
    }
    int latencySamples() const noexcept {return latency_;}
    ShifterProfile profile() const noexcept {return profile_;}
    double sampleRate() const noexcept {return rate_;}
    float frequencyHz() const noexcept {return frequency_;}
    // Read-only DSP debug telemetry used by regression tests. Never expose
    // these across threads without taking an atomic host snapshot.
    float debugTargetShiftSemitones() const noexcept {return target_;}
    float debugAppliedShiftSemitones() const noexcept {return smooth_;}
    float debugWetAmount() const noexcept {return wetSlew_*voicedSlew_*pitchActiveSlew_;}
    double detectorWindowMs() const noexcept {return tracker_.analysisWindowMs();}

    // planar channelData is both input and output, as for JUCE AudioBuffer pointers.
    // Input to stretch is never the SAME storage as its output.
    void process(float* const* channelData,int numChannels,int n,DspSettings user) {
        if(!channelData || n<=0 || block_<=0) return;
        const int channels=std::min({numChannels,channels_,2});
        if(channels!=channels_ || channels<=0 || !channelData[0]) return;
        if(channels==2 && !channelData[1]) return;
        // Host automation and untrusted upstream plugins can supply NaN/Inf.
        // std::clamp() does NOT repair NaN, so sanitize explicitly *before*
        // evaluating exp/pow or invoking the pitch shifter.
        auto safeUnit=[](float value,float fallback) noexcept {
            return std::isfinite(value) ? std::clamp(value,0.0f,1.0f) : fallback;
        };
        user.correction=safeUnit(user.correction,0.85f);
        user.retune=safeUnit(user.retune,0.20f);
        user.wet=safeUnit(user.wet,1.0f);
        user.humanize=safeUnit(user.humanize,0.70f);
        user.flexTune=safeUnit(user.flexTune,0.35f);
        user.stability=safeUnit(user.stability,0.40f);
        if(!std::isfinite(user.referenceHz))
            user.referenceHz=440.0f;
        user.referenceHz=std::clamp(user.referenceHz,430.0f,450.0f);
        const VoiceSettings vs{std::clamp(user.key,0,11),
                               std::clamp(user.scale,0,2),
                               user.correction,
                               user.referenceHz,
                               std::clamp(user.humanize,0.0f,1.0f),
                               std::clamp(user.flexTune,0.0f,1.0f),
                               std::clamp(user.stability,0.0f,1.0f)*0.35f,
                               static_cast<float>(tracker_.analysisWindowMs()/8.0)};
        const double retuneMs=1.0+149.0*static_cast<double>(user.retune);
        // Absolute-pitch-neutral vocals must remain true, PDC-aligned dry.
        // Processing an already-in-tune voice through a spectral shifter
        // alters vowels and phase for no musical benefit. A smoothed wet gate
        // removes this coloration at tiny shifts without clicky hard cuts.
        for(int offset=0;offset<n;) {
            const int count=std::min(block_,n-offset);
            // Smooth controls at each small chunk, not at a fixed host block rate.
            const float response=static_cast<float>(1.0-std::exp(-count/(rate_*retuneMs*0.001)));
            smooth_+=(target_-smooth_)*response;
            stretch_.setTransposeFactor(std::pow(2.0f,smooth_/12.0f));
            // 0.01 semitone = one cent. Remain transparently dry when
            // there is no audible work to perform. The amount/mix switches
            // remain subject to separate smoothing on every output sample.
            const float needsPitchWork=(std::abs(target_)>0.010f ||
                                        std::abs(smooth_)>0.010f)
                                        ?1.0f:0.0f;
            stretch_.setFormantFactor(1.0f,user.preserveFormants);
            stretch_.setFormantBase(frequency_>0 ? frequency_/static_cast<float>(rate_) : 0.0f);
            // Nonfinite samples can poison FFT state for multiple seconds.
            // Repair at the insert boundary; no allocations or logging on RT.
            for(int ch=0;ch<channels;++ch)
                for(int i=0;i<count;++i) {
                    float& sample=channelData[ch][offset+i];
                    if(!std::isfinite(sample) || std::abs(sample)<1.0e-20f)
                        sample=0.0f;
                    else
                        sample=std::clamp(sample,-8.0f,8.0f);
                }
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
                envelopeL_+=(std::abs(left)-envelopeL_)*envelopeAlpha_;
                envelopeR_+=(std::abs(right)-envelopeR_)*envelopeAlpha_;
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
                voicedSlew_+=(voicedTarget_-voicedSlew_)*wetStep_;
                wetSlew_+=((user.bypass?0.0f:user.wet)-wetSlew_)*wetStep_;
                pitchActiveSlew_+=(needsPitchWork-pitchActiveSlew_)*neutralStep_;
                const float wetAmount=wetSlew_*voicedSlew_*pitchActiveSlew_;
                // One branch-based wrap per sample instead of an integer
                // remainder and repeated per-channel index calculation.
                // At latency=0 the current input remains the dry authority.
                const int nextDelayPosition=delayPosition_+1==delayLength_
                                              ?0:delayPosition_+1;
                for(int c=0;c<channels;++c) {
                    const std::size_t index=static_cast<std::size_t>(c)*delayLength_
                                             +static_cast<std::size_t>(delayPosition_);
                    delay_[index]=source[c][i];
                    const float dry=latency_==0 ? source[c][i]
                        : delay_[static_cast<std::size_t>(c)*delayLength_+static_cast<std::size_t>(nextDelayPosition)];
                    const float wetSample=std::isfinite(processed[c][i])
                                        ? std::clamp(processed[c][i],-8.0f,8.0f)
                                        : 0.0f;
                    const float result=dry*(1.0f-wetAmount)+wetSample*wetAmount;
                    channelData[c][offset+i]=std::isfinite(result)?result:0.0f;
                }
                delayPosition_=nextDelayPosition;
            }
            offset+=count;
        }
    }
private:
    PitchTracker tracker_;
    VoiceControl policy_;
    // Stable shifter seed makes repeated APEX offline bounces reproducible.
    // Different plugin instances remain independent DSP state machines.
    signalsmith::stretch::SignalsmithStretch<float> stretch_{0x41504558L};
    std::vector<float> wet_,delay_;
    double rate_=48000.0;
    float target_=0, smooth_=0, frequency_=0, wetSlew_=0,voicedSlew_=0,voicedTarget_=0;
    float envelopeL_=0,envelopeR_=0,pitchActiveSlew_=0;
    float envelopeAlpha_=0.0f,wetStep_=0.0f,neutralStep_=0.0f;
    int block_=0,channels_=2,latency_=0,delayPosition_=0,delayLength_=1;
    ShifterProfile profile_=ShifterProfile::HighQuality40ms;
};

}} // namespace APEX::TuneLive
