#pragma once

#include <JuceHeader.h>
#include "TapeStopTypes.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace APEX {
namespace TapeStop {

/**
 * Clip-local tape-head resampler.
 *
 * All storage is prepared before realtime rendering.  The processor writes
 * the current isolated clip source into a bounded ring and reads it back at
 * the rate supplied by the already-evaluated clip automation trajectory.
 * A stop value of 0 is an exact direct/bypass path; non-zero values change
 * the read-head rate and therefore change pitch and time together.
 */
class ProcessorCore
{
public:
    void prepare(double sampleRate, int blockSize, int numChannels = 2)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        blockSize_ = juce::jmax(1, blockSize);
        channelCount_ = juce::jmax(1, numChannels);

        const int requiredCapacity = juce::nextPowerOfTwo(
            juce::jmax(blockSize_ * 8, (int) std::ceil(sampleRate_ * 4.0)));

        if ((int) bufferL_.size() != requiredCapacity)
        {
            bufferL_.assign((size_t) requiredCapacity, 0.0f);
            bufferR_.assign((size_t) requiredCapacity, 0.0f);
        }
        else if (bufferR_.size() != bufferL_.size())
        {
            bufferR_.assign(bufferL_.size(), 0.0f);
        }

        bufferMask_ = requiredCapacity - 1;
        prepared_ = true;
        reset();
    }

    /** Reset only the read/write state.  Old ring contents are unreachable
     * after the absolute indices are reset, so clearing several seconds of
     * audio is unnecessary on a seek, transport restart, or clip bypass. */
    void reset() noexcept
    {
        writeIndex_ = 0;
        readIndex_ = 0.0;
        currentRate_ = 1.0f;
        transitionDirection_ = TransitionDirection::Ending;
        lastOutputL_ = 0.0f;
        lastOutputR_ = 0.0f;
        lastOutputValid_ = false;
    }

    /**
     * Start a new clip-local transition.  Both directions start their read
     * head at the next source sample, which prevents a previous clip's ring
     * contents from leaking into this clip.  Ending transitions then brake
     * from normal playback; beginning transitions can start at rate zero and
     * accelerate as their automation values fall.
     */
    void beginTransition(TransitionDirection direction,
                         float initialTapeStopValue = 0.0f) noexcept
    {
        if (! prepared_)
            return;

        transitionDirection_ = direction;
        currentRate_ = 1.0f - juce::jlimit(0.0f, 1.0f, initialTapeStopValue);
        readIndex_ = (double) writeIndex_;
        lastOutputValid_ = false;
    }

    void process(float* left, float* right, int numSamples, float tapeStopValue) noexcept
    {
        processWithValues(left, right, numSamples, tapeStopValue, nullptr);
    }

    /**
     * Process a per-sample stopped-amount trajectory.  The trajectory must
     * already be evaluated from the immutable clip automation snapshot by the
     * caller; this method performs no allocations, lookups, or curve work.
     */
    void processWithValues(float* left,
                           float* right,
                           int numSamples,
                           float constantTapeStopValue,
                           const float* tapeStopValues) noexcept
    {
        if (! prepared_ || left == nullptr || right == nullptr || numSamples <= 0 || bufferL_.empty())
            return;

        const int64_t capacity = (int64_t) bufferL_.size();

        for (int i = 0; i < numSamples; ++i)
        {
            const float inputL = left[i];
            const float inputR = right[i];
            const float stopValue = juce::jlimit(0.0f, 1.0f,
                tapeStopValues != nullptr ? tapeStopValues[i] : constantTapeStopValue);
            const float targetRate = 1.0f - stopValue;
            const int64_t sourceIndex = writeIndex_;

            // The source is written before the read so a transition may begin
            // at the first sample of a clip without reading stale ring data.
            bufferL_[(size_t) (sourceIndex & bufferMask_)] = inputL;
            bufferR_[(size_t) (sourceIndex & bufferMask_)] = inputR;

            if (stopValue <= 0.000001f)
            {
                // Exact normal playback.  This is the only bypass owner: the
                // caller has already isolated this clip and will mix it once.
                left[i] = inputL;
                right[i] = inputR;
                currentRate_ = 1.0f;
                readIndex_ = (double) sourceIndex + 1.0;
                lastOutputL_ = inputL;
                lastOutputR_ = inputR;
                lastOutputValid_ = true;
                ++writeIndex_;
                continue;
            }

            // Automation already supplies the intended curve and exact sample
            // duration.  Do not add a second time-constant smoother here: it
            // would make short/long transitions miss their endpoint.
            currentRate_ = targetRate;

            const int64_t oldestIndex = juce::jmax<int64_t>(
                0, sourceIndex - capacity + 1);
            if (readIndex_ < (double) oldestIndex)
                readIndex_ = (double) oldestIndex;
            if (readIndex_ > (double) sourceIndex)
                readIndex_ = (double) sourceIndex;

            const double readFloor = std::floor(readIndex_);
            const int64_t firstIndex = juce::jlimit<int64_t>(
                oldestIndex, sourceIndex, (int64_t) readFloor);
            const int64_t secondIndex = juce::jmin<int64_t>(sourceIndex, firstIndex + 1);
            const float fraction = juce::jlimit(0.0f,
                1.0f, (float) (readIndex_ - readFloor));

            const float firstL = bufferL_[(size_t) (firstIndex & bufferMask_)];
            const float firstR = bufferR_[(size_t) (firstIndex & bufferMask_)];
            const float secondL = bufferL_[(size_t) (secondIndex & bufferMask_)];
            const float secondR = bufferR_[(size_t) (secondIndex & bufferMask_)];
            left[i] = firstL + (secondL - firstL) * fraction;
            right[i] = firstR + (secondR - firstR) * fraction;

            // A fully stopped head holds the last generated sample.  This
            // prevents a long hold from becoming a moving ring-buffer read
            // once the source write head has travelled past the ring capacity.
            if (targetRate <= 0.000001f && lastOutputValid_)
            {
                left[i] = lastOutputL_;
                right[i] = lastOutputR_;
            }

            lastOutputL_ = left[i];
            lastOutputR_ = right[i];
            lastOutputValid_ = true;
            readIndex_ += (double) targetRate;
            ++writeIndex_;
        }
    }

    float getCurrentRateForTesting() const noexcept { return currentRate_; }
    int getPreparedChannelCountForTesting() const noexcept { return channelCount_; }
    int getBufferCapacityForTesting() const noexcept { return (int) bufferL_.size(); }

private:
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    int channelCount_ = 2;
    int bufferMask_ = 0;
    int64_t writeIndex_ = 0;
    double readIndex_ = 0.0;
    float currentRate_ = 1.0f;
    bool prepared_ = false;
    TransitionDirection transitionDirection_ = TransitionDirection::Ending;
    float lastOutputL_ = 0.0f;
    float lastOutputR_ = 0.0f;
    bool lastOutputValid_ = false;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
};

} // namespace TapeStop
} // namespace APEX
