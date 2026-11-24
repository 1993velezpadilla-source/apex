#pragma once

#include "ApexTuneLiveSettings.h"
#include "ApexTuneLiveVoiceControl.h"
#include "ApexTuneLiveRealtimeTracker.h"
#include <array>
#include <cmath>
#include <cstddef>

namespace APEX::TuneLive {

// Causal vocal renderer for monitoring. It adds a correction to the current
// input sample. A joint harmonic model estimates the voiced signal, leaving
// breath, consonants and other nonperiodic energy in the original signal.
class RealtimeCore final {
public:
    void prepare(double sampleRate, int channels) noexcept
    {
        rate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 192000.0
            ? sampleRate : 48000.0;
        channels_ = std::clamp(channels, 1, 2);
        tracker_.prepare(rate_);
        const int oneMillisecond = static_cast<int>(rate_ * 0.001);
        fastRetuneSamples_ = std::min(oneMillisecond, tracker_.hopSamples());
        fastRetuneProgress_ = 1.0 / fastRetuneSamples_;
        fastWetStep_ = -std::expm1(std::log(0.05) / oneMillisecond);
        inputLevelStep_ = 1.0 / (0.004 * rate_ + 1.0);
        wetStep_ = 1.0 / (0.0015 * rate_ + 1.0);
        formantStep_ = 1.0 / (0.003 * rate_ + 1.0);
        agreementStep_ = 1.0 / (0.00030 * rate_ + 1.0);
        // Short detector gaps must fade correction but preserve the fitted
        // waveform. An immediate bank tear-down on every missed 1-ms hop
        // clicks on breathy syllables and vowel/consonant transitions.
        unvoicedHoldHops_ = std::max(2, static_cast<int>(
            std::ceil(0.045 * rate_ / tracker_.hopSamples())));
        // Preserve instantaneous transient attenuation; slow only the
        // correction recovery to prevent sharp upward gain modulation.
        correctionRestoreStep_ = 1.0 / (0.004 * rate_ + 1.0);
        // Research variant only: glide harmonic oscillator F0 over 4ms
        // instead of reprogramming its frequency at every pitch-tracker hop.
        // Coefficients update every 16 samples; no memory or locks in callback.
        phaseSlewStep_ = 1.0 - std::exp(-kControlHop / (rate_ * 0.004));
        researchDecaySamples_ = std::max(2, static_cast<int>(std::ceil(0.005 * rate_)));
        decaySamples_ = std::max(2, static_cast<int>(std::ceil(0.0015 * rate_)));
        neutralHoldSamples_ = static_cast<int>(std::lround(0.045 * rate_));
        reset();
        prepared_ = true;
    }

    void reset() noexcept
    {
        tracker_.reset();
        voice_.reset();
        bank_ = {};
        outgoing_ = {};
        envelope_.fill(0.0);
        lastCorrection_.fill(0.0);
        lastDry_.fill(0.0);
        releaseOffset_.fill(0.0);
        position_ = filled_ = 0;
        targetShift_ = shift_ = confidence_ = wet_ = releaseAmount_ = 0.0;
        retune_ = -1.0f;
        controlLeft_ = decayLeft_ = neutralLeft_ = fastRetuneLeft_ = 0;
        missingPitchHops_ = postDropoutGuardHops_ = 0;
        phaseLeft_ = 0;
        phaseHz_ = 0.0;
        waveformReleaseCount_ = 0;
        frequency_ = 0.0f;
    }

    static constexpr int latencySamples() noexcept { return 0; }
    float frequencyHz() const noexcept { return frequency_; }
    double correctionMix() const noexcept { return wet_; }
    double appliedShiftSemitones() const noexcept { return shift_; }
    // Read after processing (test/diagnostic only; no cross-thread contract).
    unsigned waveformReleaseCount() const noexcept { return waveformReleaseCount_; }

    // Deliberately NOT a user-facing plug-in setting. Set only before process()
    // in isolated research tests. The shipping mode remains bit-for-bit default.
    // The host may automate this control at a 128-frame boundary.
    // Replacing the oscillator's mode in the middle of a vocal phrase can
    // itself click; arm only when both live and release banks have finished.
    void setResearchPhaseSlew(bool enabled) noexcept
    {
        requestedResearchPhaseSlew_ = enabled;
        if (bank_.size == 0 && decayLeft_ <= 0 && researchPhaseSlew_ != enabled)
        {
            researchPhaseSlew_ = enabled;
            phaseHz_ = 0.0;
            phaseLeft_ = 0;
        }
    }
    bool researchPhaseSlewEnabledForTest() const noexcept { return researchPhaseSlew_; }

