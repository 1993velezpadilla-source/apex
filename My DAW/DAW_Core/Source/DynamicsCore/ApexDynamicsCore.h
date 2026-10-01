#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Reusable, product-independent APEX Dynamics Core.
//
// This module is the shared foundation for BOTH the APEX Parametric EQ
// Dynamic EQ and the future native APEX Compressor. It contains no plugin
// identity, no parameter ABI, no GUI, and no APEX-specific tuning. Everything
// here is fixed-storage, allocation-free, and realtime-safe.
//
// Architecture:
//   detector        -> level estimation (peak / RMS, optional stereo link)
//   gain computer   -> static transfer curve (threshold, ratio, knee, range)
//   envelope        -> attack/release ballistics with exact convergence
//   processor       -> per-sample gain computation driving any gain cell
//
// All units are linear amplitudes or dB where named; time constants are
// seconds at the prepared sample rate.
namespace APEX::Dynamics
{

enum class DetectorMode : std::uint8_t
{
    Peak = 0,
    Rms
};

struct DynamicsParameters
{
    double thresholdDb = 0.0;      // detector level where gain reduction begins
    double ratio = 1.0;            // >= 1.0 (downward compression law)
    double kneeDb = 0.0;           // soft-knee half-width, 0 = hard knee
    double rangeDb = 0.0;          // 0 = unlimited downward range
    double attackSeconds = 0.010;
    double releaseSeconds = 0.100;
    double makeupGainDb = 0.0;
    DetectorMode detectorMode = DetectorMode::Rms;
    bool stereoLinked = true;      // link uses the louder channel's detector

    static DynamicsParameters sanitised (DynamicsParameters value) noexcept
    {
        if (! std::isfinite (value.thresholdDb)) value.thresholdDb = 0.0;
        if (! std::isfinite (value.ratio) || value.ratio < 1.0) value.ratio = 1.0;
        if (! std::isfinite (value.kneeDb)) value.kneeDb = 0.0;
        if (value.kneeDb < 0.0) value.kneeDb = 0.0;
        if (! std::isfinite (value.rangeDb)) value.rangeDb = 0.0;
        if (value.rangeDb < 0.0) value.rangeDb = 0.0;
        if (! std::isfinite (value.attackSeconds)) value.attackSeconds = 0.010;
        if (! std::isfinite (value.releaseSeconds)) value.releaseSeconds = 0.100;
        value.attackSeconds = std::clamp (value.attackSeconds, 0.0005, 10.0);
        value.releaseSeconds = std::clamp (value.releaseSeconds, 0.001, 20.0);
        if (! std::isfinite (value.makeupGainDb)) value.makeupGainDb = 0.0;
        return value;
    }
};

// Level detector. RMS uses a one-pole energy averager so it remains O(1) per
// sample and allocation-free (the exact ITU BS.1770 gate is a metering
// concern, not this detector's contract).
class Detector final
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) && sampleRate > 1.0
                    ? sampleRate : 48000.0;
        constexpr double rmsWindowSeconds = 0.010;
        rmsCoefficient_ = std::exp (-1.0 / std::max (1.0,
            sampleRate_ * rmsWindowSeconds));
        reset();
    }

    void reset() noexcept
    {
        energy_ = 0.0;
    }

    // Mono level in dB (0 dBFS reference). Returns -inf for silence floor.
    double processMono (double sample, DetectorMode mode) noexcept
    {
        const auto safe = std::isfinite (sample) ? sample : 0.0;
        if (mode == DetectorMode::Peak)
            return linearToDb (std::abs (safe));

        energy_ = rmsCoefficient_ * energy_
                + (1.0 - rmsCoefficient_) * safe * safe;
        return linearToDb (std::sqrt (std::max (energy_, 0.0)));
    }

    // Stereo level in dB. Linked mode reports the louder channel; unlinked
    // callers invoke per channel via processMono.
    double processStereo (double left, double right,
                          DetectorMode mode, bool linked) noexcept
    {
        const auto leftLevel = processMono (left, mode);
        const auto rightLevel = processMono (right, mode);
        if (linked)
            return std::max (leftLevel, rightLevel);
        return 0.5 * (leftLevel + rightLevel); // averaged unlinked reference
    }

    // Single-pass stereo frame measurement: the energy/peak state advances
    // exactly once per sample. Linked peak uses the louder channel; linked
    // RMS uses the louder channel's power; unlinked uses the mean power /
    // mean magnitude. This is the preferred per-sample stereo entry point.
    double processStereoFrame (double left, double right,
                               DetectorMode mode, bool linked) noexcept
    {
        const auto safeLeft = std::isfinite (left) ? left : 0.0;
        const auto safeRight = std::isfinite (right) ? right : 0.0;
        if (mode == DetectorMode::Peak)
        {
            const auto leftPeak = std::abs (safeLeft);
            const auto rightPeak = std::abs (safeRight);
            const auto level = linked ? std::max (leftPeak, rightPeak)
                                      : 0.5 * (leftPeak + rightPeak);
            return linearToDb (level);
        }

        const auto leftPower = safeLeft * safeLeft;
        const auto rightPower = safeRight * safeRight;
        const auto power = linked ? std::max (leftPower, rightPower)
                                  : 0.5 * (leftPower + rightPower);
        energy_ = rmsCoefficient_ * energy_
                + (1.0 - rmsCoefficient_) * power;
        return linearToDb (std::sqrt (std::max (energy_, 0.0)));
    }

    static double linearToDb (double linear) noexcept
    {
        return 20.0 * std::log10 (std::max (1.0e-12, linear));
    }

