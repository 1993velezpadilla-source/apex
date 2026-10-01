#pragma once
#include <atomic>
#include <cmath>

namespace DAW {

/**
 * MeteringCore — generic stereo peak meter.
 *
 * Reusable metering engine for any point in the signal chain.
 * Thread-safe: audio thread writes, UI thread reads.
 *
 * Supports:
 *   - Track meter
 *   - Bus meter
 *   - Master meter (post-insert, selectable pre/post-fader in future)
 *   - Monitor meter (what feeds the listening output)
 */
class MeteringCore
{
public:
    /** Push a block of audio for peak measurement. */
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

    float getPeakL() const noexcept { return peakL_.load(std::memory_order_relaxed); }
    float getPeakR() const noexcept { return peakR_.load(std::memory_order_relaxed); }
    bool  isClipped() const noexcept { return clipped_.load(std::memory_order_relaxed); }
    void  clearClip() noexcept { clipped_.store(false, std::memory_order_relaxed); }

    float getPeakDb() const noexcept
    {
        float p = std::max(getPeakL(), getPeakR());
        return p <= 0.000001f ? -120.0f : 20.0f * std::log10(p);
    }

private:
    std::atomic<float> peakL_{ 0.0f };
    std::atomic<float> peakR_{ 0.0f };
    std::atomic<bool>  clipped_{ false };
};

} // namespace DAW
