#pragma once
// APEX Tune Live — non-shipping harmonic prediction experiment.
//
// Unlike the existing ~40ms spectral shifter, this candidate does not
// deliberately delay the dry signal or use a forward-lookahead buffer.
// It reconstructs voiced partials from past samples, then drives those
// partials at the quantized target frequency. No known source-F0 oracle:
// original input samples go through the actual production PitchTracker and
// VoiceControl. Its *effective audible attack/quality latency* is NOT 1ms:
// F0 detection and harmonic coefficient settling still require cycles.
//
// EXPERIMENT ONLY: harmonic resynthesis can alter vowel identity, transient
// envelopes, consonants, phase and breath noise. No host registration or
// 1ms/PDC/Antares claim is permitted until full wet timing, voice listening,
// click, CPU, formant and ASIO mic-to-ear gates are independently green.
#include "../ApexTuneLiveVoiceControl.h"
#include "ApexTuneLiveEarlyOnsetProbe.h"
#include "ApexTuneLiveHighRegisterProbe.h"
#include "ApexTuneLiveUltraHighProbe.h"
#include "ApexTuneLiveMidRegisterProbe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace APEX { namespace TuneLive { namespace Research {

class HarmonicLivePrototype final {
public:
    static constexpr int kHarmonics=24;
    void prepare(double sampleRate,int channels=1) noexcept {
        rate_=(std::isfinite(sampleRate) && sampleRate>=8000.0 && sampleRate<=192000.0)
               ?sampleRate:48000.0;
        channels_=std::clamp(channels,1,2);
        tracker_.prepare(rate_);
        early_.prepare(rate_);
        high_.prepare(rate_);
        ultra_.prepare(rate_);
        mid_.prepare(rate_);
        // No dynamic allocation in prepare or process; control coefficients
        // are computed here or only when the pitch tracker emits a frame.
        slowWetStep_=1.0/(0.008*rate_+1.0);
        fastWetStep_=1.0/(0.001*rate_+1.0);
        envelopeStep_=static_cast<float>(1.0/(0.010*rate_+1.0));
        maxVoicedSamples_=static_cast<int>(3.0*rate_/55.0);
        releaseSamples_=static_cast<int>(std::ceil(0.001*rate_));
        inverseReleaseSamples_=1.0/releaseSamples_;
        reset();
    }
    void reset() noexcept {
        tracker_.reset();
        early_.reset();
        high_.reset();
        ultra_.reset();
        mid_.reset();
        policy_.reset();
        for(auto& ch:partials_)
            for(auto& p:ch)p={};
        phaseSource_={1.0,0.0};
        phaseOutput_={1.0,0.0};
        stepSource_={1.0,0.0};
        stepOutput_={1.0,0.0};
        sourceHz_=targetHz_=0.0;
        envelopeL_=envelopeR_=0.0f;
        wet_=targetWet_=0.0;
        activeWetStep_=slowWetStep_;
        partialStep_=0.0;
        activeHarmonics_=0;
        minimumVoicedSamples_=0;
        historyWritePosition_=historyFrames_=0;
        releaseRemaining_=0;
        lastCorrection_.fill(0.0);
        voiced_=false;
        sampleCount_=0;
        firstVoiceSample_=firstHalfWetSample_=firstEarlySample_=-1;
        firstHighSample_=-1;
        firstUltraSample_=-1;
        firstMidSample_=-1;
        lastLargeNoteJumpSample_=-1;
        lastVoicedEstimateSample_=-1;
        lastFastTransitionSample_=-1;
        fastTransitionCount_=0;
        largeNoteJumpCount_=0;
        voicedSamples_=0;
        initializedPhase_=false;
    }
    // Research settings are established off the audio callback; this
    // wrapper is not yet wired to concurrent host automation.
    void setVoiceSettings(VoiceSettings settings) noexcept {settings_=settings;}
    // Research-only optional early seed for >=200 Hz, with octave veto.
    // No production host exposure; primary YIN remains authoritative.
    void enableEarlyOnset(bool enabled) noexcept {earlyEnabled_=enabled;}
    // OFF by default. This guarded 96-sample high-register hypothesis
    // can pre-seed F0 for >=400Hz *new* notes only. This does NOT predict
    // anything in 1ms, nor replace the authoritative longer-window YIN.
    void enableHighRegisterOnset(bool enabled) noexcept {
        highRegisterEnabled_=enabled;
    }
    // Non-shipping hypothesis only: 80-frame first F0 for 430–1000Hz.
    // Green synthetic octave veto is NOT certified human voice quality.
    // Mid/normal/full YIN remain mandatory for lower-register singing.
    void enableUltraHighRegisterOnset(bool enabled) noexcept {
        ultraHighEnabled_=enabled;
    }
    // NONSHIPPING faster 4-hop YIN candidate, opt-in after prepare() and
    // before processing. The shorter window still requires two agreeing
    // frames and a measured octave veto.
    void enableUltraHighFastHopResearch(bool enabled) noexcept {
        ultra_.enableFastHopResearch(enabled);
    }
    // Experimental independent-confirmation gate; default OFF.
    void enableUltraLeapConsensusResearch(bool enabled) noexcept {
        ultraLeapConsensusEnabled_=enabled;
    }
    // Research-only 128-frame mid-range 260–420Hz note onset; forbidden
    // by default and does not predict newly sung pitches within 1ms.
    // Two agreeing acoustic frames plus lower-octave veto required.
    void enableMidRegisterOnset(bool enabled) noexcept {
        midRegisterEnabled_=enabled;
    }
    // OFF by default. This speculative transition seed requires two
    // independent, agreeing short-window F0 frames with an octave veto;
    // the ordinary longer YIN still supplies authoritative sustained pitch.
    // It cannot make first-note recognition a 1ms operation.
    void enableExperimentalEarlyTransitions(bool enabled) noexcept {
        fastTransitionsEnabled_=enabled;
    }
    // NONSHIPPING, OFF by default. On a >600-cent NOTE LEAP, the
    // resynth partials are deliberately reset. Existing code cleared the
    // target wet once but immediately restored it in updatePitch(), while
    // leaving current wet_ near 1; that could let newly relearned partials
    // enter the audio at full strength. Optional dry-reset makes the wet
    // envelope ramp back in after an observed source period. Research gate
    // requires human singer AB first; not an assertion about formants.
    void enableNoteLeapDryReset(bool enabled) noexcept {
        noteLeapDryResetEnabled_=enabled;
    }
    // Experimental residual-preserving voiced replacement:
    // subtract the tracked unshifted harmonics, insert shifted harmonics,
    // and retain current-input residual (consonants/breath/noise) instantly.
    // NOT a formant-verified production mode.
    void enableResidualPreservation(bool enabled) noexcept {residualMode_=enabled;}
    // EXPERIMENTAL: earlier *wet mix* onset, not earlier F0 recognition.
    // Default remains 3 periods / 8ms crossfade. In fast mode require
    // only one observed source period / 1ms crossfade. Neither bound
    // is a plugin-PDC nor a first-note latency guarantee.
    // Set only off the audio callback before processing; there is no
    // concurrently automated parameter until host safety is validated.
    void enableFastEngagement(bool enabled) noexcept {
        fastEngagement_=enabled;
    }
    // EXPERIMENT, OFF by default. Try allowing the >=400Hz candidate
    // harmonic coefficients to become audible after HALF of one newly
    // observed source period rather than one full period. This may be
    // faster but can damage attacks/formants; only objective and blind
    // real-singer A/B may decide whether it is safe for further trials.
    // No change to <400Hz or the conservative <160Hz fallback.
    // Must be configured only off the audio callback.
    void enableHalfPeriodEngagementResearch(bool enabled) noexcept {
        halfPeriodEngagementResearch_=enabled;
    }
    void process(float* const* data,int channels,int length) noexcept {
        if(!data || channels!=channels_ || length<=0 || !data[0] ||
           (channels_==2 && !data[1]))return;
        for(int i=0;i<length;++i) {
            std::array<float,2> dry{{safe(data[0][i]),
                 channels_==2?safe(data[1][i]):0.0f}};
            if(halfPeriodEngagementResearch_) {
                inputHistory_[static_cast<std::size_t>(historyWritePosition_)]=dry;
                historyWritePosition_=(historyWritePosition_+1)%kHistorySize;
                historyFrames_=std::min(historyFrames_+1,kHistorySize);
            }
            envelopeL_+=(std::abs(dry[0])-envelopeL_)*envelopeStep_;
            envelopeR_+=(std::abs(dry[1])-envelopeR_)*envelopeStep_;
            const float detector=(channels_==2 && envelopeR_>1.2f*envelopeL_)
                                   ?dry[1]:dry[0];
            const bool preliminary=earlyEnabled_ && early_.push(detector);
            const bool earlyHigh=highRegisterEnabled_ && high_.push(detector);
            const bool earlyUltra=ultraHighEnabled_ && ultra_.push(detector);
            const bool earlyMid=midRegisterEnabled_ && mid_.push(detector);
            if(tracker_.push(detector)) {
                const auto primary=tracker_.latest();
                // After an EARLY predicted octave switch, the full YIN
                // window still contains part of the PREVIOUS note. Never
                // rewind to a stale octave during this short overlap.
                // If full YIN agrees, or the window expires, it takes over.
                const bool overlapsPredictedLeap=
                    fastTransitionsEnabled_ && primary.voiced &&
                    lastFastTransitionSample_>=0 &&
                    (sampleCount_-lastFastTransitionSample_)<
                        static_cast<int64_t>(rate_*0.020) &&
                    primary.hz>=55.0f && sourceHz_>=55.0 &&
                    std::abs(1200.0*std::log2(primary.hz/sourceHz_))>650.0;
                if(!overlapsPredictedLeap)
                    updatePitch(primary,false);
            } else if(!voiced_ && earlyUltra && ultra_.latest().voiced &&
                      ultra_.latest().confidence>=0.95f) {
                // Truly observed samples: no known F0 injected, no 1ms
                // claim. Longer YIN re-checks its initial hypothesis.
                const auto e=ultra_.latest();
                const PitchTracker::Result estimate{e.hz,e.confidence,true};
                updatePitch(estimate,true);
                if(voiced_ && firstUltraSample_<0)
                    firstUltraSample_=sampleCount_;
            } else if(voiced_ && fastTransitionsEnabled_ &&
                      earlyUltra && ultra_.latest().voiced &&
                      ultra_.latest().confidence>=0.95f &&
                      (!ultraLeapConsensusEnabled_ ||
                       (high_.latest().voiced &&
                        high_.latest().confidence>=0.95f &&
                        high_.latest().hz>0.0f &&
                        std::abs(1200.0f*std::log2(
                            ultra_.latest().hz/high_.latest().hz))<65.0f)) &&
                      sourceHz_>=55.0 &&
                      lastVoicedEstimateSample_>=0 &&
                      (sampleCount_-lastVoicedEstimateSample_)<
                          static_cast<int64_t>(rate_*0.100) &&
                      (ultra_.latest().hz/sourceHz_)>1.45f &&
                      (lastFastTransitionSample_<0 ||
                       sampleCount_-lastFastTransitionSample_>
                          static_cast<int64_t>(rate_*0.045))) {
                // Optional UPWARD-only high-register note-leap hint.
                // The 80-frame tracker has passed dedicated 220/330/392Hz
                // false-octave stress and must agree across two frames.
                // Still a hypothesis, never claim 1ms/ASIO certification.
                const auto e=ultra_.latest();
                const PitchTracker::Result estimate{e.hz,e.confidence,true};
                updatePitch(estimate,true);
                lastFastTransitionSample_=sampleCount_;
                ++fastTransitionCount_;
            } else if(!voiced_ && earlyHigh && high_.latest().voiced &&
                      high_.latest().confidence>=0.95f) {
                // 2 agreeing short-window analyses of actual samples, then
                // later full YIN re-acquires as the trustworthy authority.
                const auto e=high_.latest();
                const PitchTracker::Result estimate{e.hz,e.confidence,true};
                updatePitch(estimate,true);
                if(voiced_ && firstHighSample_<0)
                    firstHighSample_=sampleCount_;
            } else if(!voiced_ && earlyMid && mid_.latest().voiced &&
                      mid_.latest().confidence>=0.95f) {
                // Lower-mid singer hypothesis is independently octave-vetoed;
                // long-history YIN must eventually confirm the estimate.
                const auto e=mid_.latest();
                const PitchTracker::Result estimate{e.hz,e.confidence,true};
                updatePitch(estimate,true);
                if(voiced_ && firstMidSample_<0)
                    firstMidSample_=sampleCount_;
            } else if(preliminary && early_.latest().voiced) {
                // Early detector is not given an oracle; it observes the
                // actual mic samples and requires two agreeing estimates.
                const auto e=early_.latest();
                const PitchTracker::Result estimate{e.hz,e.confidence,true};
                if(!voiced_) {
                    updatePitch(estimate,true);
                } else if(fastTransitionsEnabled_ && sourceHz_>=55.0 &&
                          e.confidence>=0.90f &&
                          std::abs(1200.0*std::log2(e.hz/sourceHz_))>650.0 &&
                          (lastFastTransitionSample_<0 ||
                           (sampleCount_-lastFastTransitionSample_)>
                                static_cast<int64_t>(rate_*0.045))) {
                    // Short-window octave disagreement is intentionally
                    // restricted to large jumps; ordinary vibrato/neighbor
                    // semitone shifts remain under the full YIN authority.
                    updatePitch(estimate,true);
                    lastFastTransitionSample_=sampleCount_;
                    ++fastTransitionCount_;
                }
            }
            std::array<double,2> synth{{0.0,0.0}};
            std::array<double,2> unshifted{{0.0,0.0}};
            if(voiced_) {
                // Maturity only needs the longest supported pitch period.
                // Saturate the counter so a sustained input cannot overflow it.
                if(voicedSamples_<maxVoicedSamples_)
                    ++voicedSamples_;
                if(!initializedPhase_) {
                    // Both oscillators share the same instantaneous phase
                    // before diverging in frequency. Do not confuse this
                    // phase continuity with verified transient latency.
                    phaseOutput_=phaseSource_;
                    initializedPhase_=true;
                }
                // The frequency bounds stay constant between detector frames.
                Unit src=phaseSource_,out=phaseOutput_;
                for(int h=0;h<activeHarmonics_;++h) {
                    for(int c=0;c<channels_;++c) {
                        const auto channel=static_cast<std::size_t>(c);
                        auto& p=partials_[channel][static_cast<std::size_t>(h)];
                        p.cosine+=partialStep_*(2.0*dry[channel]*src.real-p.cosine);
                        p.sine+=partialStep_*(2.0*dry[channel]*src.imag-p.sine);
                        synth[channel]+=
                            p.cosine*out.real+p.sine*out.imag;
                        unshifted[channel]+=
                            p.cosine*src.real+p.sine*src.imag;
                    }
                    src=multiply(src,phaseSource_);
                    out=multiply(out,phaseOutput_);
                }
            }
            const bool mature=voiced_ && voicedSamples_>=minimumVoicedSamples_;
            const double requested=mature?targetWet_:0.0;
            wet_+=(requested-wet_)*activeWetStep_;
            if(firstHalfWetSample_<0 && wet_>=0.5)
                firstHalfWetSample_=sampleCount_;
            const auto release=renderResidualRelease();
            for(int c=0;c<channels_;++c) {
                const auto channel=static_cast<std::size_t>(c);
                const double predicted=residualMode_
                    ?dry[channel]+synth[channel]-unshifted[channel]
                    :synth[channel];
                const double result=(1.0-wet_)*dry[channel]+wet_*predicted+
                                    release[channel];
                const float output=std::isfinite(result)
                    ?static_cast<float>(std::clamp(result,-8.0,8.0)):0.0f;
                if(residualMode_)lastCorrection_[channel]=output-dry[channel];
                data[c][i]=output;
            }
            if(voiced_) {
                phaseSource_=multiply(phaseSource_,stepSource_);
                phaseOutput_=multiply(phaseOutput_,stepOutput_);
                // Bound accumulated oscillator floating-point drift.
                if((sampleCount_&1023)==1023) {
                    normalize(phaseSource_);
                    normalize(phaseOutput_);
                }
            }
            ++sampleCount_;
        }
    }
    bool voiced() const noexcept {return voiced_;}
    double sourceHz() const noexcept {return sourceHz_;}
    double targetHz() const noexcept {return targetHz_;}
    double researchWetMix() const noexcept {return wet_;}
    int64_t firstVoiceSample() const noexcept {return firstVoiceSample_;}
    int64_t firstEarlySample() const noexcept {return firstEarlySample_;}
    int64_t firstHighSample() const noexcept {return firstHighSample_;}
    int64_t firstUltraSample() const noexcept {return firstUltraSample_;}
    int64_t firstMidSample() const noexcept {return firstMidSample_;}
    int largeNoteJumpCount() const noexcept {return largeNoteJumpCount_;}
    int fastTransitionCount() const noexcept {return fastTransitionCount_;}
    int64_t lastFastTransitionSample() const noexcept {return lastFastTransitionSample_;}
    int64_t lastLargeNoteJumpSample() const noexcept {return lastLargeNoteJumpSample_;}
    int64_t firstHalfWetSample() const noexcept {return firstHalfWetSample_;}
    int64_t processedSamples() const noexcept {return sampleCount_;}
    double trackerWindowMs() const noexcept {return tracker_.analysisWindowMs();}
private:
    struct Unit {double real=1.0,imag=0.0;};
    struct Partial {double cosine=0.0,sine=0.0;};
    using Harmonics = std::array<std::array<Partial,kHarmonics>,2>;

