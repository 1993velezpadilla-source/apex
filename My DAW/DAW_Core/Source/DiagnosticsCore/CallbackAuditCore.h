#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

/**
    Callback audit core — low-overhead realtime telemetry.

    The audio thread pushes one fixed-size POD record per callback (SPSC
    ring, drop-newest) and folds it into the accumulator. Everything on the
    audio-thread path is noexcept, allocation-free and lock-free.

    Low-buffer forensic additions:
      - intervalTicks: measured start-to-start interval, so a deadline miss
        can be classified as ENGINE OVERRUN (duration exceeded the nominal
        period) vs LATE DELIVERY (the driver/OS delivered the callback late
        but APEX finished within budget). This is the distinction that keeps
        external driver/DPC glitches from being misattributed to APEX.
      - stageTicks: per-stage durations inside the callback (input,
        recording, click, monitor trim, engine, master bus, click sum,
        control room) — the answer to "what ran during the slow block?".
      - context fields (transport flags, track count, graph version) for
        spike forensics.

    Threading: add() runs on the audio thread; snapshot()/reset() run on
    the message thread. All shared state is atomic (relaxed) — the previous
    plain-member version was a cross-thread data race. Statistics may tear
    slightly across fields (each individual field is consistent); that is
    accepted and documented for telemetry.

    lateDeliveries semantics (2026-07-25, user decision): a STRICT
    start-to-start interval > nominal period measurement of CALLBACK
    DELIVERY JITTER. At ultra-small buffers (32/64 samples) ordinary
    driver/OS scheduling jitter trips this counter frequently; it is NOT
    by itself evidence of an audible failure, deadline miss, engine
    overrun, or xrun. Do not conflate four distinct facts: delivery
    jitter (lateDeliveries), duration > period (engineOverruns), the
    deadlineMiss flag, and driver-reported xruns.

    ringOverflows semantics: records dropped because the message-thread
    drain fell behind the callback rate. Drained records are discarded by
    design — every record is folded into the accumulator on the audio
    thread, so no statistics are lost. The drain cadence is adaptive
    (computeAuditDrainIntervalSeconds) so this counter only fires on a
    genuinely stalled drain, not by design at low buffers.
*/

/** Stage identifiers for CallbackAuditRecord::stageTicks. */
enum CallbackAuditStage : size_t
{
    CallbackStageInput       = 0,   // hardware input preservation
    CallbackStageRecording   = 1,   // recording capture handoff
    CallbackStageClick       = 2,   // metronome / count-in render
    CallbackStageMonitorTrim = 3,   // live input monitor trim + meter
    CallbackStageEngine      = 4,   // AudioEngine::process (tracks/plugins/routing/master sum)
    CallbackStageMasterBus   = 5,   // master bus inserts/fader/meter/ceiling/dither/render tap
    CallbackStageClickSum    = 6,   // click bus summed into output
    CallbackStageControlRoom = 7,   // control room (dim/mute/mono/trim, monitor meters)
    kCallbackAuditStageCount = 8
};

/** Nominal callback deadline in high-resolution ticks, derived from the
    ACTUAL device configuration (never a fixed 44.1/48 kHz assumption).
    Returns 0 for invalid inputs; callers treat 0 as "unarmed". */
constexpr int64_t computeCallbackPeriodTicks (int numSamples,
                                              double sampleRate,
                                              double ticksPerSecond) noexcept
{
    return (numSamples > 0 && sampleRate > 0.0 && ticksPerSecond > 0.0)
        ? static_cast<int64_t> ((static_cast<double> (numSamples) / sampleRate) * ticksPerSecond)
        : 0;
}

/** Adaptive audit-ring drain cadence. The ring must be drained within
    half-capacity worth of callback periods or ringOverflows fires by
    design at low buffers (Brain §34.21: the lost-event counter must stay
    meaningful). 48k/32 -> ~0.34 s; 192k/32 -> ~0.085 s; large blocks
    clamp to the legacy 5 s cadence. Degenerate inputs fall back to 5 s. */
inline double computeAuditDrainIntervalSeconds (double periodSeconds, size_t ringCapacity) noexcept
{
    if (periodSeconds <= 0.0 || ringCapacity == 0)
        return 5.0;
    const double halfCapacitySeconds = 0.5 * static_cast<double> (ringCapacity) * periodSeconds;
    if (halfCapacitySeconds < 0.05) return 0.05;
    if (halfCapacitySeconds > 5.0)  return 5.0;
    return halfCapacitySeconds;
}

