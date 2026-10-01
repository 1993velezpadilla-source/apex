// ===========================================================================
// TimelinePaintMetrics.h — Lightweight F0 baseline instrumentation.
//
// Provides:
//   - std::atomic counters (zero behavioral impact when not polled)
//   - Fixed-capacity preallocated sample ring buffers for percentile timing
//   - RAII ScopedTimer for nanosecond-precision measurement
//
// All metric writes happen inside the message-thread paint path with:
//   - No per-frame heap allocations
//   - No locks in paint() (atomic increments only)
//   - No per-frame console/file logging
//   - No audio-thread access
//
// After measurement stops, the benchmark controller reads the samples and
// computes p50/p95/p99/worst outside the paint path.
//
// Capacity: Sample rings hold 16384 entries each. Excess samples are silently
// dropped (counter recorded via droppedCount()).
// ===========================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <vector>

// ===========================================================================
// Fixed-capacity sample ring buffer — lock-free single writer, multi reader.
//
// Ownership:
//   Writer: message thread (ArrangementView paint path, timer callbacks).
//   Readers: benchmark controller after measurement stops (message thread).
//   Not accessed from audio thread.
//
// Capacity: kCapacity = 16384, preallocated in BSS. push() does zero
// allocation — only an atomic increment and a memory write. When capacity
// is reached, excess samples are silently dropped (counted in dropped()).
// Wrap behavior: deterministic — first N samples retained, rest dropped.
// Percentile computation happens only after measurement stops.
// ===========================================================================
struct SampleRing
{
    static constexpr uint32_t kCapacity = 16384;

    // ── Write path (message thread, no locks) ───────────────────────────
    void push(uint64_t sample) noexcept
    {
        uint32_t idx = writeIndex_.fetch_add(1, std::memory_order_relaxed);
        if (idx < kCapacity)
        {
            samples_[idx] = sample;
        }
        // idx >= kCapacity: silently dropped; droppedCount() reports total
    }

    // ── Read path (post-benchmark, single-threaded) ─────────────────────
    uint32_t size() const noexcept
    {
        return std::min(writeIndex_.load(std::memory_order_acquire), kCapacity);
    }

    uint32_t dropped() const noexcept
    {
        const auto written = writeIndex_.load(std::memory_order_acquire);
        return written > kCapacity ? written - kCapacity : 0;
    }

    void reset() noexcept
    {
        writeIndex_.store(0, std::memory_order_release);
    }

    // Access raw samples for percentile computation (caller must not
    // write concurrently with reads).
    const uint64_t* data() const noexcept { return samples_; }

    // ── Percentile helpers (call after measurement stops) ───────────────
    // Computes p50/p95/p99/worst from samples_ into a temporary vector.
    // Empty ring: returns a Percentiles struct with all fields = 0.
    // Caller must check size() > 0 before interpreting values.
    // One-sample ring: returns the sample for all percentiles (p50=p95=p99=worst).
    struct Percentiles
    {
        double p50_ns = 0.0;
        double p95_ns = 0.0;
        double p99_ns = 0.0;
        uint64_t worst_ns = 0;
        uint64_t mean_ns = 0;
    };

    Percentiles computePercentiles() const
    {
        Percentiles result;
        const auto n = size();
        if (n == 0) return result;

        std::vector<uint64_t> sorted(samples_, samples_ + n);
        std::sort(sorted.begin(), sorted.end());

        auto atFraction = [&](double frac) -> uint64_t
        {
            const size_t maxIdx = n > 0 ? n - 1 : 0;
            size_t idx = static_cast<size_t>(frac * static_cast<double>(maxIdx));
            if (idx > maxIdx) idx = maxIdx;
            return sorted[idx];
        };

        result.p50_ns   = static_cast<double>(atFraction(0.50));
        result.p95_ns   = static_cast<double>(atFraction(0.95));
        result.p99_ns   = static_cast<double>(atFraction(0.99));
        result.worst_ns = sorted.back();

        uint64_t sum = 0;
        for (size_t i = 0; i < n; ++i)
            sum += sorted[i];
        result.mean_ns = sum / n;

        return result;
    }

private:
    uint64_t samples_[kCapacity] = {};
    std::atomic<uint32_t> writeIndex_{0};
};