    void process(float* const* data, int channels, int samples, const DspSettings& settings) noexcept
    {
        const bool stereo = channels == 2;
        if (!prepared_ || !data || channels != channels_ || samples <= 0 ||
            !data[0] || (stereo && !data[1]))
            return;

        const auto unit = [](float value, float fallback) {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
        };
        const float retune = unit(settings.retune, 0.20f);
        const bool fastest = retune == 0.0f;
        if (retune != retune_)
        {
            retune_ = retune;
            if (fastest)
                beginFastRetune();
            else
            {
                const double time = (1.0 + 149.0 * retune) * 0.001;
                retuneStep_ = 1.0 - std::exp(-kControlHop / (rate_ * time));
                fastRetuneLeft_ = 0;
            }
        }
        tracker_.setVocalRange(settings.vocalRange);
        const VoiceSettings policy{
            std::clamp(settings.key, 0, 11),
            std::clamp(settings.scale, 0, 2),
            unit(settings.correction, 0.85f),
            std::isfinite(settings.referenceHz) ? std::clamp(settings.referenceHz, 430.0f, 450.0f) : 440.0f,
            unit(settings.humanize, 0.70f),
            unit(settings.flexTune, 0.35f),
            0.35f * unit(settings.stability, 0.40f),
            static_cast<float>(tracker_.hopMilliseconds()),
            unit(settings.vibratoPreserve, 0.0f)
        };
        const double mix = settings.bypass ? 0.0 : unit(settings.wet, 1.0f);

        for (int i = 0; i < samples; ++i)
        {
            Sample input{clean(data[0][i]), stereo ? clean(data[1][i]) : 0.0};
            for (int c = 0; c < channels_; ++c)
            {
                const auto channel = static_cast<std::size_t>(c);
                envelope_[channel] += inputLevelStep_ * (input[channel] * input[channel] - envelope_[channel]);
            }
            const auto strongest = stereo && envelope_[1] > 1.44 * envelope_[0] ? 1u : 0u;
            history_[position_] = {static_cast<float>(input[0]), static_cast<float>(input[1])};
            position_ = (position_ + 1) & (kHistory - 1);
            filled_ = std::min(filled_ + 1, kHistory);

            const bool pitchUpdated = tracker_.push(static_cast<float>(input[strongest]));
            if (pitchUpdated)
                followPitch(tracker_.latest(), policy, settings.preserveFormants);

            if (fastest)
                advanceFastRetune(pitchUpdated);
            else if (--controlLeft_ <= 0)
            {
                advanceControl();
                controlLeft_ = kControlHop;
            }
            // Continuous phase is more than keeping the phase phasor alive:
            // the frequency driving that phasor must not jump on each
            // detector update. Research mode glides only phase increments;
            // musical target note and all wet safety gates remain unchanged.
            if (researchPhaseSlew_ && bank_.size > 0 && --phaseLeft_ <= 0)
            {
                phaseLeft_ = kControlHop;
                phaseHz_ += phaseSlewStep_ * (bank_.hz - phaseHz_);
                bank_.sourceStep = Phase::rotate(kTau * phaseHz_ / rate_);
                updateDestinationStep();
            }

            // An active note can cross the target pitch several times per
            // vibrato cycle. Keep the current phase-coherent renderer through
            // those crossings instead of crossfading into out-of-phase dry.
            if (std::abs(targetShift_) >= 0.01 || std::abs(shift_) >= 0.01)
                neutralLeft_ = neutralHoldSamples_;
            else if (neutralLeft_ > 0)
                --neutralLeft_;
            const bool correcting = bank_.size > 0 && neutralLeft_ > 0 && policy.correction > 0.0f;
            const double wetTarget = correcting ? confidence_ * mix : 0.0;
            const double wetStep = fastest && wetTarget > wet_ ? fastWetStep_ : wetStep_;
            wet_ += wetStep * (wetTarget - wet_);
            if (wet_ < 1.0e-10)
                wet_ = 0.0;
            if (mix == 0.0 && wet_ == 0.0)
                decayLeft_ = 0;

            // The old and freshly detected note must be *complementary*
            // during a bank handoff. Previously both corrections were at
            // near-full level for the first millisecond, making a transient
            // at pitch leaps despite a smoothed phase increment. Keep the
            // existing decay duration and apply the envelope in both modes.
            double incomingWeight = 1.0;
            if (decayLeft_ > 0 && bank_.size > 0)
            {
                const double t = static_cast<double>(decayLeft_) / decaySpan_;
                const double oldWeight = t * t * (3.0 - 2.0 * t);
                incomingWeight = 1.0 - oldWeight;
            }
            Sample delta = renderOutgoing(mix);
            if (bank_.size > 0)
            {
                if (incomingWeight < 1.0)
                {
                    Sample incoming{};
                    renderAndLearn(input, incoming);
                    for (int c = 0; c < channels_; ++c)
                        delta[static_cast<std::size_t>(c)] +=
                            incomingWeight * incoming[static_cast<std::size_t>(c)];
                }
                else
                    renderAndLearn(input, delta);
            }
            for (int c = 0; c < channels_; ++c)
            {
                const auto channel = static_cast<std::size_t>(c);
                // True dry transients are never touched. Only the synthesized
                // pitch correction is derivative-limited when a phase-model
                // reacquisition produces a one-sample step (audible click).
                // 0.18 FS/sample exceeds the normal note-transition peak
                // measured by the existing tracking gauntlet (~0.162), but
                // eliminates the 0.28-FS dropout pop without buffering/PDC.
                // Experimental transition safety: a sudden loss of model
                // agreement means the synthetic correction can collapse
                // by >0.1 FS in one sample, even before a new pitch is
                // detected. Hold the *correction derivative* to the normal
                // harmonic-band scale during those rare transitions. This
                // never touches the dry sample, and the shipping branch
                // keeps its existing, wider 0.18 protection unchanged.
                const bool weakModel = bank_.size > 0 &&
                    bank_.correctionGain[channel] < 0.80;
                const bool outgoingTransition = decayLeft_ > 0 && outgoing_.size > 0;
                // Preserve natural high-frequency vocal harmonics: full
                // time correction slew made *more* suspicious events on real
                // singers. Keep the tight guard during a known model fade,
                // and reserve the separate output-slope guard for rare gross
                // correction-only discontinuities.
                const double maxCorrectionStep =
                    researchPhaseSlew_ && (weakModel || outgoingTransition)
                        ? 0.034 : 0.18;
                double correction = std::clamp(
                    delta[channel], lastCorrection_[channel] - maxCorrectionStep,
                    lastCorrection_[channel] + maxCorrectionStep);
                if (researchPhaseSlew_)
                {
                    // The original waveform's instantaneous derivative is
                    // our protection for authentic sharp consonants and
                    // loud high-register partials. A gross added-sample
                    // discontinuity must be well above both the dry movement
                    // and the normal corrected harmonic slope before we
                    // adjust it. Otherwise pass the correction unchanged.
                    const double drySlope = input[channel] - lastDry_[channel];
                    const double correctionSlope = correction - lastCorrection_[channel];
                    const double requestedOutputSlope = drySlope + correctionSlope;
                    const double naturalCap = std::max(0.040,
                        std::abs(drySlope) + 0.010);
                    // At strong high-register vowels a 0.04-FS correction
                    // impulse can add to the perfectly natural dry slope.
                    // Detect the *excess over dry*, not a blanket high-pass
                    // that clips harmonic-rich singing every sample.
                    const bool anomaly = std::abs(correctionSlope) > 0.040 &&
                        std::abs(requestedOutputSlope) > naturalCap + 0.008;
                    if (anomaly)
                        correction += std::clamp(requestedOutputSlope,
                            -naturalCap, naturalCap) - requestedOutputSlope;
                }
                data[c][i] = static_cast<float>(std::clamp(input[channel] + correction, -8.0, 8.0));
                lastCorrection_[channel] = static_cast<double>(data[c][i]) - input[channel];
                if (researchPhaseSlew_)
                    lastDry_[channel] = input[channel];
            }
        }
    }

private:
    static constexpr unsigned kHistory = 4096;
    static constexpr int kPartials = 40;
    static constexpr int kControlHop = 16;
    static constexpr double kTau = 6.28318530717958647692;
    using Sample = std::array<double, 2>;