struct CallbackAuditRecord
{
    uint64_t sequence = 0;
    int64_t startTicks = 0;
    int64_t durationTicks = 0;
    int64_t periodTicks = 0;                 // nominal deadline in ticks
    int64_t intervalTicks = 0;               // measured start-to-start interval
    int32_t numSamples = 0;
    uint32_t streamGeneration = 0;
    uint8_t deadlineMiss = 0;
    uint8_t flags = 0;                       // bit0 playing, bit1 recording, bit2 monitoring active
    uint8_t reserved_ = 0;
    uint32_t contextTrackCount = 0;
    uint64_t contextGraphVersion = 0;
    std::array<int64_t, (size_t) kCallbackAuditStageCount> stageTicks {};
};

static_assert (std::is_trivially_copyable<CallbackAuditRecord>::value,
               "audit record must stay POD for the SPSC ring");

struct CallbackAuditSnapshot
{
    uint64_t callbacks = 0;
    uint64_t deadlineMisses = 0;
    uint64_t maximumConsecutiveMisses = 0;
    uint64_t ringOverflows = 0;
    uint64_t engineOverruns = 0;             // duration > period
    uint64_t lateDeliveries = 0;             // interval > period while duration <= period
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double p999 = 0.0;
    double p9999 = 0.0;
    double maximum = 0.0;                    // max duration/period ratio
    double intervalMaximum = 0.0;            // max interval/period ratio
    CallbackAuditRecord maxRecord;           // slowest callback seen (stage breakdown + context)
};

template <size_t Capacity>
class CallbackAuditRing final
{
public:
    static constexpr size_t kCapacity = Capacity;

    bool tryPush (const CallbackAuditRecord& record) noexcept
    {
        const auto w = writeIdx_.load (std::memory_order_relaxed);
        const auto r = readIdx_.load (std::memory_order_acquire);

        if (w - r >= Capacity)
        {
            overflowCount_.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        buffer_[static_cast<size_t> (w % Capacity)] = record;
        writeIdx_.store (w + 1, std::memory_order_release);
        return true;
    }

    bool tryPop (CallbackAuditRecord& record) noexcept
    {
        const auto r = readIdx_.load (std::memory_order_relaxed);
        const auto w = writeIdx_.load (std::memory_order_acquire);

        if (r >= w)
            return false;

        record = buffer_[static_cast<size_t> (r % Capacity)];
        readIdx_.store (r + 1, std::memory_order_release);
        return true;
    }

    uint64_t getOverflowCount() const noexcept
    {
        return overflowCount_.load (std::memory_order_relaxed);
    }

private:
    std::array<CallbackAuditRecord, Capacity> buffer_{};
    std::atomic<uint64_t> writeIdx_{ 0 };
    std::atomic<uint64_t> readIdx_{ 0 };
    std::atomic<uint64_t> overflowCount_{ 0 };
};

class CallbackAuditAccumulator final
{
public:
    static constexpr size_t kHistogramBins = 2000;
    static constexpr double kMaxRatio = 2.0;
    static constexpr double kBinWidth = kMaxRatio / static_cast<double> (kHistogramBins);

