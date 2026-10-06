#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * InputFxChain — per-track input FX for wet monitoring workflows.
 *
 * Sits on the input signal path BEFORE the monitor/record split.
 * Used for:
 *   - Vocal reverb/delay during recording (monitor wet, record dry)
 *   - Input compression/EQ for printed recording (monitor wet, record wet)
 *
 * Version 1: pass-through placeholder.
 * Future: hosts VST/AU plugins on the input signal path.
 */
class InputFxChain
{
public:
    void prepare(double sampleRate, int blockSize) noexcept
    {
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
    }

    void releaseResources() noexcept {}

    void process(float* /*L*/, float* /*R*/, int /*numSamples*/) noexcept
    {
        // v1: pass-through — no input FX processing
    }

    bool isEmpty() const noexcept { return true; }
    void setBypassed(bool b) noexcept { bypassed_ = b; }
    bool isBypassed() const noexcept { return bypassed_; }

private:
    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;
    bool   bypassed_   = true;
};

} // namespace DAW