    struct Phase {
        double real = 1.0, imag = 0.0;
        Phase operator*(Phase other) const noexcept
        {
            return {real * other.real - imag * other.imag, real * other.imag + imag * other.real};
        }
        static Phase rotate(double angle) noexcept { return {std::cos(angle), std::sin(angle)}; }
    };
    struct Partial {
        double real = 0.0, imag = 0.0;
        double at(Phase phase) const noexcept { return real * phase.real + imag * phase.imag; }
    };
    struct Bank {
        std::array<std::array<Partial, kPartials>, 2> partials{};
        std::array<double, kPartials> gain{}, targetGain{}, weight{};
        Phase source, destination, sourceStep, destinationStep;
        Sample residualPower{}, correctionGain{1.0, 1.0};
        double hz = 0.0, ratio = 1.0, learningStep = 0.0;
        int size = 0;
    };

    static double clean(float value) noexcept
    {
        return std::isfinite(value) && std::abs(value) >= 1.0e-20f
            ? std::clamp(value, -8.0f, 8.0f) : 0.0;
    }

    Sample historyAt(double age) const noexcept
    {
        const auto whole = static_cast<unsigned>(age);
        const double fraction = age - whole;
        const auto current = (position_ - 1u - whole) & (kHistory - 1);
        const auto prior = (current - 1u) & (kHistory - 1);
        return {
            history_[current][0] + fraction * (history_[prior][0] - history_[current][0]),
            history_[current][1] + fraction * (history_[prior][1] - history_[current][1])
        };
    }