// ===========================================================================
// Metrics container — header-only, always compiled.
// Every counter is an atomic increment with relaxed ordering on x64.
// The cost is a single locked instruction — unmeasurable at paint scale.
// ===========================================================================
struct TimelinePaintMetrics
{
    // ── Runtime activation gate ──────────────────────────────────────────
    // Metrics are disabled by default for zero overhead in normal APEX
    // operation. The benchmark controller sets active=true before the
    // measurement phase and restores it afterward.
    // When inactive:
    //   - push() is a no-op (no ring writes)
    //   - fetch_add() is a no-op (no counter traffic)
    //   - no timers or probes are added for instrumentation
    //   - no file or console I/O occurs
    // Overhead when active: ~1-2 ns per atomic increment on x64, which is
    // unmeasurable at 1-16 ms paint timescale (ratio ~0.0001%).
    static inline std::atomic<bool> active{false};
    // ── Activation-aware counter increment helper ───────────────────────
    // Use count(counter) or count(counter, delta) from hot paths.
    // When active is false, the inc is skipped — zero overhead.
    template <typename T>
    static void inc(std::atomic<T>& c, T delta = T(1)) noexcept
    {
        if (active.load(std::memory_order_relaxed))
            c.fetch_add(delta, std::memory_order_relaxed);
    }

    // ── Counter group: paint invocation ──────────────────────────────────
    static inline std::atomic<uint64_t> paintCount{0};
    static inline std::atomic<uint64_t> paintOverChildrenCount{0};
    static inline std::atomic<uint64_t> rulerPaintCount{0};
    static inline std::atomic<uint64_t> toolbarPaintCount{0};
    static inline std::atomic<uint64_t> clipRenderPaints{0};

    // ── Counter group: requested repaint events (counted at source call sites) ──
    static inline std::atomic<uint64_t> requestedFullRepaints{0};
    static inline std::atomic<uint64_t> requestedPartialRepaints{0};
    static inline std::atomic<int64_t>  requestedDirtyAreaPixels{0};

    // ── Counter group: actual paint invocation (counted in paint()) ─────────────
    static inline std::atomic<uint64_t> actualFullPaintInvocations{0};
    static inline std::atomic<uint64_t> actualPartialPaintInvocations{0};

    // ── Counter group: track/clip visibility ─────────────────────────────
    static inline std::atomic<uint64_t> tracksIterated{0};
    static inline std::atomic<uint64_t> tracksCulled{0};
    static inline std::atomic<uint64_t> clipsPainted{0};
    static inline std::atomic<uint64_t> clipsCulled{0};

    // ── Counter group: viewport scroll work ──────────────────────────────
    // These counters are deliberately scoped to the scroll path rather than
    // inferred from paint counts.  A scroll can move existing presentation
    // components without producing a paint before the message loop drains.
    static inline std::atomic<uint64_t> trackListScrollEvents{0};
    static inline std::atomic<uint64_t> trackListScrollResizedCalls{0};
    static inline std::atomic<uint64_t> trackListScrollRowBoundsAssignments{0};
    static inline std::atomic<uint64_t> trackListScrollRowPositionUpdates{0};
    static inline std::atomic<uint64_t> arrangementScrollEvents{0};
    static inline std::atomic<uint64_t> arrangementScrollClipIterations{0};
    static inline std::atomic<uint64_t> arrangementScrollVisibilityChanges{0};
    static inline std::atomic<uint64_t> arrangementScrollWaveformRefreshes{0};
    static inline std::atomic<uint64_t> arrangementScrollFullRepaints{0};
    static inline std::atomic<uint64_t> arrangementScrollPartialRepaints{0};
    static inline std::atomic<int64_t>  arrangementScrollDirtyAreaPixels{0};

    // ── Counter group: waveform ─────────────────────────────────────────
    static inline std::atomic<uint64_t> waveformDrawCalls{0};
    static inline std::atomic<uint64_t> waveformPeaksWalked{0};
    static inline std::atomic<uint64_t> waveformCachedDraws{0};

    // ── Counter group: automation ───────────────────────────────────────
    static inline std::atomic<uint64_t> automationTimerTicks{0};
    static inline std::atomic<uint64_t> automationTimerSkipped{0};
    static inline std::atomic<uint64_t> automationTimerInvalidated{0};
    static inline std::atomic<uint64_t> automationContainersProcessed{0};
    static inline std::atomic<uint64_t> automationCacheInvalidations{0};
    static inline std::atomic<uint64_t> automationPhase1LanesDrawn{0};

