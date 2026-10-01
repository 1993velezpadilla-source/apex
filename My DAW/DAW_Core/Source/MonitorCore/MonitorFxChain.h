#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * MonitorFxChain — monitor-only FX placeholder.
 *
 * Sits exclusively on the monitoring path and is NEVER on the render/export
 * path. Version 1: pass-through with bypass support.
 *
 * Future: insert speaker correction, headphone EQ, crossfeed,
 * room simulation, reference matching tools.
 *
 * Architecture allows future per-speaker-set and per-headphone FX chains.
 */
class MonitorFxChain
{
public:
    void prepare(double sampleRate, int blockSize) noexcept
    {
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
    }

    void releaseResources() noexcept {}

    void process(float* /*outL*/, float* /*outR*/, int /*numSamples*/,
                 bool bypassed) noexcept
    {
        if (bypassed) return;
        // v1: no-op — placeholder for future speaker/headphone correction
    }

    bool isEmpty() const noexcept { return true; }

private:
    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;
};

} // namespace DAW
