#pragma once
#include <JuceHeader.h>

namespace APEX {
namespace G10 {

// Diagnostic-only correlation object for focused analyzer latency tests.
// Production instances never attach one. The object is fixed-size POD-like
// state: no locks, allocation, I/O, strings, or formatting on any measured
// path. T0 is published from the analyzer FIFO producer; T1-T4 are recorded by
// the GUI consumer/FFT/cache/paint stages.
class G10AnalyzerTimingProbe final
{
public:
    struct Snapshot
    {
        juce::int64 t0 = 0;
        juce::int64 t1 = 0;
        juce::int64 t2 = 0;
        juce::int64 t3 = 0;
        juce::int64 visibleTarget = 0;
        juce::int64 presentationEntry = 0;
        juce::int64 visibleLevel = 0;
        juce::int64 repaintRequest = 0;
        juce::int64 paintEntry = 0;
        juce::int64 t4 = 0;

        bool isComplete() const noexcept
        {
            return t0 > 0 && t1 >= t0 && t2 >= t1 && t3 >= t2 && t4 >= t3;
        }

        double millisecondsFromT0 (juce::int64 value) const noexcept
        {
            if (t0 <= 0 || value < t0)
                return -1.0;
            const double frequency = (double) juce::Time::getHighResolutionTicksPerSecond();
            return frequency > 0.0 ? (double) (value - t0) * 1000.0 / frequency : -1.0;
        }
    };

    void arm() noexcept
    {
        t0_.store (0, std::memory_order_relaxed);
        t1_.store (0, std::memory_order_relaxed);
        t2_.store (0, std::memory_order_relaxed);
        t3_.store (0, std::memory_order_relaxed);
        visibleTarget_.store (0, std::memory_order_relaxed);
        presentationEntry_.store (0, std::memory_order_relaxed);
        visibleLevel_.store (0, std::memory_order_relaxed);
        repaintRequest_.store (0, std::memory_order_relaxed);
        paintEntry_.store (0, std::memory_order_relaxed);
        t4_.store (0, std::memory_order_relaxed);
        signalStartSlot_.store (0, std::memory_order_relaxed);
        armed_.store (true, std::memory_order_release);
    }

    void recordAcceptedSignalPush (uint32_t signalStartSlot) noexcept
    {
        if (! armed_.exchange (false, std::memory_order_acq_rel))
            return;

        signalStartSlot_.store (signalStartSlot, std::memory_order_relaxed);
        t0_.store (juce::Time::getHighResolutionTicks(), std::memory_order_release);
    }

    void recordConsumerRange (uint32_t firstSlot, uint32_t endSlot) noexcept
    {
        const auto t0 = t0_.load (std::memory_order_acquire);
        if (t0 <= 0 || t1_.load (std::memory_order_relaxed) != 0)
            return;

        const uint32_t signalStart = signalStartSlot_.load (std::memory_order_relaxed);
        if (firstSlot <= signalStart && endSlot > signalStart)
            recordOnce (t1_);
    }

    void recordFftComplete() noexcept
    {
        if (t1_.load (std::memory_order_acquire) > 0)
            recordOnce (t2_);
    }

    void recordSpectrumCacheUpdated() noexcept
    {
        if (t2_.load (std::memory_order_acquire) > 0)
            recordOnce (t3_);
    }

    void recordRenderedSpectrum() noexcept
    {
        if (t3_.load (std::memory_order_acquire) > 0)
            recordOnce (t4_);
    }

    void recordVisibleTargetPublished() noexcept
    {
        if (t3_.load (std::memory_order_acquire) > 0)
            recordOnce (visibleTarget_);
    }

    void recordPresentationEntry() noexcept
    {
        if (t3_.load (std::memory_order_acquire) > 0)
            recordOnce (presentationEntry_);
    }

    void recordVisibleLevelUpdated() noexcept
    {
        if (t3_.load (std::memory_order_acquire) > 0)
            recordOnce (visibleLevel_);
    }

    void recordRepaintRequested() noexcept
    {
        if (t3_.load (std::memory_order_acquire) > 0)
            recordOnce (repaintRequest_);
    }

    void recordPaintEntered() noexcept
    {
        if (t3_.load (std::memory_order_acquire) > 0)
            recordOnce (paintEntry_);
    }

    bool cacheHasUpdated() const noexcept
    {
        return t3_.load (std::memory_order_acquire) > 0;
    }

    bool isArmedForTesting() const noexcept
    {
        return armed_.load (std::memory_order_acquire);
    }

    Snapshot snapshot() const noexcept
    {
        return { t0_.load (std::memory_order_acquire),
                 t1_.load (std::memory_order_acquire),
                 t2_.load (std::memory_order_acquire),
                 t3_.load (std::memory_order_acquire),
                 visibleTarget_.load (std::memory_order_acquire),
                 presentationEntry_.load (std::memory_order_acquire),
                 visibleLevel_.load (std::memory_order_acquire),
                 repaintRequest_.load (std::memory_order_acquire),
                 paintEntry_.load (std::memory_order_acquire),
                 t4_.load (std::memory_order_acquire) };
    }

private:
    static void recordOnce (std::atomic<juce::int64>& destination) noexcept
    {
        juce::int64 expected = 0;
        destination.compare_exchange_strong (expected,
                                             juce::Time::getHighResolutionTicks(),
                                             std::memory_order_release,
                                             std::memory_order_relaxed);
    }

    std::atomic<bool> armed_ { false };
    std::atomic<uint32_t> signalStartSlot_ { 0 };
    std::atomic<juce::int64> t0_ { 0 };
    std::atomic<juce::int64> t1_ { 0 };
    std::atomic<juce::int64> t2_ { 0 };
    std::atomic<juce::int64> t3_ { 0 };
    std::atomic<juce::int64> visibleTarget_ { 0 };
    std::atomic<juce::int64> presentationEntry_ { 0 };
    std::atomic<juce::int64> visibleLevel_ { 0 };
    std::atomic<juce::int64> repaintRequest_ { 0 };
    std::atomic<juce::int64> paintEntry_ { 0 };
    std::atomic<juce::int64> t4_ { 0 };
};

} // namespace G10
} // namespace APEX