    void initialiseWaveform() noexcept
    {
        const double period = rate_ / bank_.hz;
        if (filled_ < static_cast<unsigned>(period) + 2u)
            return;
        constexpr int points = 96;
        const auto backward = Phase::rotate(-kTau / points);
        Phase pitchPhase;
        // Fit the latest complete input cycle at the detected period. At the
        // first confident estimate, the renderer already has a waveform.
        for (int n = 0; n < points; ++n)
        {
            const auto input = historyAt(n * period / points);
            Phase phase = pitchPhase;
            for (int h = 0; h < bank_.size; ++h)
            {
                const auto bin = static_cast<std::size_t>(h);
                for (int c = 0; c < channels_; ++c)
                {
                    const auto channel = static_cast<std::size_t>(c);
                    auto& p = bank_.partials[channel][bin];
                    const double sample = input[channel] * (2.0 / points);
                    p.real += sample * phase.real;
                    p.imag += sample * phase.imag;
                }
                phase = phase * pitchPhase;
            }
            pitchPhase = pitchPhase * backward;
        }
    }

    void followPitch(PitchTracker::Result pitch, VoiceSettings policy, bool preserveFormants) noexcept
    {
        if (!pitch.voiced || !std::isfinite(pitch.hz) || pitch.hz <= 0.0f)
        {
            handlePitchLoss();
            return;
        }
        // The pitch detector observes low-pass-filtered history. During an
        // 8-ms vowel gap that history can still return a highly confident
        // spurious 55-Hz subharmonic even though the microphone is silent.
        // Require the most recent 2 ms of *raw* input to carry real energy
        // before allowing a new note to change the oscillator phase bank.
        // This fixed 192-frame bound is inexpensive at 192 kHz and alloc-free.
        const unsigned recentCount = std::min(filled_,
            static_cast<unsigned>(std::clamp(static_cast<int>(rate_ * 0.002), 16, 192)));
        double recentEnergy = 0.0;
        for (unsigned back = 0; back < recentCount; ++back)
        {
            const auto& sample = history_[(position_ - 1u - back) & (kHistory - 1)];
            const double left = sample[0], right = sample[1];
            recentEnergy += std::max(left * left, right * right);
        }
        if (recentCount < 16u || recentEnergy < 1.0e-6 * recentCount)
        {
            handlePitchLoss();
            return;
        }
        const auto decision = voice_.update(pitch, policy);
        if (!decision.voiced)
        {
            handlePitchLoss();
            return;
        }
        // Reacquired detector tones can briefly select a harmonic/octave
        // after an airy break. Do not destroy the phase bank on that single
        // outlier; require the new distant pitch to persist for ~12 ms.
        if (missingPitchHops_ > 0 && bank_.size > 0)
            postDropoutGuardHops_ = std::max(2, static_cast<int>(
                std::ceil(0.012 * rate_ / tracker_.hopSamples())));
        missingPitchHops_ = 0;
        const bool jump = bank_.size > 0 &&
            (pitch.hz > bank_.hz * 1.19 || pitch.hz * 1.19 < bank_.hz);
        if (jump && postDropoutGuardHops_ > 0)
        {
            --postDropoutGuardHops_;
            confidence_ = 0.0;
            return;
        }
        if (jump)
            releaseWaveform();
        else
            postDropoutGuardHops_ = 0;
        const bool onset = bank_.size == 0;
        const int priorSize = bank_.size;
        frequency_ = pitch.hz;
        bank_.hz = pitch.hz;
        const bool newShift = onset || targetShift_ != decision.shiftSemitones;
        targetShift_ = decision.shiftSemitones;
        if (retune_ == 0.0f && newShift)
        {
            if (onset)
            {
                shift_ = targetShift_;
                // Anchor both oscillators at the beginning of the fitted
                // input cycle. The destination has advanced through that
                // cycle at the corrected rate when the wet fade begins.
                bank_.destination = Phase::rotate(kTau * (std::exp2(shift_ / 12.0) - 1.0));
            }
            beginFastRetune();
        }
        confidence_ = std::clamp((pitch.confidence - 0.77) / 0.19, 0.0, 1.0);
        const double safeBand = std::min(6000.0, rate_ * 0.44);
        bank_.size = std::clamp(static_cast<int>(safeBand / bank_.hz), 1, kPartials);
        for (int h = bank_.size; h < priorSize; ++h)
            for (auto& channel : bank_.partials)
                channel[static_cast<std::size_t>(h)] = {};
        for (int h = 0; h < bank_.size; ++h)
        {
            const auto bin = static_cast<std::size_t>(h);
            // Fade the upper harmonics out before their frequencies merge
            // with the wideband consonant residual or approach Nyquist.
            const double taper = std::clamp((safeBand - (h + 1) * bank_.hz) / (0.34 * safeBand),
                                           0.0, 1.0);
            bank_.weight[bin] = taper * taper * (3.0 - 2.0 * taper);
            if (h >= priorSize)
                bank_.gain[bin] = 1.0;
        }
        if (!researchPhaseSlew_ || onset)
        {
            phaseHz_ = bank_.hz;
            bank_.sourceStep = Phase::rotate(kTau * bank_.hz / rate_);
        }
        bank_.learningStep = std::min(2.2 * bank_.hz / rate_, 0.68 / bank_.size);
        if (onset)
            initialiseWaveform();
        updateEnvelope(preserveFormants);
    }