    // ── Counter group: grid ─────────────────────────────────────────────
    static inline std::atomic<uint64_t> gridBarsGenerated{0};
    static inline std::atomic<uint64_t> gridBeatsGenerated{0};

    // ── Sample rings for timing/area distributions ──────────────────────
    static inline SampleRing paintDurationNs;               // ArrangementViewCore::paint() duration
    static inline SampleRing paintChildrenDurationNs;       // paintOverChildren() duration
    static inline SampleRing waveformDurationNs;            // Clip waveform draw duration
    static inline SampleRing frameIntervalNs;               // ArrangementView 60Hz timer interval (message thread)
    static inline SampleRing requestedDirtyAreaPixelsRing;  // Per-repaint-request area (from source call sites)
    static inline SampleRing actualPaintClipAreaPixels;     // Per-paint g.getClipBounds() area

    // ====================================================================
    // Snapshot — atomically read all simple counters (not rings)
    // ====================================================================
    struct Snapshot
    {
        uint64_t paintCount;
        uint64_t paintOverChildrenCount;
        uint64_t rulerPaintCount;
        uint64_t toolbarPaintCount;
        uint64_t clipRenderPaints;
        uint64_t requestedFullRepaints;
        uint64_t requestedPartialRepaints;
        int64_t  requestedDirtyAreaPixels;
        uint64_t actualFullPaintInvocations;
        uint64_t actualPartialPaintInvocations;
        uint64_t tracksIterated;
        uint64_t tracksCulled;
        uint64_t clipsPainted;
        uint64_t clipsCulled;
        uint64_t trackListScrollEvents;
        uint64_t trackListScrollResizedCalls;
        uint64_t trackListScrollRowBoundsAssignments;
        uint64_t trackListScrollRowPositionUpdates;
        uint64_t arrangementScrollEvents;
        uint64_t arrangementScrollClipIterations;
        uint64_t arrangementScrollVisibilityChanges;
        uint64_t arrangementScrollWaveformRefreshes;
        uint64_t arrangementScrollFullRepaints;
        uint64_t arrangementScrollPartialRepaints;
        int64_t  arrangementScrollDirtyAreaPixels;
        uint64_t waveformDrawCalls;
        uint64_t waveformPeaksWalked;
        uint64_t waveformCachedDraws;
        uint64_t automationTimerTicks;
        uint64_t automationTimerSkipped;
        uint64_t automationTimerInvalidated;
        uint64_t automationContainersProcessed;
        uint64_t automationCacheInvalidations;
        uint64_t automationPhase1LanesDrawn;
        uint64_t gridBarsGenerated;
        uint64_t gridBeatsGenerated;
    };

    static Snapshot snapshot() noexcept
    {
        return Snapshot{
            paintCount.load(),
            paintOverChildrenCount.load(),
            rulerPaintCount.load(),
            toolbarPaintCount.load(),
            clipRenderPaints.load(),
            requestedFullRepaints.load(),
            requestedPartialRepaints.load(),
            requestedDirtyAreaPixels.load(),
            actualFullPaintInvocations.load(),
            actualPartialPaintInvocations.load(),
            tracksIterated.load(),
            tracksCulled.load(),
            clipsPainted.load(),
            clipsCulled.load(),
            trackListScrollEvents.load(),
            trackListScrollResizedCalls.load(),
            trackListScrollRowBoundsAssignments.load(),
            trackListScrollRowPositionUpdates.load(),
            arrangementScrollEvents.load(),
            arrangementScrollClipIterations.load(),
            arrangementScrollVisibilityChanges.load(),
            arrangementScrollWaveformRefreshes.load(),
            arrangementScrollFullRepaints.load(),
            arrangementScrollPartialRepaints.load(),
            arrangementScrollDirtyAreaPixels.load(),
            waveformDrawCalls.load(),
            waveformPeaksWalked.load(),
            waveformCachedDraws.load(),
            automationTimerTicks.load(),
            automationTimerSkipped.load(),
            automationTimerInvalidated.load(),
            automationContainersProcessed.load(),
            automationCacheInvalidations.load(),
            automationPhase1LanesDrawn.load(),
            gridBarsGenerated.load(),
            gridBeatsGenerated.load()
        };
    }

