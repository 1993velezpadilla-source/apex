// ===========================================================================
// PitchForensicAuditCore.h
// Debug-only lock-free pitch path counters. Audio thread increments only.
// ===========================================================================
#pragma once
#include "UnifiedPitchStateCore.h"
#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

namespace ArrangementEditor
{

struct PitchForensicCountersSnapshot
{
    uint64_t unifiedPitchPathHits = 0;
    uint64_t independentPitchHits = 0;
    uint64_t tapeResampleHits = 0;
    uint64_t darkExtremeHits = 0;
    uint64_t chipmunkExtremeHits = 0;
    uint64_t formantShiftHits = 0;
    uint64_t darkBodyHits = 0;
    uint64_t bypassHits = 0;
    uint64_t dryLeakDetected = 0;
    uint64_t cryptHits = 0;
    uint64_t echoHits = 0;
    uint64_t howlHits = 0;
    uint64_t wailHits = 0;
    int reportedLatencySamples = 0;
};

class PitchForensicAuditCore
{
public:
    void reset() noexcept;
    void markUnified() noexcept { unifiedPitchPathHits_.fetch_add(1, std::memory_order_relaxed); }
    void markIndependent() noexcept { independentPitchHits_.fetch_add(1, std::memory_order_relaxed); }
    void markTape() noexcept { tapeResampleHits_.fetch_add(1, std::memory_order_relaxed); }
    void markDarkExtreme() noexcept { darkExtremeHits_.fetch_add(1, std::memory_order_relaxed); }
    void markChipmunkExtreme() noexcept { chipmunkExtremeHits_.fetch_add(1, std::memory_order_relaxed); }
    void markFormantShift() noexcept { formantShiftHits_.fetch_add(1, std::memory_order_relaxed); }
    void markDarkBody() noexcept { darkBodyHits_.fetch_add(1, std::memory_order_relaxed); }
    void markBypass() noexcept { bypassHits_.fetch_add(1, std::memory_order_relaxed); }
    void markDryLeak() noexcept { dryLeakDetected_.fetch_add(1, std::memory_order_relaxed); }
    void markCrypt() noexcept { cryptHits_.fetch_add(1, std::memory_order_relaxed); }
    void markEcho() noexcept { echoHits_.fetch_add(1, std::memory_order_relaxed); }
    void markHowl() noexcept { howlHits_.fetch_add(1, std::memory_order_relaxed); }
    void markWail() noexcept { wailHits_.fetch_add(1, std::memory_order_relaxed); }
    void setReportedLatencySamples(int samples) noexcept { reportedLatencySamples_.store(samples, std::memory_order_relaxed); }

    PitchForensicCountersSnapshot snapshot() const noexcept;
    void debugPrintIfDue(const UnifiedPitchSnapshot& state, uint64_t clipIdHash = 0);

private:
    std::atomic<uint64_t> unifiedPitchPathHits_ { 0 };
    std::atomic<uint64_t> independentPitchHits_ { 0 };
    std::atomic<uint64_t> tapeResampleHits_ { 0 };
    std::atomic<uint64_t> darkExtremeHits_ { 0 };
    std::atomic<uint64_t> chipmunkExtremeHits_ { 0 };
    std::atomic<uint64_t> formantShiftHits_ { 0 };
    std::atomic<uint64_t> darkBodyHits_ { 0 };
    std::atomic<uint64_t> bypassHits_ { 0 };
    std::atomic<uint64_t> dryLeakDetected_ { 0 };
    std::atomic<uint64_t> cryptHits_ { 0 };
    std::atomic<uint64_t> echoHits_ { 0 };
    std::atomic<uint64_t> howlHits_ { 0 };
    std::atomic<uint64_t> wailHits_ { 0 };
    std::atomic<int> reportedLatencySamples_ { 0 };
    uint32_t lastPrintMs_ = 0;
};

} // namespace ArrangementEditor