    void seedHarmonicsFromObservedPeriod() noexcept {
        // The detector has already listened to several periods. Project the
        // most recent one onto the harmonic model before the wet ramp starts.
        // This history is read only for initialization. Audio still streams out
        // sample by sample, including at the start of a new note.
        const double sourcePeriod=rate_/sourceHz_;
        if(historyFrames_<static_cast<int>(std::ceil(sourcePeriod))+1)return;
        constexpr int points=64;
        constexpr double phaseStride=-6.28318530717958647692/points;
        const Unit previousStep{std::cos(phaseStride),std::sin(phaseStride)};
        Unit pastPhase=phaseSource_;
        for(int point=0;point<points;++point) {
            const double age=point*sourcePeriod/points;
            const int whole=static_cast<int>(age);
            const double fraction=age-whole;
            const int recent=(historyWritePosition_+kHistorySize-1-whole)%kHistorySize;
            const int older=(recent+kHistorySize-1)%kHistorySize;
            const auto& recentFrame=inputHistory_[static_cast<std::size_t>(recent)];
            const auto& olderFrame=inputHistory_[static_cast<std::size_t>(older)];
            std::array<double,2> weights{};
            for(int c=0;c<channels_;++c) {
                const auto channel=static_cast<std::size_t>(c);
                weights[channel]=(2.0/points)*
                    (recentFrame[channel]+fraction*(olderFrame[channel]-recentFrame[channel]));
            }
            Unit harmonic=pastPhase;
            for(int h=0;h<activeHarmonics_;++h) {
                for(int c=0;c<channels_;++c) {
                    const auto channel=static_cast<std::size_t>(c);
                    auto& partial=partials_[channel][static_cast<std::size_t>(h)];
                    partial.cosine+=weights[channel]*harmonic.real;
                    partial.sine+=weights[channel]*harmonic.imag;
                }
                harmonic=multiply(harmonic,pastPhase);
            }
            pastPhase=multiply(pastPhase,previousStep);
        }
        voicedSamples_=minimumVoicedSamples_;
    }