    static void reset() noexcept
    {
        paintCount            = 0;
        paintOverChildrenCount = 0;
        rulerPaintCount       = 0;
        toolbarPaintCount     = 0;
        clipRenderPaints      = 0;
        requestedFullRepaints      = 0;
        requestedPartialRepaints   = 0;
        requestedDirtyAreaPixels    = 0;
        actualFullPaintInvocations  = 0;
        actualPartialPaintInvocations = 0;
        tracksIterated        = 0;
        tracksCulled          = 0;
        clipsPainted          = 0;
        clipsCulled           = 0;
        trackListScrollEvents = 0;
        trackListScrollResizedCalls = 0;
        trackListScrollRowBoundsAssignments = 0;
        trackListScrollRowPositionUpdates = 0;
        arrangementScrollEvents = 0;
        arrangementScrollClipIterations = 0;
        arrangementScrollVisibilityChanges = 0;
        arrangementScrollWaveformRefreshes = 0;
        arrangementScrollFullRepaints = 0;
        arrangementScrollPartialRepaints = 0;
        arrangementScrollDirtyAreaPixels = 0;
        waveformDrawCalls     = 0;
        waveformPeaksWalked   = 0;
        waveformCachedDraws   = 0;
        automationTimerTicks  = 0;
        automationTimerSkipped = 0;
        automationTimerInvalidated = 0;
        automationContainersProcessed = 0;
        automationCacheInvalidations = 0;
        automationPhase1LanesDrawn = 0;
        gridBarsGenerated     = 0;
        gridBeatsGenerated    = 0;

        paintDurationNs.reset();
        paintChildrenDurationNs.reset();
        waveformDurationNs.reset();
        frameIntervalNs.reset();
        requestedDirtyAreaPixelsRing.reset();
        actualPaintClipAreaPixels.reset();
    }

    // ── ScopedTimer — RAII that pushes elapsed ns into a SampleRing ────
    // When TimelinePaintMetrics is not active, the constructor takes no
    // timestamp and the destructor does not push — zero overhead.
    struct ScopedTimer
    {
        std::chrono::high_resolution_clock::time_point start_;
        SampleRing& target_;
        bool active_;

        ScopedTimer(SampleRing& tgt) noexcept
            : start_(),
              target_(tgt),
              active_(TimelinePaintMetrics::active.load(std::memory_order_relaxed))
        {
            if (active_)
                start_ = std::chrono::high_resolution_clock::now();
        }

        ~ScopedTimer() noexcept
        {
            if (!active_) return;
            auto end = std::chrono::high_resolution_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
            target_.push(static_cast<uint64_t>(ns));
        }
    };

    // ── ArrangementView 60Hz timer interval sampler ─────────────────────
    // Records time between consecutive message-thread timer callbacks into
    // frameIntervalNs ring.
    // This is the ArrangementView message-thread timer interval.
    // It is NOT OS-presented FPS, VSync timing, ApexPresentationClock
    // timing, or actual displayed frame rate.
    //
    // First-sample semantics:
    //   After construction or reset(), hasPrevious_ is false. The first
    //   tick() after (re)activation captures the current timestamp and sets
    //   hasPrevious_ = true, but does NOT push a sample. The second tick()
    //   computes and pushes the first valid interval from the captured
    //   timestamp. This ensures no interval includes construction, settle,
    //   warm-up, reset, or phase-transition time.
    //   This first skipped tick is intentional and documented.
    struct FrameIntervalSampler
    {
        std::chrono::high_resolution_clock::time_point lastTick_;
        bool hasPrevious_;

        FrameIntervalSampler() noexcept
            : lastTick_(), hasPrevious_(false) {}

        void tick() noexcept
        {
            if (!TimelinePaintMetrics::active.load(std::memory_order_relaxed))
                return;
            auto now = std::chrono::high_resolution_clock::now();
            if (!hasPrevious_)
            {
                // First tick after (re)activation: capture start time,
                // skip interval to exclude any non-measurement time.
                lastTick_ = now;
                hasPrevious_ = true;
                return;
            }
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastTick_).count();
            frameIntervalNs.push(static_cast<uint64_t>(ns));
            lastTick_ = now;
        }

        void reset() noexcept
        {
            lastTick_ = {};
            hasPrevious_ = false;
        }
    };

    // Singleton sampler — wired in ArrangementView::timerCallback().
    // Reset before each measurement phase.
    static inline FrameIntervalSampler frameSampler;
};
