// ===========================================================================
// TrackPeakMeterStateCore.h
// Lock-free atomic meter state written by the audio thread,
// read by the UI thread. One instance per track.
// ===========================================================================
#pragma once
#include <atomic>

namespace DAW {

struct TrackPeakMeterStateCore
{
    // Written by audio thread, read by UI thread — all atomics.
    std::atomic<float> samplePeakDb { -120.0f };
    std::atomic<float> truePeakDb   { -120.0f };
    std::atomic<float> peakHoldDb   { -120.0f };
    std::atomic<bool>  clipped      { false   };

    // UI thread only — reset peak hold.
    void resetHold() noexcept
    {
        peakHoldDb.store(-120.0f, std::memory_order_relaxed);
        clipped.store(false, std::memory_order_relaxed);
    }

    // Snapshot for UI: read all values in one call (no lock needed,
    // small tearing risk is acceptable for metering display).
    struct Snapshot
    {
        float samplePeakDb = -120.0f;
        float truePeakDb   = -120.0f;
        float peakHoldDb   = -120.0f;
        bool  clipped      = false;
    };

    Snapshot read() const noexcept
    {
        return {
            samplePeakDb.load(std::memory_order_relaxed),
            truePeakDb  .load(std::memory_order_relaxed),
            peakHoldDb  .load(std::memory_order_relaxed),
            clipped     .load(std::memory_order_relaxed)
        };
    }
};

} // namespace DAW
