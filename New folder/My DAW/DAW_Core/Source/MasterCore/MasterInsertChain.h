#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * MasterInsertChain — insert processing on the master bus.
 *
 * Part of the RENDER/EXPORT path (unlike MonitorFxChain).
 * Processing here IS printed into bounced/exported files.
 *
 * Version 1: pass-through placeholder.
 * Future: hosts limiter, compressor, EQ, etc. on the master bus.
 */
class MasterInsertChain
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
        // v1: pass-through — no master inserts
    }

    int  getSlotCount() const noexcept { return kMaxSlots; }
    bool isEmpty() const noexcept { return true; }

    static constexpr int kMaxSlots = 8;

private:
    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;
};

} // namespace DAW