    void handlePitchLoss() noexcept
    {
        frequency_ = 0.0f;
        confidence_ = 0.0; // smoothly return the current consonant to dry
        if (bank_.size == 0)
            return;
        if (++missingPitchHops_ < unvoicedHoldHops_)
            return;
        targetShift_ = 0.0;
        voice_.reset();
        releaseWaveform();
    }

    void advanceControl() noexcept
    {
        shift_ += (targetShift_ - shift_) * retuneStep_;
        if (std::abs(shift_) < 1.0e-8)
            shift_ = 0.0;
        updateDestinationStep();
    }

    void beginFastRetune() noexcept
    {
        fastRetuneStart_ = shift_;
        fastRetuneLeft_ = targetShift_ == shift_ ? 0 : fastRetuneSamples_;
    }

    void advanceFastRetune(bool pitchUpdated) noexcept
    {
        if (fastRetuneLeft_ > 0)
        {
            // Finish the change between consecutive pitch estimates, within
            // one millisecond. Keep the oscillator phase continuous.
            const double t = (fastRetuneSamples_ - --fastRetuneLeft_) * fastRetuneProgress_;
            shift_ = fastRetuneLeft_ == 0 ? targetShift_
                : fastRetuneStart_ + t * t * (3.0 - 2.0 * t) * (targetShift_ - fastRetuneStart_);
        }
        else if (!pitchUpdated)
        {
            return;
        }
        updateDestinationStep();
    }

    void updateDestinationStep() noexcept
    {
        if (bank_.size == 0)
            return;
        bank_.ratio = std::exp2(shift_ / 12.0);
        const double renderHz = researchPhaseSlew_ && phaseHz_ > 0.0 ? phaseHz_ : bank_.hz;
        bank_.destinationStep = Phase::rotate(kTau * renderHz * bank_.ratio / rate_);
        // Lock both oscillators together once the correction has fully faded.
        if (wet_ == 0.0 && std::abs(shift_) < 0.01)
            bank_.destination = bank_.source;
    }