    void add (const CallbackAuditRecord& record) noexcept
    {
        const double ratio = (record.periodTicks > 0)
            ? static_cast<double> (record.durationTicks) / static_cast<double> (record.periodTicks)
            : 0.0;

        const auto bin = static_cast<size_t> (ratio / kBinWidth);
        if (bin < kHistogramBins)
            histogram_[bin].fetch_add (1, std::memory_order_relaxed);
        else
            histogram_[kHistogramBins - 1].fetch_add (1, std::memory_order_relaxed);

        // Sole-writer max (audio thread is the only add() caller).
        if (ratio > maximum_.load (std::memory_order_relaxed))
        {
            maximum_.store (ratio, std::memory_order_relaxed);
            maxRecord_ = record;             // POD copy — captures stage/context of the slowest block
        }

        if (record.periodTicks > 0 && record.intervalTicks > record.periodTicks)
        {
            const double intervalRatio = static_cast<double> (record.intervalTicks)
                                       / static_cast<double> (record.periodTicks);
            if (intervalRatio > intervalMaximum_.load (std::memory_order_relaxed))
                intervalMaximum_.store (intervalRatio, std::memory_order_relaxed);

            // Late delivery: the callback arrived late but APEX still
            // finished within budget — evidence AGAINST an APEX overrun.
            if (record.durationTicks <= record.periodTicks)
                lateDeliveries_.fetch_add (1, std::memory_order_relaxed);
        }

        if (record.periodTicks > 0 && record.durationTicks > record.periodTicks)
            engineOverruns_.fetch_add (1, std::memory_order_relaxed);

        callbacks_.fetch_add (1, std::memory_order_relaxed);

        if (record.deadlineMiss != 0)
        {
            deadlineMisses_.fetch_add (1, std::memory_order_relaxed);
            const auto c = consecutiveMisses_.load (std::memory_order_relaxed) + 1;
            consecutiveMisses_.store (c, std::memory_order_relaxed);
            if (c > maxConsecutiveMisses_.load (std::memory_order_relaxed))
                maxConsecutiveMisses_.store (c, std::memory_order_relaxed);
        }
        else
        {
            consecutiveMisses_.store (0, std::memory_order_relaxed);
        }
    }

    CallbackAuditSnapshot snapshot (uint64_t ringOverflows) const noexcept
    {
        CallbackAuditSnapshot snap;
        snap.callbacks = callbacks_.load (std::memory_order_relaxed);
        snap.deadlineMisses = deadlineMisses_.load (std::memory_order_relaxed);
        snap.maximumConsecutiveMisses = maxConsecutiveMisses_.load (std::memory_order_relaxed);
        snap.ringOverflows = ringOverflows;
        snap.engineOverruns = engineOverruns_.load (std::memory_order_relaxed);
        snap.lateDeliveries = lateDeliveries_.load (std::memory_order_relaxed);
        snap.maximum = maximum_.load (std::memory_order_relaxed);
        snap.intervalMaximum = intervalMaximum_.load (std::memory_order_relaxed);
        snap.maxRecord = maxRecord_;

        if (snap.callbacks > 0)
        {
            snap.p50  = percentile (0.50, snap.callbacks);
            snap.p95  = percentile (0.95, snap.callbacks);
            snap.p99  = percentile (0.99, snap.callbacks);
            snap.p999 = percentile (0.999, snap.callbacks);
            snap.p9999 = percentile (0.9999, snap.callbacks);
        }

        return snap;
    }

    void reset() noexcept
    {
        for (auto& bin : histogram_)
            bin.store (0, std::memory_order_relaxed);
        callbacks_.store (0, std::memory_order_relaxed);
        deadlineMisses_.store (0, std::memory_order_relaxed);
        maxConsecutiveMisses_.store (0, std::memory_order_relaxed);
        consecutiveMisses_.store (0, std::memory_order_relaxed);
        engineOverruns_.store (0, std::memory_order_relaxed);
        lateDeliveries_.store (0, std::memory_order_relaxed);
        maximum_.store (0.0, std::memory_order_relaxed);
        intervalMaximum_.store (0.0, std::memory_order_relaxed);
        maxRecord_ = CallbackAuditRecord{};
    }

private:
    double percentile (double p, uint64_t totalCallbacks) const noexcept
    {
        const uint64_t target = static_cast<uint64_t> (p * static_cast<double> (totalCallbacks));
        uint64_t cumulative = 0;

        for (size_t i = 0; i < kHistogramBins; ++i)
        {
            cumulative += histogram_[i].load (std::memory_order_relaxed);
            if (cumulative >= target)
                return static_cast<double> (i) * kBinWidth;
        }

        return kMaxRatio;
    }

    std::array<std::atomic<uint64_t>, kHistogramBins> histogram_{};
    std::atomic<uint64_t> callbacks_ { 0 };
    std::atomic<uint64_t> deadlineMisses_ { 0 };
    std::atomic<uint64_t> maxConsecutiveMisses_ { 0 };
    std::atomic<uint64_t> consecutiveMisses_ { 0 };
    std::atomic<uint64_t> engineOverruns_ { 0 };
    std::atomic<uint64_t> lateDeliveries_ { 0 };
    std::atomic<double> maximum_ { 0.0 };
    std::atomic<double> intervalMaximum_ { 0.0 };
    CallbackAuditRecord maxRecord_;          // written by the sole add() caller; read by snapshot()
};