private:
    double sampleRate_ = 48000.0;
    double rmsCoefficient_ = 1.0;
    double energy_ = 0.0;
};

// Static transfer curve: detector level in dB -> gain reduction in dB.
// Implements the standard downward compression law with an optional
// quadratic soft knee (continuous value and derivative) and an optional
// maximum reduction range.
class GainComputer final
{
public:
    static double gainReductionDb (double levelDb,
                                   const DynamicsParameters& parameters) noexcept
    {
        const auto p = DynamicsParameters::sanitised (parameters);
        const auto over = levelDb - p.thresholdDb;

        if (p.kneeDb <= 0.0)
        {
            if (over <= 0.0)
                return 0.0;
            const auto reduction = over * (1.0 - 1.0 / p.ratio);
            return clampRange (reduction, p.rangeDb);
        }

        const auto knee = p.kneeDb;
        if (over <= -knee)
            return 0.0;
        if (over >= knee)
        {
            const auto reduction = over * (1.0 - 1.0 / p.ratio);
            return clampRange (reduction, p.rangeDb);
        }
        // Quadratic blend over [-knee, +knee]; continuous at both ends.
        const auto full = knee * (1.0 - 1.0 / p.ratio);
        const auto t = (over + knee) / (2.0 * knee);
        return clampRange (full * t * t, p.rangeDb);
    }

private:
    static double clampRange (double reduction, double rangeDb) noexcept
    {
        if (rangeDb > 0.0)
            reduction = std::min (reduction, rangeDb);
        return std::max (0.0, reduction);
    }
};

// Attack/release ballistics. One-pole attack and release branches with an
// exact snap-to-target guarantee: once no representable progress remains,
// the state becomes exactly the target (no asymptotic stall).
class Envelope final
{
public:
    void prepare (double sampleRate, double attackSeconds,
                  double releaseSeconds) noexcept
    {
        const auto rate = std::isfinite (sampleRate) && sampleRate > 1.0
                        ? sampleRate : 48000.0;
        attack_ = std::exp (-1.0 / std::max (1.0, rate * attackSeconds));
        release_ = std::exp (-1.0 / std::max (1.0, rate * releaseSeconds));
        reset();
    }

    void reset() noexcept { state_ = 0.0; }

    // Product-independent coefficient accessors. They let a product-level
    // adapter reuse the exact attack/release timing law in a signed domain
    // while the envelope itself retains its non-negative magnitude contract.
    double attackCoefficient() const noexcept { return attack_; }
    double releaseCoefficient() const noexcept { return release_; }

