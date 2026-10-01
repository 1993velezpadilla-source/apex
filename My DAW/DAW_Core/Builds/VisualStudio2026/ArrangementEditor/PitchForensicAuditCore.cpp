// ===========================================================================
// PitchForensicAuditCore.cpp
// ===========================================================================
#include "PitchForensicAuditCore.h"
#include "PitchZoneClassifierCore.h"

namespace ArrangementEditor
{

void PitchForensicAuditCore::reset() noexcept
{
    unifiedPitchPathHits_.store(0, std::memory_order_relaxed);
    independentPitchHits_.store(0, std::memory_order_relaxed);
    tapeResampleHits_.store(0, std::memory_order_relaxed);
    darkExtremeHits_.store(0, std::memory_order_relaxed);
    chipmunkExtremeHits_.store(0, std::memory_order_relaxed);
    formantShiftHits_.store(0, std::memory_order_relaxed);
    darkBodyHits_.store(0, std::memory_order_relaxed);
    bypassHits_.store(0, std::memory_order_relaxed);
    dryLeakDetected_.store(0, std::memory_order_relaxed);
    cryptHits_.store(0, std::memory_order_relaxed);
    echoHits_.store(0, std::memory_order_relaxed);
    howlHits_.store(0, std::memory_order_relaxed);
    wailHits_.store(0, std::memory_order_relaxed);
    reportedLatencySamples_.store(0, std::memory_order_relaxed);
}

PitchForensicCountersSnapshot PitchForensicAuditCore::snapshot() const noexcept
{
    PitchForensicCountersSnapshot s;
    s.unifiedPitchPathHits = unifiedPitchPathHits_.load(std::memory_order_relaxed);
    s.independentPitchHits = independentPitchHits_.load(std::memory_order_relaxed);
    s.tapeResampleHits = tapeResampleHits_.load(std::memory_order_relaxed);
    s.darkExtremeHits = darkExtremeHits_.load(std::memory_order_relaxed);
    s.chipmunkExtremeHits = chipmunkExtremeHits_.load(std::memory_order_relaxed);
    s.formantShiftHits = formantShiftHits_.load(std::memory_order_relaxed);
    s.darkBodyHits = darkBodyHits_.load(std::memory_order_relaxed);
    s.bypassHits = bypassHits_.load(std::memory_order_relaxed);
    s.dryLeakDetected = dryLeakDetected_.load(std::memory_order_relaxed);
    s.cryptHits = cryptHits_.load(std::memory_order_relaxed);
    s.echoHits = echoHits_.load(std::memory_order_relaxed);
    s.howlHits = howlHits_.load(std::memory_order_relaxed);
    s.wailHits = wailHits_.load(std::memory_order_relaxed);
    s.reportedLatencySamples = reportedLatencySamples_.load(std::memory_order_relaxed);
    return s;
}

void PitchForensicAuditCore::debugPrintIfDue(const UnifiedPitchSnapshot& state, uint64_t clipIdHash)
{
   #if JUCE_DEBUG
    const uint32_t now = juce::Time::getMillisecondCounter();
    if (now - lastPrintMs_ < 2000)
        return;

    lastPrintMs_ = now;
    const auto c = snapshot();
    DBG("[PITCH STATE] clipId=" << (juce::uint64)clipIdHash
        << " st=" << juce::String(state.pitchSemitones, 2)
        << " scale=" << juce::String(state.pitchScale, 3)
        << " zone=" << PitchZoneClassifierCore::zoneName(state.zone)
        << " formant=" << juce::String(state.formantScale, 2)
        << " wet=" << juce::String(state.wet, 2)
        << " stretch=" << juce::String(state.stretchRatio, 2)
        << " timelineComp=" << (state.timelineCompensation ? "on" : "off"));
    DBG("[PITCH PATH] unified=1 independent=" << juce::String(state.independentBlend, 2)
        << " tape=" << juce::String(state.tapeBlend, 2)
        << " darkBody=" << juce::String(state.bodyAmount, 2)
        << " chipmunk=" << juce::String(state.chipIntensity, 2)
        << " brightness=" << juce::String(state.brightness, 2));
    DBG("[ATMOSPHERE] crypt=" << juce::String(state.cryptAmount, 2)
        << " echo=" << juce::String(state.echoAmount, 2)
        << " howl=" << juce::String(state.howlAmount, 2)
        << " wail=" << juce::String(state.wailAmount, 2));
    DBG("[PITCH COUNTERS] unifiedPitchPathHits=" << (juce::uint64)c.unifiedPitchPathHits
        << " independentPitchHits=" << (juce::uint64)c.independentPitchHits
        << " tapeResampleHits=" << (juce::uint64)c.tapeResampleHits
        << " darkExtremeHits=" << (juce::uint64)c.darkExtremeHits
        << " chipmunkExtremeHits=" << (juce::uint64)c.chipmunkExtremeHits
        << " formantShiftHits=" << (juce::uint64)c.formantShiftHits
        << " darkBodyHits=" << (juce::uint64)c.darkBodyHits
        << " bypassHits=" << (juce::uint64)c.bypassHits
        << " dryLeakDetected=" << (juce::uint64)c.dryLeakDetected
        << " cryptHits=" << (juce::uint64)c.cryptHits
        << " echoHits=" << (juce::uint64)c.echoHits
        << " howlHits=" << (juce::uint64)c.howlHits
        << " wailHits=" << (juce::uint64)c.wailHits
        << " reportedLatencySamples=" << c.reportedLatencySamples);
   #else
    juce::ignoreUnused(state, clipIdHash);
   #endif
}

} // namespace ArrangementEditor
