#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>

namespace DAW {

/**
 * DynamicsDetectorCore — isolated nucleo.
 *
 * Implements the Giannoulis / Massberg / Reiss (JAES 2012) feedforward compressor
 * algorithm used in university DSP courses (Stanford CCRMA, Queen Mary, McGill).
 *
 * Block diagram:
 *   detector_in → level detect (dB) → gain computer (soft knee) → ballistics → gain coeff
 *   audio_in    ──────────────────────────────────────────────────────────────→ × gain → out
 *
 * For internal compression:   detector_in == audio_in
 * For external sidechain:     detector_in comes from the sidechain bus
 *
 * Parameters are set via the public fields. Coefficients are recomputed only
 * when prepare() or setAttack/setRelease is called — safe to update from the
 * message thread if the audio thread uses the stored coefficients atomically.
 *
 * This core is a single nucleo — no JUCE processor overhead, no bus management.
 * Wrap it in a plugin processor or call it directly from an engine loop.
 */
class DynamicsDetectorCore
{
public:
    // ── Parameters ──────────────────────────────────────────────────────────
    float thresholdDb  = -20.f;   // T
    float ratio        =   4.f;   // R  (1 = no compression, inf = limiter)
    float kneeWidthDb  =   6.f;   // W  (0 = hard knee)
    float attackMs     =  10.f;   // attack time (ms)
    float releaseMs    = 100.f;   // release time (ms)
    float makeupDb     =   0.f;   // make-up gain (dB)

    // ── Lifecycle ────────────────────────────────────────────────────────────

    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        recomputeCoeffs();
        reset();
    }

    void reset() noexcept
    {
        env_     = 0.f;
        envPrev_ = 0.f;
    }

    void setAttack(float ms)  noexcept { attackMs  = ms;  recomputeCoeffs(); }
    void setRelease(float ms) noexcept { releaseMs = ms;  recomputeCoeffs(); }

    // ── Per-sample process ───────────────────────────────────────────────────

    /**
     * Process one sample.
     *
     * @param audioIn     The sample to apply gain reduction to (audio path).
     * @param detectorIn  The sample to analyse for level detection.
     *                    Pass audioIn for internal compression.
     *                    Pass the sidechain bus sample for external sidechain.
     * @return            Gain-reduced output sample with make-up gain applied.
     */
    float processSample(float audioIn, float detectorIn) noexcept
    {
        // 1. Level detection in dB (peak, with epsilon guard)
        const float xDb = 20.f * std::log10(std::max(std::abs(detectorIn), 1e-6f));

        // 2. Gain computer — static curve with soft knee (Reiss 2012 eq.)
        float yDb;
        const float T = thresholdDb;
        const float R = ratio;
        const float W = kneeWidthDb;

        if (2.f * (xDb - T) < -W)
        {
            yDb = xDb;   // below knee — no compression
        }
        else if (2.f * std::abs(xDb - T) <= W)
        {
            // Inside soft knee — quadratic interpolation
            const float over = xDb - T + W * 0.5f;
            yDb = xDb + (1.f / R - 1.f) * over * over / (2.f * W);
        }
        else
        {
            yDb = T + (xDb - T) / R;   // above knee — full ratio
        }

        const float reductionDb = xDb - yDb;   // always >= 0

        // 3. Smoothed branching peak detector (ballistics in log domain — Reiss recommendation)
        if (reductionDb > envPrev_)
            env_ = alphaA_ * envPrev_ + (1.f - alphaA_) * reductionDb;
        else
            env_ = alphaR_ * envPrev_ + (1.f - alphaR_) * reductionDb;

        envPrev_ = env_;

        // 4. Convert to linear gain and apply with make-up
        const float gain = std::pow(10.f, -env_ / 20.f) * makeupGainLinear_;
        return gain * audioIn;
    }

    /**
     * Process a buffer in-place.
     * detectorBuffer may be the same pointer as audioBuffer (internal compression)
     * or a separate sidechain bus buffer.
     */
    void processBlock(float* audioBuffer,
                      const float* detectorBuffer,
                      int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            audioBuffer[i] = processSample(audioBuffer[i], detectorBuffer[i]);
    }

    /** Returns current gain reduction in dB (positive = reduction). */
    float getGainReductionDb() const noexcept { return env_; }

    /** Returns the current linear gain coefficient [0..1]. */
    float getLinearGain() const noexcept { return std::pow(10.f, -env_ / 20.f); }

private:
    void recomputeCoeffs() noexcept
    {
        if (sampleRate_ <= 0.0) return;
        alphaA_          = std::exp(-1.f / (float)(attackMs  * 0.001 * sampleRate_));
        alphaR_          = std::exp(-1.f / (float)(releaseMs * 0.001 * sampleRate_));
        makeupGainLinear_ = std::pow(10.f, makeupDb / 20.f);
    }

    double sampleRate_ = 44100.0;
    float  alphaA_     = 0.f;
    float  alphaR_     = 0.f;
    float  makeupGainLinear_ = 1.f;
    float  env_        = 0.f;
    float  envPrev_    = 0.f;
};

} // namespace DAW
