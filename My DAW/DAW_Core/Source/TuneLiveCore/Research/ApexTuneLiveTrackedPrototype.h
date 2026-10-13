#pragma once
// EXPERIMENT ONLY: real source-sample streaming tracker -> scale policy ->
// period-guided shifter. No F0 oracle, no host PDC and NO production registration.
// Unlike the synthetic direct-shifter probe this exercises the actual YIN
// onset delay and the musical note-selection logic.
#include "../ApexTuneLiveVoiceControl.h"
#include "ApexTuneLivePeriodGuidedPrototype.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace APEX { namespace TuneLive { namespace Research {
class TrackedPrototype final {
public:
    void prepare(double sr,int channels=1) noexcept {
        rate_=(std::isfinite(sr) && sr>=8000.0 && sr<=192000.0)?sr:48000.0;
        channels_=std::clamp(channels,1,2);
        pitch_.prepare(rate_);
        shifter_.prepare(rate_,channels_);
        reset();
    }
    void reset() noexcept {
        pitch_.reset();
        policy_.reset();
        shifter_.reset();
        leftEnvelope_=rightEnvelope_=0.0f;
        detectedHz_=requestedSemitones_=0.0f;
        voiced_=false;
        estimates_=0;
        firstVoicedSample_=-1;
        processedSamples_=0;
        // Do not assume stale F0 after playback transport seeks or pauses.
        shifter_.setPeriodAndRatio(0.0,1.0);
    }
    // Set on a non-RT control thread before process(). Research bench
    // only: this is not yet wired to concurrent host automation.
    void setVoiceSettings(VoiceSettings settings) noexcept {
        settings_=settings;
    }
    void process(float* const* data,int channels,int n) noexcept {
        if(!data || channels!=channels_ || n<=0 ||
           !data[0] || (channels_==2 && !data[1])) return;
        const float alpha=static_cast<float>(1.0/(rate_*0.010+1.0));
        for(int i=0;i<n;++i) {
            // Read ORIGINAL microphone data before pitch processing.
            const float left=std::isfinite(data[0][i])?data[0][i]:0.0f;
            const float right=channels_==2 && std::isfinite(data[1][i])
                                 ?data[1][i]:0.0f;
            leftEnvelope_+=(std::abs(left)-leftEnvelope_)*alpha;
            rightEnvelope_+=(std::abs(right)-rightEnvelope_)*alpha;
            const float source=(channels_==2 && rightEnvelope_>1.2f*leftEnvelope_)
                                  ?right:left;
            if(pitch_.push(source)) {
                ++estimates_;
                auto decision=policy_.update(pitch_.latest(),settings_);
                voiced_=decision.voiced;
                if(voiced_) {
                    detectedHz_=pitch_.latest().hz;
                    requestedSemitones_=decision.shiftSemitones;
                    if(firstVoicedSample_<0)
                        firstVoicedSample_=processedSamples_;
                    const double ratio=std::pow(2.0,
                                     static_cast<double>(requestedSemitones_)/12.0);
                    shifter_.setPeriodAndRatio(detectedHz_,ratio);
                } else {
                    detectedHz_=requestedSemitones_=0.0f;
                    shifter_.setPeriodAndRatio(0.0,1.0);
                }
            }
            float* current[2]={data[0]+i,channels_==2?data[1]+i:nullptr};
            shifter_.process(current,channels_,1);
            ++processedSamples_;
        }
    }
    bool voiced() const noexcept {return voiced_;}
    float sourceHz() const noexcept {return detectedHz_;}
    float shiftSemitones() const noexcept {return requestedSemitones_;}
    int64_t processedSamples() const noexcept {return processedSamples_;}
    int64_t firstVoicedSample() const noexcept {return firstVoicedSample_;}
    int64_t estimates() const noexcept {return estimates_;}
    double trackerWindowMs() const noexcept {return pitch_.analysisWindowMs();}
    int requiredShifterHistorySamples() const noexcept {
        return shifter_.requiredHistorySamples();
    }
private:
    PitchTracker pitch_;
    VoiceControl policy_;
    PeriodGuidedPrototype shifter_;
    VoiceSettings settings_{};
    double rate_=48000.0;
    float leftEnvelope_=0.0f,rightEnvelope_=0.0f,detectedHz_=0.0f,
          requestedSemitones_=0.0f;
    int channels_=1;
    bool voiced_=false;
    int64_t estimates_=0,firstVoicedSample_=-1,processedSamples_=0;
};
}}} // namespace APEX::TuneLive::Research
