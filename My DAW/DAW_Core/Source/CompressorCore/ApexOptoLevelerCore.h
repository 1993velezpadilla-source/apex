#pragma once

// APEX Opto Leveler: original, clean-room optical-inspired model.
// Not an extracted or verified Teletronix / UAD algorithm. Calibration and
// blinded measurements are REQUIRED before any reference-accuracy claim.
// C++17 header-only, no allocations/locks in the audio callback.

#include <algorithm>
#include <cmath>

namespace APEX::Compressor
{

struct OptoParameters
{
    double peakReduction = 0.0;  // 0..100, musical control, NOT calibrated dB
    double gainDb = 0.0;         // 0..+40 dB, wet branch only
    double mix = 1.0;            // 0=dry, 1=wet; phase aligned / zero latency
    double emphasis = 0.0;       // 0=flat sidechain; 1=low frequencies rejected
    double colorDrive = 1.0;     // normalized saturation drive; not an LA-2A knob
    bool limit = false;          // false=Compress, true=Limit
    bool colorEnabled = true;    // analog-like output stage, bypassable for A/B
    bool bypass = false;         // returns unaltered dry samples

    static OptoParameters sanitise (OptoParameters p) noexcept
    {
        if (! std::isfinite(p.peakReduction)) p.peakReduction = 0.0;
        if (! std::isfinite(p.gainDb)) p.gainDb = 0.0;
        if (! std::isfinite(p.mix)) p.mix = 1.0;
        if (! std::isfinite(p.emphasis)) p.emphasis = 0.0;
        if (! std::isfinite(p.colorDrive)) p.colorDrive = 1.0;
        p.peakReduction = std::clamp(p.peakReduction, 0.0, 100.0);
        p.gainDb = std::clamp(p.gainDb, 0.0, 40.0);
        p.mix = std::clamp(p.mix, 0.0, 1.0);
        p.emphasis = std::clamp(p.emphasis, 0.0, 1.0);
        p.colorDrive = std::clamp(p.colorDrive, 0.25, 4.0);
        return p;
    }
};

class ApexOptoLevelerCore final
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0)
                    ? sampleRate : 48000.0;
        attackCoeff_ = timeCoefficient(0.010); // nominal 10 ms attack
        fastReleaseCoeff_ = timeCoefficient(0.014); // ~half reduction at 60 ms
        // Sidechain high-pass audition is intentionally sidechain-only.
        scLowpassCoeff_ = timeCoefficient(1.0 / (2.0 * 3.141592653589793 * 320.0));
        memoryAttackCoeff_ = timeCoefficient(1.75);
        memoryReleaseCoeff_ = timeCoefficient(12.0);
        controlCoeff_ = timeCoefficient(0.005);
        reset();
    }

    void reset() noexcept
    {
        fastReduction_ = slowReduction_ = compressionMemory_ = 0.0;
        scLowL_ = scLowR_ = 0.0;
        currentMix_ = parameters_.mix;
        currentGain_ = dbToGain(parameters_.gainDb);
        activeMix_ = parameters_.bypass ? 0.0 : 1.0;
        hasProcessed_ = false;
        gainReductionDb_ = 0.0;
        slowCoeffSamplesUntilRefresh_ = 0;
        cachedSlowCoeff_ = 1.0;
    }

    void setParameters (const OptoParameters& requested) noexcept
    {
        // Call only at a safe control boundary; parent processor owns the
        // atomic parameter snapshot and automation handoff.
        parameters_ = OptoParameters::sanitise(requested);
        // Initial state restore before the first audio block should not
        // manufacture an audible ramp from the factory preset.
        if (!hasProcessed_)
        {
            activeMix_ = parameters_.bypass ? 0.0 : 1.0;
            currentMix_ = parameters_.mix;
            currentGain_ = dbToGain(parameters_.gainDb);
        }
    }

    OptoParameters getParameters() const noexcept { return parameters_; }
    double getGainReductionDb() const noexcept { return gainReductionDb_; }
    double getCompressionMemory() const noexcept { return compressionMemory_; }
    static constexpr int latencySamples() noexcept { return 0; }

    void process (float* left, float* right, int frames) noexcept
    {
        if (left == nullptr || frames <= 0) return;
        hasProcessed_ = true;
        const bool stereo = right != nullptr && right != left;
        constexpr double epsilon = 1.0e-25;
        const double thresholdDb = -(parameters_.peakReduction * 0.40);
        const double thresholdActive = parameters_.peakReduction > 0.0 ? 1.0 : 0.0;
        const double targetMix = parameters_.mix;
        const double targetGain = dbToGain(parameters_.gainDb);
        for (int n = 0; n < frames; ++n)
        {
            const double dryL = sanitiseInput(left[n]);
            const double dryR = stereo ? sanitiseInput(right[n]) : dryL;
            currentMix_ = controlCoeff_ * currentMix_ + (1.0 - controlCoeff_) * targetMix;
            currentGain_ = controlCoeff_ * currentGain_ + (1.0 - controlCoeff_) * targetGain;
            const double targetActive = parameters_.bypass ? 0.0 : 1.0;
            activeMix_ = controlCoeff_ * activeMix_ + (1.0 - controlCoeff_) * targetActive;
            if (std::abs(activeMix_ - targetActive) < 1.0e-10)
                activeMix_ = targetActive;

            // Keep the gain cell warm even under bypass. Applying bypass as a
            // smoothed DRY crossfade at the output prevents discontinuities
            // when automation toggles it under loud program material.
            // Programmable frequency sensitivity, inspired by hardware R37.
            scLowL_ = scLowpassCoeff_ * scLowL_ + (1.0 - scLowpassCoeff_) * dryL;
            scLowR_ = scLowpassCoeff_ * scLowR_ + (1.0 - scLowpassCoeff_) * dryR;
            const double scL = dryL - parameters_.emphasis * scLowL_;
            const double scR = dryR - parameters_.emphasis * scLowR_;
            const double levelDb = 20.0 * std::log10(
                std::max(epsilon, std::max(std::abs(scL), std::abs(scR))));
            const double overDb = levelDb - thresholdDb;
            double targetReduction = 0.0;
            if (thresholdActive != 0.0)
            {
                const double kneeHalfWidthDb = parameters_.limit ? 3.0 : 7.0;
                // Simplified variable ratio: explicitly a model to CALIBRATE.
                const double ratio = parameters_.limit ? 30.0
                    : (2.4 + 1.4 * std::tanh(std::max(0.0, overDb) / 18.0));
                const double slope = 1.0 - 1.0 / ratio;
                if (overDb >= kneeHalfWidthDb)
                    targetReduction = overDb * slope;
                else if (overDb > -kneeHalfWidthDb)
                {
                    const double delta = overDb + kneeHalfWidthDb;
                    targetReduction = slope * delta * delta / (4.0 * kneeHalfWidthDb);
                }
            }
            targetReduction = std::clamp(targetReduction, 0.0, 90.0);
            const double reduction = fastReduction_ + slowReduction_;
            if (targetReduction > reduction)
            {
                const double next = attackCoeff_ * reduction
                                  + (1.0 - attackCoeff_) * targetReduction;
                fastReduction_ = slowReduction_ = next * 0.5;
                slowCoeffSamplesUntilRefresh_ = 0;
            }
            else
            {
                // Multi-rate recovery. An abrupt reduction step recovers roughly
                // half in 60 ms, while a program-dependent memory prolongs the
                // remaining tail. A single one-pole release cannot do this.
                const double memoryTarget = std::clamp(reduction / 15.0, 0.0, 1.0);
                const bool rising = memoryTarget > compressionMemory_;
                const double mc = rising ? memoryAttackCoeff_ : memoryReleaseCoeff_;
                compressionMemory_ = mc * compressionMemory_
                                   + (1.0 - mc) * memoryTarget;
                // Coefficient changes much slower than audio; evaluate every
                // 64 samples on the continuous sample clock, not per block.
                if (slowCoeffSamplesUntilRefresh_ <= 0)
                {
                    cachedSlowCoeff_ = timeCoefficient(1.0 + 14.0 * compressionMemory_);
                    slowCoeffSamplesUntilRefresh_ = 64;
                }
                --slowCoeffSamplesUntilRefresh_;
                const double slowCoeff = cachedSlowCoeff_;
                fastReduction_ = fastReleaseCoeff_ * fastReduction_
                               + (1.0 - fastReleaseCoeff_) * (0.5 * targetReduction);
                slowReduction_ = slowCoeff * slowReduction_
                               + (1.0 - slowCoeff) * (0.5 * targetReduction);
            }
            // Accumulate exposure even while actively compressing.
            if (targetReduction > reduction)
            {
                const double mc = memoryAttackCoeff_;
                compressionMemory_ = mc * compressionMemory_
                    + (1.0 - mc) * std::clamp(targetReduction / 15.0, 0.0, 1.0);
            }
            gainReductionDb_ = std::max(0.0, fastReduction_ + slowReduction_);
            const double appliedGain = dbToGain(-gainReductionDb_);
            double wetL = dryL * appliedGain;
            double wetR = dryR * appliedGain;
            if (parameters_.colorEnabled)
            {
                wetL = color(wetL, parameters_.colorDrive);
                wetR = color(wetR, parameters_.colorDrive);
            }
            wetL *= currentGain_;
            wetR *= currentGain_;
            // Linear mixing maintains time alignment (both are 0 latency).
            // 50% may be louder than pure wet; that is the intent of parallel.
            const double effectiveMix = currentMix_ * activeMix_;
            left[n] = static_cast<float>(dryL * (1.0 - effectiveMix) + wetL * effectiveMix);
            if (stereo)
                right[n] = static_cast<float>(dryR * (1.0 - effectiveMix) + wetR * effectiveMix);
        }
    }

