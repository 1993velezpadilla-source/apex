#pragma once
#include <atomic>

namespace DAW {

/**
 * MasterMeterEngine — metering for the master bus output.
 *
 * Reads the master bus signal AFTER master inserts and master fader,
 * BEFORE the render tap and monitor section.
 *
 * Provides: stereo peak levels, clip detection, peak hold.
 * Thread-safe: audio thread writes, UI thread reads.
 */
class MasterMeterEngine
{
public:
    void pushBlock(const float* L, const float* R, int numSamples) noexcept
    {
        float pL = 0.0f, pR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float al = std::abs(L[i]);
            float ar = std::abs(R[i]);
            if (al > pL) pL = al;
            if (ar > pR) pR = ar;
        }

        peakL_.store(pL, std::memory_order_relaxed);
        peakR_.store(pR, std::memory_order_relaxed);

        if (pL > 1.0f || pR > 1.0f)
            clipped_.store(true, std::memory_order_relaxed);
    }

    float getPeakL()  const noexcept { return peakL_.load(std::memory_order_relaxed); }
    float getPeakR()  const noexcept { return peakR_.load(std::memory_order_relaxed); }
    bool  isClipped() const noexcept { return clipped_.load(std::memory_order_relaxed); }
    void  clearClip()       noexcept { clipped_.store(false, std::memory_order_relaxed); }

private:
    std::atomic<float> peakL_   { 0.0f };
    std::atomic<float> peakR_   { 0.0f };
    std::atomic<bool>  clipped_ { false };
};

} // namespace DAW