    void updateEnvelope(bool preserveFormants) noexcept
    {
        bank_.targetGain.fill(1.0);
        if (!preserveFormants || bank_.size < 3 || std::abs(bank_.ratio - 1.0) < 0.0005)
            return;
        std::array<double, kPartials> power{}, envelope{};
        for (int h = 0; h < bank_.size; ++h)
        {
            const auto bin = static_cast<std::size_t>(h);
            for (int c = 0; c < channels_; ++c)
            {
                const auto& p = bank_.partials[static_cast<std::size_t>(c)][bin];
                power[bin] += p.real * p.real + p.imag * p.imag;
            }
        }
        for (int h = 0; h < bank_.size; ++h)
        {
            const auto bin = static_cast<std::size_t>(h);
            const auto left = static_cast<std::size_t>(std::max(0, h - 1));
            const auto right = static_cast<std::size_t>(std::min(bank_.size - 1, h + 1));
            envelope[bin] = 1.0e-6 + 0.25 * (power[left] + 2.0 * power[bin] + power[right]);
        }
        // Sample the source envelope at the destination frequencies. This
        // compensates broad vowel resonances as the harmonic grid moves.
        for (int h = 0; h < bank_.size; ++h)
        {
            const auto bin = static_cast<std::size_t>(h);
            const double index = std::clamp((h + 1) * bank_.ratio - 1.0, 0.0, bank_.size - 1.0);
            const auto lower = static_cast<std::size_t>(index);
            const auto upper = std::min(lower + 1, static_cast<std::size_t>(bank_.size - 1));
            const double target = envelope[lower] + (index - static_cast<double>(lower)) *
                                                       (envelope[upper] - envelope[lower]);
            bank_.targetGain[bin] = std::clamp(std::sqrt(target / envelope[bin]), 0.72, 1.39);
        }
    }

    void renderAndLearn(const Sample& input, Sample& delta) noexcept
    {
        std::array<Phase, kPartials> basis;
        Sample predicted{}, correction{};
        Phase source = bank_.source, destination = bank_.destination;
        for (int h = 0; h < bank_.size; ++h)
        {
            const auto bin = static_cast<std::size_t>(h);
            basis[bin] = source;
            auto& gain = bank_.gain[bin];
            gain += formantStep_ * (bank_.targetGain[bin] - gain);
            const double weight = bank_.weight[bin];
            for (int c = 0; c < channels_; ++c)
            {
                const auto channel = static_cast<std::size_t>(c);
                const auto& p = bank_.partials[channel][bin];
                const double sourceValue = p.at(source);
                predicted[channel] += sourceValue;
                correction[channel] += weight * (gain * p.at(destination) - sourceValue);
            }
            source = source * bank_.source;
            destination = destination * bank_.destination;
        }
        // Learn all partials from the same residual, so a strong harmonic
        // cannot leak into the independent estimates of its neighbours.
        for (int c = 0; c < channels_; ++c)
        {
            const auto channel = static_cast<std::size_t>(c);
            const double residual = input[channel] - predicted[channel];
            const double error = residual * bank_.learningStep;
            // Back off when the harmonic model stops explaining this input,
            // as happens at a consonant or a new note. Use the previous
            // sample's gain so the current unmodelled transient passes on time.
            delta[channel] += wet_ * bank_.correctionGain[channel] * correction[channel];
            bank_.residualPower[channel] += agreementStep_ *
                (residual * residual - bank_.residualPower[channel]);
            const double mismatch = bank_.residualPower[channel] / std::max(1.0e-9, envelope_[channel]);
            constexpr double tolerance = 0.08;
            constexpr double transitionWidth = 0.45;
            const double fade = std::clamp((mismatch - tolerance) / transitionWidth, 0.0, 1.0);
            const double requestedGain = 1.0 - fade * fade * (3.0 - 2.0 * fade);
            auto& correctionGain = bank_.correctionGain[channel];
            // Keep protective attenuation immediate: slowing the attack
            // created oversized peaks on abrupt real note transitions.
            // Only the recovery is slewed, preventing rapid upward zipper.
            if (requestedGain < correctionGain)
                correctionGain = requestedGain;
            else
                correctionGain += correctionRestoreStep_ * (requestedGain - correctionGain);
            for (int h = 0; h < bank_.size; ++h)
            {
                const auto bin = static_cast<std::size_t>(h);
                auto& p = bank_.partials[channel][bin];
                p.real += error * basis[bin].real;
                p.imag += error * basis[bin].imag;
            }
        }
        bank_.source = bank_.source * bank_.sourceStep;
        bank_.destination = bank_.destination * bank_.destinationStep;
    }