private:
    static double sanitiseInput (float x) noexcept
    {
        return std::isfinite(x) ? std::clamp(static_cast<double>(x), -8.0, 8.0) : 0.0;
    }
    static double dbToGain (double db) noexcept { return std::exp(db * 0.11512925464970229); }
    double timeCoefficient (double seconds) const noexcept
    {
        return std::exp(-1.0 / (sampleRate_ * std::max(seconds, 0.000001)));
    }
    static double color (double input, double drive) noexcept
    {
        // A simple level-dependent asymmetric saturation, not a validated
        // transformer/tube circuit clone. DC-free at zero input.
        const double slope = 1.0 + 0.4 * drive;
        constexpr double bias = 0.09;
        const double slopeAtZero = slope * (1.0 - std::tanh(bias) * std::tanh(bias));
        return (std::tanh(slope * input + bias) - std::tanh(bias)) / slopeAtZero;
    }

    OptoParameters parameters_ {};
    double sampleRate_ = 48000.0;
    double attackCoeff_ = 0.0, fastReleaseCoeff_ = 0.0, scLowpassCoeff_ = 0.0;
    double memoryAttackCoeff_ = 0.0, memoryReleaseCoeff_ = 0.0;
    double controlCoeff_ = 0.0;
    double fastReduction_ = 0.0, slowReduction_ = 0.0, compressionMemory_ = 0.0;
    double scLowL_ = 0.0, scLowR_ = 0.0, currentMix_ = 1.0, currentGain_ = 1.0;
    double gainReductionDb_ = 0.0;
    double activeMix_ = 1.0;
    bool hasProcessed_ = false;
    double cachedSlowCoeff_ = 1.0;
    int slowCoeffSamplesUntilRefresh_ = 0;
};

} // namespace APEX::Compressor