    void releaseResidualCorrection() noexcept {
        if(!residualMode_ || !voiced_)return;
        // A discarded harmonic model must not cut a live correction at
        // arbitrary phase. Keep its oscillators running through a short
        // crossfade using frozen coefficients, without holding a DC offset.
        releasePartials_=partials_;
        releaseSource_=phaseSource_;
        releaseOutput_=phaseOutput_;
        releaseSourceStep_=stepSource_;
        releaseOutputStep_=stepOutput_;
        releaseHarmonics_=activeHarmonics_;
        releaseWet_=wet_;
        releaseRemaining_=releaseSamples_;
    }

    std::array<double,2> renderResidualRelease() noexcept {
        std::array<double,2> delta{};
        if(releaseRemaining_==0)return delta;
        Unit src=releaseSource_,out=releaseOutput_;
        for(int h=0;h<releaseHarmonics_;++h) {
            for(int c=0;c<channels_;++c) {
                const auto channel=static_cast<std::size_t>(c);
                const auto& partial=releasePartials_[channel][static_cast<std::size_t>(h)];
                delta[channel]+=partial.cosine*(out.real-src.real)+
                                partial.sine*(out.imag-src.imag);
            }
            src=multiply(src,releaseSource_);
            out=multiply(out,releaseOutput_);
        }
        const double remaining=releaseRemaining_*inverseReleaseSamples_;
        const double gain=remaining*remaining*(3.0-2.0*remaining);
        for(int c=0;c<channels_;++c) {
            const auto channel=static_cast<std::size_t>(c);
            delta[channel]*=releaseWet_;
            if(releaseRemaining_==releaseSamples_)
                releaseOffset_[channel]=lastCorrection_[channel]-delta[channel];
            delta[channel]=gain*(delta[channel]+gain*releaseOffset_[channel]);
        }
        releaseSource_=multiply(releaseSource_,releaseSourceStep_);
        releaseOutput_=multiply(releaseOutput_,releaseOutputStep_);
        --releaseRemaining_;
        return delta;
    }

