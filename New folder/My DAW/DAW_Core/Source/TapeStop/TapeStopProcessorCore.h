#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>

namespace APEX {
namespace TapeStop {

class ProcessorCore
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        blockSize_ = juce::jmax(1, blockSize);

        const int capacity = juce::nextPowerOfTwo(juce::jmax(blockSize_ * 8, (int)std::ceil(sampleRate_ * 4.0)));
        bufferMask_ = capacity - 1;
        bufferL_.assign((size_t)capacity, 0.0f);
        bufferR_.assign((size_t)capacity, 0.0f);
        writePos_ = 0;
        readPos_ = 0.0;
        smoothedRate_ = 1.0f;
        prepared_ = true;
    }

    void reset()
    {
        std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
        writePos_ = 0;
        readPos_ = 0.0;
        smoothedRate_ = 1.0f;
    }

    void process(float* left, float* right, int numSamples, float tapeStopValue) noexcept
    {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples <= 0 || bufferL_.empty())
            return;

        const float targetRate = 1.0f - juce::jlimit(0.0f, 1.0f, tapeStopValue);
        const bool bypass = targetRate > 0.9995f;
        const float smoothingCoeff = 1.0f - std::exp(-1.0f / (float)juce::jmax(1.0, sampleRate_ * 0.012));

        for (int i = 0; i < numSamples; ++i)
        {
            bufferL_[(size_t)writePos_] = left[i];
            bufferR_[(size_t)writePos_] = right[i];

            if (bypass)
            {
                readPos_ = (double)writePos_;
                smoothedRate_ = 1.0f;
                writePos_ = (writePos_ + 1) & bufferMask_;
                continue;
            }

            smoothedRate_ += (targetRate - smoothedRate_) * smoothingCoeff;

            if (smoothedRate_ > 0.995f)
            {
                left[i] = bufferL_[(size_t)writePos_];
                right[i] = bufferR_[(size_t)writePos_];
                readPos_ = (double)writePos_;
            }
            else
            {
                const int i0 = ((int)std::floor(readPos_)) & bufferMask_;
                const int i1 = (i0 + 1) & bufferMask_;
                const float frac = (float)(readPos_ - std::floor(readPos_));

                left[i] = bufferL_[(size_t)i0] + (bufferL_[(size_t)i1] - bufferL_[(size_t)i0]) * frac;
                right[i] = bufferR_[(size_t)i0] + (bufferR_[(size_t)i1] - bufferR_[(size_t)i0]) * frac;

                readPos_ += (double)smoothedRate_;
                while (readPos_ >= (double)bufferL_.size())
                    readPos_ -= (double)bufferL_.size();
            }

            writePos_ = (writePos_ + 1) & bufferMask_;
        }
    }

private:
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    int bufferMask_ = 0;
    int writePos_ = 0;
    double readPos_ = 0.0;
    float smoothedRate_ = 1.0f;
    bool prepared_ = false;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
};

} // namespace TapeStop
} // namespace APEX