    // Advance the smoothed gain-reduction dB toward targetDb for one sample.
    double process (double targetDb) noexcept
    {
        const auto target = std::isfinite (targetDb) ? targetDb : 0.0;
        const bool attacking = target > state_;
        const auto coefficient = attacking ? attack_ : release_;
        const auto next = coefficient * state_ + (1.0 - coefficient) * target;
        if (next == state_) // float stagnation guard
            state_ = target;
        else
            state_ = next;
        return state_;
    }

private:
    double attack_ = 1.0;
    double release_ = 1.0;
    double state_ = 0.0;
};

// Convenience composition: detector -> gain computer -> envelope -> linear
// gain, in one fixed-storage unit. The returned linear gain can drive any
// gain cell (EQ band gain, broadband trim, makeup).
class DynamicsProcessor final
{
public:
    void prepare (double sampleRate,
                  const DynamicsParameters& parameters) noexcept
    {
        parameters_ = DynamicsParameters::sanitised (parameters);
        envelopeRate_ = std::isfinite (sampleRate) && sampleRate > 1.0
                      ? sampleRate : 48000.0;
        detector_.prepare (envelopeRate_);
        envelope_.prepare (envelopeRate_, parameters_.attackSeconds,
                           parameters_.releaseSeconds);
        makeupGain_ = std::pow (10.0, parameters_.makeupGainDb / 20.0);
    }

    void reset() noexcept { detector_.reset(); envelope_.reset(); }

    void setParameters (const DynamicsParameters& parameters) noexcept
    {
        parameters_ = DynamicsParameters::sanitised (parameters);
        // Coefficient changes adopt immediately without resetting ballistics
        // (smoothing absorbs them); attack/release retarget at control rate.
        envelope_.prepare (envelopeRate_, parameters_.attackSeconds,
                           parameters_.releaseSeconds);
        makeupGain_ = std::pow (10.0, parameters_.makeupGainDb / 20.0);
    }

    // One sample of one channel (unlinked mode).
    double processMono (double sample) noexcept
    {
        const auto levelDb = detector_.processMono (sample,
                                                    parameters_.detectorMode);
        const auto target = GainComputer::gainReductionDb (levelDb,
                                                          parameters_);
        const auto smoothed = envelope_.process (target);
        return std::pow (10.0, -smoothed / 20.0) * makeupGain_;
    }

    // One stereo frame: returns the shared gain for both channels when
    // linked, or processes the left channel when unlinked (callers apply the
    // result per channel; linked mode is the product default).
    double processLinkedGain (double left, double right) noexcept
    {
        const auto levelDb = detector_.processStereo (left, right,
                                                      parameters_.detectorMode,
                                                      parameters_.stereoLinked);
        const auto target = GainComputer::gainReductionDb (levelDb,
                                                          parameters_);
        const auto smoothed = envelope_.process (target);
        return std::pow (10.0, -smoothed / 20.0) * makeupGain_;
    }

    // Sidechain-ready overloads: the detector consumes a separate key
    // signal while the returned gain is applied by the caller to the main
    // program material. Host/plugin routing supplies the key; this core
    // never owns sidechain plumbing.
    double processMonoFromKey (double /*mainSample*/,
                               double detectorSample) noexcept
    {
        const auto levelDb = detector_.processMono (detectorSample,
                                                    parameters_.detectorMode);
        const auto target = GainComputer::gainReductionDb (levelDb,
                                                          parameters_);
        const auto smoothed = envelope_.process (target);
        return std::pow (10.0, -smoothed / 20.0) * makeupGain_;
    }

    double processLinkedGainFromKey (double /*left*/, double /*right*/,
                                     double detectorLeft,
                                     double detectorRight) noexcept
    {
        const auto levelDb = detector_.processStereo (detectorLeft,
                                                      detectorRight,
                                                      parameters_.detectorMode,
                                                      parameters_.stereoLinked);
        const auto target = GainComputer::gainReductionDb (levelDb,
                                                          parameters_);
        const auto smoothed = envelope_.process (target);
        return std::pow (10.0, -smoothed / 20.0) * makeupGain_;
    }

private:
    DynamicsParameters parameters_ {};
    Detector detector_;
    Envelope envelope_;
    double envelopeRate_ = 48000.0;
    double makeupGain_ = 1.0;
};

} // namespace APEX::Dynamics
