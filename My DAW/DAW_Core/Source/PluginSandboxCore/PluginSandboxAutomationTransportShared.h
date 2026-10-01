#pragma once

#include <JuceHeader.h>
#include "PluginSandboxProtocolCore.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX   // keep windows.h min/max macros from poisoning std::min/std::max
 #endif
 #include <windows.h>
#endif

namespace DAW {

#if JUCE_WINDOWS
namespace PluginSandboxAutomationShared
{
// ── Phase E2A: realtime parameter-event transport primitive ─────────────────
// Additive shared-memory sidecar. Own mapping, own magic/layout/session/
// generation. The frozen Phase B audio region is NOT part of this layout.

inline constexpr std::uint64_t kAutomationMagic = 0x4150455841555431ULL; // "APEXAUT1"
inline constexpr std::uint32_t kAutomationLayoutVersion = 1;
inline constexpr std::uint32_t kAutomationBatchSlotCount = 5;   // mirrors the audio ring
inline constexpr std::uint32_t kMaxAutomationEventsPerQuantum = 64;
inline constexpr std::uint32_t kMaxAutomationParameters = 128;
inline constexpr std::uint32_t kOverflowMarker = 0xFFFFFFFFu;

// Compact trivially-copyable event. sizeof MUST be 12 (no per-event cache-line
// padding: single-writer immutable batch; only the batch header is isolated).
struct alignas(4) SandboxAutomationEvent
{
    std::uint32_t parameterOrdinal = 0;
    std::uint32_t sampleOffset = 0;      // 0 <= sampleOffset < Q
    float normalizedValue = 0.0f;
};
static_assert(sizeof(SandboxAutomationEvent) == 12,
              "E2A automation events must be exactly 12 bytes");

enum class AutomationBatchState : LONG
{
    Free = 0,
    Writing = 1,
    Ready = 2,
    WorkerReading = 3
};

struct alignas(64) AutomationBatchHeader
{
    std::uint64_t magic = kAutomationMagic;
    std::uint32_t layoutVersion = kAutomationLayoutVersion;
    std::uint32_t headerBytes = 0;
    std::uint32_t regionBytes = 0;
    std::uint32_t batchSlotCount = 0;
    std::uint32_t maxEventsPerBatch = 0;
    std::uint32_t maxParameters = 0;
    std::uint64_t generation = 0;
    char sessionToken[33] {};
    std::uint8_t reserved[19] {};
};
static_assert(sizeof(AutomationBatchHeader) <= 128, "E2A header must stay bounded");

struct alignas(64) AutomationAtomic32Line
{
    volatile LONG value = 0;
    std::uint8_t padding[60] {};
};

struct alignas(64) AutomationAtomic64Line
{
    volatile LONG64 value = 0;
    std::uint8_t padding[56] {};
};

/** One automation batch bound to exactly one audio quantum sequence. */
struct alignas(64) AutomationBatchSlot
{
    AutomationAtomic32Line state;            // AutomationBatchState
    AutomationAtomic64Line audioSequence;
    AutomationAtomic32Line eventCount;       // 0 = intentional none; kOverflowMarker = invalid
    alignas(64) SandboxAutomationEvent events[kMaxAutomationEventsPerQuantum] {};
};
static_assert(sizeof(AutomationBatchSlot) == 64 + 64 + 64 + 768,
              "E2A batch slot layout must stay compact");
static_assert(alignof(AutomationBatchSlot) == 64);
static_assert(std::is_standard_layout_v<AutomationBatchSlot>);
static_assert(std::is_trivially_copyable_v<AutomationBatchSlot>);

struct alignas(64) AutomationTransportRegion
{
    AutomationBatchHeader header;
    AutomationAtomic32Line transportActive;
    AutomationAtomic64Line latestPublishedSequence;
    AutomationAtomic64Line parentOverflowRejected;
    AutomationAtomic64Line workerInvalidBatches;
    AutomationAtomic64Line workerAppliedEvents;
    AutomationBatchSlot slots[kAutomationBatchSlotCount];
};
static_assert(std::is_standard_layout_v<AutomationTransportRegion>);
static_assert(std::is_trivially_copyable_v<AutomationTransportRegion>);

inline LONG loadAcquire(const volatile LONG* value) noexcept
{
    return InterlockedCompareExchange(const_cast<volatile LONG*>(value), 0, 0);
}

inline LONG64 loadAcquire(const volatile LONG64* value) noexcept
{
    return InterlockedCompareExchange64(const_cast<volatile LONG64*>(value), 0, 0);
}

inline void storeRelease(volatile LONG* value, LONG replacement) noexcept
{
    InterlockedExchange(value, replacement);
}

inline void storeRelease(volatile LONG64* value, LONG64 replacement) noexcept
{
    InterlockedExchange64(value, replacement);
}

inline bool compareExchange(volatile LONG* value,
                            AutomationBatchState expected,
                            AutomationBatchState replacement) noexcept
{
    return InterlockedCompareExchange(value,
                                      static_cast<LONG>(replacement),
                                      static_cast<LONG>(expected))
           == static_cast<LONG>(expected);
}

inline AutomationBatchState batchState(const AutomationBatchSlot& slot) noexcept
{
    return static_cast<AutomationBatchState>(loadAcquire(&slot.state.value));
}

inline bool validRegion(const AutomationTransportRegion& region,
                        const char* expectedSession = nullptr) noexcept
{
    const auto& header = region.header;
    if (header.magic != kAutomationMagic
        || header.layoutVersion != kAutomationLayoutVersion
        || header.headerBytes != sizeof(AutomationBatchHeader)
        || header.regionBytes != sizeof(AutomationTransportRegion)
        || header.batchSlotCount != kAutomationBatchSlotCount
        || header.maxEventsPerBatch != kMaxAutomationEventsPerQuantum
        || header.maxParameters != kMaxAutomationParameters
        || header.generation == 0)
        return false;

    if (expectedSession != nullptr)
        return header.sessionToken[32] == '\0'
            && std::strncmp(header.sessionToken, expectedSession, 32) == 0;
    return true;
}

inline bool validEvent(const SandboxAutomationEvent& event,
                       std::uint32_t quantumSamples) noexcept
{
    return event.parameterOrdinal < kMaxAutomationParameters
        && event.sampleOffset < quantumSamples
        && std::isfinite(event.normalizedValue)
        && event.normalizedValue >= 0.0f
        && event.normalizedValue <= 1.0f;
}

/** A batch is valid when its events are ordered, bounded, and individually
    valid. Called on a snapshot, never on shared memory mid-write. */
inline bool validBatchEvents(const SandboxAutomationEvent* events,
                             std::uint32_t eventCount,
                             std::uint32_t quantumSamples) noexcept
{
    if (eventCount > kMaxAutomationEventsPerQuantum)
        return false;
    for (std::uint32_t i = 0; i < eventCount; ++i)
    {
        if (! validEvent(events[i], quantumSamples))
            return false;
        if (i > 0 && events[i].sampleOffset < events[i - 1].sampleOffset)
            return false;   // nondecreasing offset order required
    }
    return true;
}
} // namespace PluginSandboxAutomationShared
#endif

} // namespace DAW
