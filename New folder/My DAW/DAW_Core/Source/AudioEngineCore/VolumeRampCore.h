#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace DAW {

// ============================================================================
// VolumeRampCore — Per-track per-sample smoother for volume + pan.
//
// Eliminates zipper noise from block-rate volume/pan changes. Generates a
// per-sample ramp from the previous output value to the current target,
// using a one-pole IIR (tau = 5ms by default).
//
// Contract:
//   - Audio thread: call processBlock() with a buffer to apply ramped gain
//   - UI thread: setTargetVolume() and setTargetPan() (atomic, relaxed)
//   - Must be prepared before processBlock() is called
//   - One instance per track
//
// Pattern follows PitchSmootherCore (same per-sample ramp model).
// ============================================================================
class VolumeRampCore
{
public:
    VolumeRampCore() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_     = juce::jmax(1.0, sampleRate);
        maxBlockSize_   = juce::jmax(1, maxBlockSize);
        smoothingCoeff_ = computeSmoothingCoefficient();

        leftRamp_.resize ((size_t) maxBlockSize_, 0.0f);
        rightRamp_.resize((size_t) maxBlockSize_, 0.0f);

        // Initialize smoothed values to the current targets to avoid an
        // initial fade-in on the first block.
        smoothedVolume_ = targetVolume_.load(std::memory_order_relaxed);
        smoothedPan_    = targetPan_   .load(std::memory_order_relaxed);
    }

    void reset()
    {
        smoothedVolume_ = targetVolume_.load(std::memory_order_relaxed);
        smoothedPan_    = targetPan_   .load(std::memory_order_relaxed);
        std::fill(leftRamp_ .begin(), leftRamp_ .end(), 0.0f);
        std::fill(rightRamp_.begin(), rightRamp_.end(), 0.0f);
    }

    // UI thread setters — lock-free via atomic relaxed store
    void setTargetVolume(float newVolume) noexcept
    {
        targetVolume_.store(juce::jmax(0.0f, newVolume),
                            std::memory_order_relaxed);
    }

    void setTargetPan(float newPan) noexcept
    {
        targetPan_.store(juce::jlimit(-1.0f, 1.0f, newPan),
                         std::memory_order_relaxed);
    }

    // Audio thread — generate per-sample ramps for left/right gains
    // After this returns, leftRamp_[s] and rightRamp_[s] hold the gain
    // for sample s. Caller multiplies sample by ramp[s].
    void generateRamps(int numSamples)
    {
        // Grow ramp buffers if a larger block size is requested (e.g. offline export
        // with a different block size than the live device block size).
        if ((int) leftRamp_.size() < numSamples)
        {
            maxBlockSize_ = numSamples;
            leftRamp_ .resize((size_t) numSamples, smoothedVolume_);
            rightRamp_.resize((size_t) numSamples, smoothedVolume_);
        }

        const float vTarget = targetVolume_.load(std::memory_order_relaxed);
        const float pTarget = targetPan_   .load(std::memory_order_relaxed);

        const bool panSettled = std::abs(pTarget - smoothedPan_) < 1.0e-5f;
        const bool volSettled = std::abs(vTarget - smoothedVolume_) < 1.0e-5f;
        if (panSettled && volSettled)
        {
            smoothedPan_ = pTarget;
            smoothedVolume_ = vTarget;
            const float panAngle = (smoothedPan_ + 1.0f) * 0.25f
                                   * juce::MathConstants<float>::pi;
            const float left = smoothedVolume_ * std::cos(panAngle);
            const float right = smoothedVolume_ * std::sin(panAngle);
            std::fill(leftRamp_.begin(), leftRamp_.begin() + numSamples, left);
            std::fill(rightRamp_.begin(), rightRamp_.begin() + numSamples, right);
            return;
        }

        for (int s = 0; s < numSamples; ++s)
        {
            // One-pole IIR smoother — same pattern as PitchSmootherCore
            smoothedVolume_ += (vTarget - smoothedVolume_) * (float) smoothingCoeff_;
            smoothedPan_    += (pTarget - smoothedPan_)    * (float) smoothingCoeff_;

            // Equal-power pan law per-sample
            const float panAngle = (smoothedPan_ + 1.0f) * 0.25f
                                   * juce::MathConstants<float>::pi;
            leftRamp_ [s] = smoothedVolume_ * std::cos(panAngle);
            rightRamp_[s] = smoothedVolume_ * std::sin(panAngle);
        }
    }

    // Audio thread — apply pre-generated ramps to a stereo buffer
    // Generates ramps then multiplies in-place
    void applyToStereoBuffer(float* leftChannel,
                             float* rightChannel,
                             int    numSamples)
    {
        generateRamps(numSamples);

        for (int s = 0; s < numSamples; ++s)
        {
            leftChannel [s] *= leftRamp_ [s];
            rightChannel[s] *= rightRamp_[s];
        }
    }

    // Read-only accessors for metering after applying gain
    // Caller can use peak of leftRamp_/rightRamp_ for metering
    const float* getLeftRamp()  const noexcept { return leftRamp_ .data(); }
    const float* getRightRamp() const noexcept { return rightRamp_.data(); }

    // Returns the LAST sample value of the ramp — useful for metering display
    float getCurrentLeftGain() const noexcept
    {
        return leftRamp_.empty() ? 0.0f : leftRamp_.back();
    }

    float getCurrentRightGain() const noexcept
    {
        return rightRamp_.empty() ? 0.0f : rightRamp_.back();
    }

private:
    double computeSmoothingCoefficient() const noexcept
    {
        // tau = 5ms — fast enough that user fader moves feel responsive,
        // slow enough to eliminate zipper. Same as audio engineering
        // standard for fader smoothing in pro DAWs.
        constexpr double tau = 0.005;
        return 1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * tau));
    }

    double sampleRate_     = 44100.0;
    int    maxBlockSize_   = 512;
    double smoothingCoeff_ = 1.0;

    // UI-thread-writable, audio-thread-readable
    std::atomic<float> targetVolume_ { 1.0f };
    std::atomic<float> targetPan_    { 0.0f };

    // Audio-thread-only state
    float smoothedVolume_ = 1.0f;
    float smoothedPan_    = 0.0f;

    // Per-sample ramp buffers
    std::vector<float> leftRamp_;
    std::vector<float> rightRamp_;
};

} // namespace DAW