    void updatePitch(PitchTracker::Result estimate,bool earlySeed) noexcept {
        const auto decision=policy_.update(estimate,settings_);
        const bool acceptable=decision.voiced &&
                 std::isfinite(estimate.hz) &&
                 estimate.hz>=55.0f && estimate.hz<=1200.0f &&
                 std::isfinite(decision.shiftSemitones);
        if(acceptable) {
            const double ratio=std::pow(2.0,
                std::clamp(static_cast<double>(decision.shiftSemitones),
                           -2.5,2.5)/12.0);
            // A large sung-note leap invalidates harmonic coefficients from
            // the old vowel. Decay to instantaneous dry/residual and learn
            // the new fundamental before fading corrected partials back in.
            // This cannot remove YIN reacquisition time, but avoids using
            // an A3 spectral envelope to synthesize an unrelated A4 attack.
            // Full YIN can briefly mark audio unvoiced across an abrupt
            // leap. Preserve the last trustworthy source pitch for 100 ms
            // rather than requiring two consecutive voiced frames. A genuine
            // long silence never triggers this cross-note comparison.
            const bool recentVoice=lastVoicedEstimateSample_>=0 &&
                (sampleCount_-lastVoicedEstimateSample_)<static_cast<int64_t>(rate_*0.100);
            if(sourceHz_>=55.0 && recentVoice &&
                std::abs(1200.0*std::log2(estimate.hz/sourceHz_))>600.0) {
                releaseResidualCorrection();
                for(auto& channel:partials_)
                    for(auto& partial:channel)partial={};
                voicedSamples_=0;
                targetWet_=0.0;
                if(noteLeapDryResetEnabled_)wet_=0.0;
                initializedPhase_=false;
                lastLargeNoteJumpSample_=sampleCount_;
                ++largeNoteJumpCount_;
            }
            sourceHz_=estimate.hz;
            lastVoicedEstimateSample_=sampleCount_;
            targetHz_=sourceHz_*ratio;
            // Low-register singers need the longer ramp to avoid octave
            // artifacts. Cache the engagement policy with the detected pitch.
            const bool fastVoice=fastEngagement_ && sourceHz_>=160.0;
            const double requiredCycles=fastVoice
                ? (halfPeriodEngagementResearch_ && sourceHz_>=400.0 ? 0.5 : 1.0)
                : 3.0;
            minimumVoicedSamples_=static_cast<int>(requiredCycles*rate_/sourceHz_);
            activeWetStep_=fastVoice?fastWetStep_:slowWetStep_;
            // Both oscillators must stay in band, including when shifting down.
            const double higherFrequency=std::max(sourceHz_,targetHz_);
            const int previousHarmonics=activeHarmonics_;
            activeHarmonics_=kHarmonics;
            while(activeHarmonics_*higherFrequency>=rate_*0.46)
                --activeHarmonics_;
            for(int c=0;c<channels_;++c)
                for(int h=activeHarmonics_;h<previousHarmonics;++h)
                    partials_[static_cast<std::size_t>(c)][static_cast<std::size_t>(h)]={};
            const double radians=2.0*3.14159265358979323846/rate_;
            stepSource_={std::cos(sourceHz_*radians),
                         std::sin(sourceHz_*radians)};
            stepOutput_={std::cos(targetHz_*radians),
                         std::sin(targetHz_*radians)};
            partialStep_=std::clamp(sourceHz_/(2.0*rate_),0.00001,0.2);
            if(!voiced_) {
                for(auto& channel:partials_)
                    for(auto& partial:channel)partial={};
                voicedSamples_=0;
                initializedPhase_=false;
                if(fastVoice && halfPeriodEngagementResearch_ && !recentVoice && earlySeed &&
                   sourceHz_>=400.0 && estimate.confidence>=0.95f)
                    seedHarmonicsFromObservedPeriod();
                if(firstVoiceSample_<0)firstVoiceSample_=sampleCount_;
            }
            if(earlySeed && firstEarlySample_<0)
                firstEarlySample_=sampleCount_;
            voiced_=true;
            targetWet_=std::abs(decision.shiftSemitones)>0.01f?1.0:0.0;
        } else {
            releaseResidualCorrection();
            voiced_=false;
            targetWet_=0.0;
            voicedSamples_=0;
        }
    }
    static Unit multiply(Unit a,Unit b) noexcept {
        return {a.real*b.real-a.imag*b.imag,
                a.real*b.imag+a.imag*b.real};
    }
    static void normalize(Unit& u) noexcept {
        const double mag=std::sqrt(u.real*u.real+u.imag*u.imag);
        if(mag>1.0e-9 && std::isfinite(mag)) {
            u.real/=mag;u.imag/=mag;
        } else u={1.0,0.0};
    }
    static float safe(float x) noexcept {
        if(!std::isfinite(x)||std::abs(x)<1.0e-20f)return 0.0f;
        return std::clamp(x,-8.0f,8.0f);
    }
    PitchTracker tracker_;
    EarlyOnsetProbe early_;
    HighRegisterOnsetProbe high_;
    UltraHighRegisterProbe ultra_;
    MidRegisterOnsetProbe mid_;
    VoiceControl policy_;
    VoiceSettings settings_{};
    // A period at 400Hz uses at most 480 frames at the maximum rate.
    static constexpr int kHistorySize=512;
    std::array<std::array<float,2>,kHistorySize> inputHistory_{};
    Harmonics partials_{},releasePartials_{};
    std::array<double,2> lastCorrection_{},releaseOffset_{};
    Unit phaseSource_{},phaseOutput_{},stepSource_{},stepOutput_{};
    Unit releaseSource_{},releaseOutput_{},releaseSourceStep_{},releaseOutputStep_{};
    double rate_=48000.0,sourceHz_=0.0,targetHz_=0.0,
           wet_=0.0,targetWet_=0.0,activeWetStep_=0.0,slowWetStep_=0.0,
           fastWetStep_=0.0,partialStep_=0.0,inverseReleaseSamples_=0.0,releaseWet_=0.0;
    float envelopeL_=0.0f,envelopeR_=0.0f,envelopeStep_=0.0f;
    int channels_=1,voicedSamples_=0,maxVoicedSamples_=0,
        minimumVoicedSamples_=0,activeHarmonics_=0,releaseSamples_=0,
        releaseRemaining_=0,releaseHarmonics_=0,historyWritePosition_=0,historyFrames_=0;
    int64_t sampleCount_=0,firstVoiceSample_=-1,firstHalfWetSample_=-1,
            firstEarlySample_=-1,lastLargeNoteJumpSample_=-1,
            lastVoicedEstimateSample_=-1,lastFastTransitionSample_=-1,
            firstHighSample_=-1,firstMidSample_=-1,
            firstUltraSample_=-1;
    int largeNoteJumpCount_=0,fastTransitionCount_=0;
    bool voiced_=false,initializedPhase_=false,earlyEnabled_=false,
         fastTransitionsEnabled_=false,residualMode_=false,
         fastEngagement_=false,highRegisterEnabled_=false,
         midRegisterEnabled_=false,ultraHighEnabled_=false,
         noteLeapDryResetEnabled_=false,ultraLeapConsensusEnabled_=false,
         halfPeriodEngagementResearch_=false;
};
}}} // namespace APEX::TuneLive::Research