    void releaseWaveform() noexcept
    {
        ++waveformReleaseCount_;
        outgoing_ = bank_;
        bank_ = {};
        decaySpan_ = researchPhaseSlew_ ? researchDecaySamples_ : decaySamples_;
        decayLeft_ = decaySpan_;
        phaseHz_ = 0.0;
        phaseLeft_ = 0;
        releaseAmount_ = wet_;
        wet_ = shift_ = 0.0;
        controlLeft_ = neutralLeft_ = fastRetuneLeft_ = 0;
        missingPitchHops_ = postDropoutGuardHops_ = 0;
    }

    Sample renderOutgoing(double mix) noexcept
    {
        Sample delta{};
        if (decayLeft_ <= 0)
            return delta;
        Phase source = outgoing_.source, destination = outgoing_.destination;
        for (int h = 0; h < outgoing_.size; ++h)
        {
            const auto bin = static_cast<std::size_t>(h);
            for (int c = 0; c < channels_; ++c)
            {
                const auto channel = static_cast<std::size_t>(c);
                const auto& p = outgoing_.partials[channel][bin];
                delta[channel] += outgoing_.weight[bin] * (outgoing_.gain[bin] * p.at(destination) - p.at(source));
            }
            source = source * outgoing_.source;
            destination = destination * outgoing_.destination;
        }
        const double progress = static_cast<double>(decayLeft_) / decaySpan_;
        const double window = progress * progress * (3.0 - 2.0 * progress);
        for (int c = 0; c < channels_; ++c)
        {
            const auto channel = static_cast<std::size_t>(c);
            delta[channel] *= releaseAmount_ * outgoing_.correctionGain[channel];
            if (decayLeft_ == decaySpan_)
                releaseOffset_[channel] = lastCorrection_[channel] - delta[channel];
            delta[channel] = window * (delta[channel] + window * releaseOffset_[channel]);
            if (mix == 0.0 && releaseAmount_ == 0.0)
                delta[channel] = 0.0;
        }
        outgoing_.source = outgoing_.source * outgoing_.sourceStep;
        outgoing_.destination = outgoing_.destination * outgoing_.destinationStep;
        --decayLeft_;
        return delta;
    }

    RealtimePitchTracker tracker_;
    VoiceControl voice_;
    std::array<std::array<float, 2>, kHistory> history_{};
    Bank bank_{}, outgoing_{};
    Sample envelope_{}, lastCorrection_{}, lastDry_{}, releaseOffset_{};
    double rate_ = 48000.0, inputLevelStep_ = 0.0, wetStep_ = 0.0, formantStep_ = 0.0, agreementStep_ = 0.0;
    double correctionRestoreStep_ = 0.0;
    double phaseSlewStep_ = 0.0, phaseHz_ = 0.0;
    double targetShift_ = 0.0, shift_ = 0.0, confidence_ = 0.0, wet_ = 0.0;
    double retuneStep_ = 0.0, releaseAmount_ = 0.0;
    double fastRetuneStart_ = 0.0, fastRetuneProgress_ = 0.0, fastWetStep_ = 0.0;
    float frequency_ = 0.0f, retune_ = -1.0f;
    unsigned position_ = 0, filled_ = 0;
    int channels_ = 1, decaySamples_ = 2, decayLeft_ = 0, controlLeft_ = 0;
    int phaseLeft_ = 0, researchDecaySamples_ = 2, decaySpan_ = 2;
    int neutralHoldSamples_ = 0, neutralLeft_ = 0;
    int fastRetuneSamples_ = 0, fastRetuneLeft_ = 0;
    int unvoicedHoldHops_ = 2, missingPitchHops_ = 0, postDropoutGuardHops_ = 0;
    unsigned waveformReleaseCount_ = 0;
    bool prepared_ = false;
    bool researchPhaseSlew_ = false;
    bool requestedResearchPhaseSlew_ = false;
};

} // namespace APEX::TuneLive
