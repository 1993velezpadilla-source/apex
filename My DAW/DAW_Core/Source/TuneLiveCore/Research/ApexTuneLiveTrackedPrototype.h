#pragma once
// EXPERIMENT ONLY: real source-sample streaming tracker -> scale policy ->
// period-guided shifter. No F0 oracle, no host PDC and NO production registration.
// Unlike the synthetic direct-shifter probe this exercises the actual YIN
// onset delay and the musical note-selection logic.
#include "../ApexTuneLiveVoiceControl.h"
#include "ApexTuneLivePeriodGuidedPrototype.h"
#include "ApexTuneLiveEarlyOnsetProbe.h"
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
        early_.prepare(rate_);
        shifter_.prepare(rate_,channels_);
        reset();
    }
    void reset() noexcept {
        pitch_.reset();
        early_.reset();
        policy_.reset();
        shifter_.reset();
        leftEnvelope_=rightEnvelope_=0.0f;
        detectedHz_=requestedSemitones_=0.0f;
        voiced_=false;
        estimates_=0;
        firstVoicedSample_=-1;
        firstEarlySample_=-1;
        processedSamples_=0;
        // Do not assume stale F0 after playback transport seeks or pauses.
        shifter_.setPeriodAndRatio(0.0,1.0);
    }
    // Set on a non-RT control thread before process(). Research bench
    // only: this is not yet wired to concurrent host automation.
    void setVoiceSettings(VoiceSettings settings) noexcept {
        settings_=settings;
    }
    // Disabled by default, including in every shipping APEX Native insert.
    // This only seeds the research shifter until the existing long-window
    // full YIN authority has its first verified pitch estimate.
    void enableExperimentalEarlyOnset(bool enabled) noexcept {
        earlyEnabled_=enabled;
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
            const bool preliminary=earlyEnabled_ && early_.push(source);
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
            } else if(preliminary && !voiced_ && early_.latest().voiced) {
                // Earliest verified harmonic onset is used as a SEED only.
                // Main YIN replaces this on its next validated estimate.
                const auto e=early_.latest();
                const PitchTracker::Result prelim{e.hz,e.confidence,true};
                const auto decision=policy_.update(prelim,settings_);
                if(decision.voiced) {
                    voiced_=true;
                    detectedHz_=e.hz;
                    requestedSemitones_=decision.shiftSemitones;
                    if(firstVoicedSample_<0)firstVoicedSample_=processedSamples_;
                    if(firstEarlySample_<0)firstEarlySample_=processedSamples_;
                    shifter_.setPeriodAndRatio(detectedHz_,
                          std::pow(2.0,static_cast<double>(requestedSemitones_)/12.0));
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
    int64_t firstEarlySample() const noexcept {return firstEarlySample_;}
    int64_t estimates() const noexcept {return estimates_;}
    double trackerWindowMs() const noexcept {return pitch_.analysisWindowMs();}
    int requiredShifterHistorySamples() const noexcept {
        return shifter_.requiredHistorySamples();
    }
    // Diagnostic only. Exposing wet progression helps count the real
    // detector-to-audible-correction delay without falsely reporting PDC.
    double researchWetMix() const noexcept {return shifter_.wetMix();}
    double researchAppliedRatio() const noexcept {return shifter_.smoothedRatio();}
private:
    PitchTracker pitch_;
    EarlyOnsetProbe early_;
    VoiceControl policy_;
    PeriodGuidedPrototype shifter_;
    VoiceSettings settings_{};
    double rate_=48000.0;
    float leftEnvelope_=0.0f,rightEnvelope_=0.0f,detectedHz_=0.0f,
          requestedSemitones_=0.0f;
    int channels_=1;
    bool voiced_=false;
    int64_t estimates_=0,firstVoicedSample_=-1,firstEarlySample_=-1,
            processedSamples_=0;
    bool earlyEnabled_=false;
};
}}} // namespace APEX::TuneLive::Research
