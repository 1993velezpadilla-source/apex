#pragma once

#include <JuceHeader.h>
#include "PluginSandboxProtocolCore.h"

#include <array>
#include <cstddef>
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
namespace PluginSandboxAudioShared
{
inline constexpr std::uint64_t kMagic = 0x4150455841554431ULL; // "APEXAUD1"
inline constexpr std::uint32_t kLayoutVersion = 2;
inline constexpr std::uint32_t kSampleFormatFloat32 = 1;
// Depth-aware outstanding pipeline: exchange k consumes slot k-d where
// d = ceil(maximumHostBlockSamples / quantumSamples). The ring must hold the
// d outstanding quanta plus the quantum currently being submitted, i.e.
// d + 1 slots. With the supported maximum host block (2048 samples) and a
// 512-sample quantum, d = 4, so 5 slots guarantee no outstanding quantum is
// ever overwritten before its deterministic resolution point k+d.
inline constexpr std::uint32_t kSlotCount = 5;
inline constexpr std::uint32_t kMaxOutstandingDepth = kSlotCount - 1; // 4
inline constexpr std::uint32_t kPhysicalMaxChannels = 8;
inline constexpr std::uint32_t kPhysicalMaxSamples = 2048;
inline constexpr std::uint32_t kSamplesPerSlot = kPhysicalMaxChannels * kPhysicalMaxSamples;

enum class SlotState : LONG
{
    Free = 0,
    HostWriting = 1,
    ReadyForWorker = 2,
    WorkerProcessing = 3,
    ReadyForHost = 4
};

enum AudioSlotFlags : std::uint32_t
{
    SlotFlagNone = 0,
    SlotFlagInvalidMetadata = 1u << 0
};

struct alignas(64) SharedAtomic64Line
{
    volatile LONG64 value = 0;
    std::uint8_t padding[56] {};
};

struct alignas(64) SharedAtomic32Line
{
    volatile LONG value = 0;
    std::uint8_t padding[60] {};
};

struct alignas(64) SharedSlotStateLine
{
    volatile LONG value = static_cast<LONG>(SlotState::Free);
    std::uint8_t padding[60] {};
};

struct alignas(64) SharedAudioSlotMetadata
{
    std::uint64_t sequence = 0;
    std::uint64_t generation = 0;
    std::uint32_t numSamples = 0;
    std::uint32_t inputChannels = 0;
    std::uint32_t outputChannels = 0;
    std::uint32_t flags = 0;
    std::uint8_t reserved[32] {};
};

struct alignas(64) SharedAudioSlot
{
    SharedSlotStateLine state;
    SharedAudioSlotMetadata metadata;
    alignas(64) float input[kSamplesPerSlot] {};
    alignas(64) float output[kSamplesPerSlot] {};
};

struct alignas(64) SharedAudioTransportHeader
{
    std::uint64_t magic = 0;
    std::uint32_t layoutVersion = 0;
    std::uint32_t headerBytes = 0;
    std::uint64_t regionBytes = 0;
    std::uint32_t sampleFormat = 0;
    std::uint32_t slotCount = 0;
    std::uint32_t physicalMaxChannels = 0;
    std::uint32_t physicalMaxSamples = 0;
    std::uint32_t configuredMaxInputChannels = 0;
    std::uint32_t configuredMaxOutputChannels = 0;
    std::uint32_t configuredMaxSamples = 0;
    std::uint32_t nominalBlockSamples = 0;
    double sampleRate = 0.0;
    std::uint64_t generation = 0;
    char sessionToken[33] {};
    std::uint8_t reserved[15] {};
};

struct alignas(64) SharedAudioTransportRegion
{
    SharedAudioTransportHeader header;
    SharedAtomic32Line transportActive;
    SharedAtomic32Line workerOnline;
    SharedAtomic64Line workerHeartbeat;
    SharedAtomic64Line workerCompletedSequence;
    SharedAtomic64Line workerProcessedBlocks;
    SharedAtomic64Line workerProcessTicks;
    SharedAtomic64Line workerMetadataErrors;
    SharedAudioSlot slots[kSlotCount];
};

static_assert(sizeof(LONG) == 4);
static_assert(sizeof(LONG64) == 8);
static_assert(sizeof(float) == 4);
static_assert(sizeof(SharedAtomic64Line) == 64);
static_assert(sizeof(SharedAtomic32Line) == 64);
static_assert(sizeof(SharedSlotStateLine) == 64);
static_assert(sizeof(SharedAudioSlotMetadata) == 64);
static_assert(alignof(SharedAudioSlot) == 64);
static_assert(alignof(SharedAudioTransportRegion) == 64);
static_assert(std::is_standard_layout_v<SharedAudioTransportRegion>);
static_assert(std::is_trivially_copyable_v<SharedAudioTransportRegion>);
static_assert(offsetof(SharedAudioSlot, state) % alignof(LONG) == 0);
static_assert(offsetof(SharedAudioTransportRegion, workerHeartbeat) % alignof(LONG64) == 0);

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
                            SlotState expected,
                            SlotState replacement) noexcept
{
    return InterlockedCompareExchange(value,
                                      static_cast<LONG>(replacement),
                                      static_cast<LONG>(expected))
           == static_cast<LONG>(expected);
}

inline LONG64 increment(volatile LONG64* value) noexcept
{
    return InterlockedIncrement64(value);
}

inline LONG64 add(volatile LONG64* value, LONG64 amount) noexcept
{
    return InterlockedAdd64(value, amount);
}

inline SlotState slotState(const SharedAudioSlot& slot) noexcept
{
    return static_cast<SlotState>(loadAcquire(&slot.state.value));
}

inline float* channelData(float* base, std::uint32_t channel) noexcept
{
    return base + static_cast<std::size_t>(channel) * kPhysicalMaxSamples;
}

inline const float* channelData(const float* base, std::uint32_t channel) noexcept
{
    return base + static_cast<std::size_t>(channel) * kPhysicalMaxSamples;
}

inline bool validConfiguration(std::uint32_t inputChannels,
                               std::uint32_t outputChannels,
                               std::uint32_t maxSamples,
                               std::uint32_t nominalBlockSamples,
                               double sampleRate) noexcept
{
    return inputChannels > 0 && inputChannels <= kPhysicalMaxChannels
        && outputChannels > 0 && outputChannels <= kPhysicalMaxChannels
        && maxSamples > 0 && maxSamples <= kPhysicalMaxSamples
        && nominalBlockSamples > 0 && nominalBlockSamples <= maxSamples
        && sampleRate >= 8000.0 && sampleRate <= 384000.0;
}

inline bool validRegion(const SharedAudioTransportRegion& region,
                        const char* expectedSession = nullptr) noexcept
{
    const auto& header = region.header;
    if (header.magic != kMagic
        || header.layoutVersion != kLayoutVersion
        || header.headerBytes != sizeof(SharedAudioTransportHeader)
        || header.regionBytes != sizeof(SharedAudioTransportRegion)
        || header.sampleFormat != kSampleFormatFloat32
        || header.slotCount != kSlotCount
        || header.physicalMaxChannels != kPhysicalMaxChannels
        || header.physicalMaxSamples != kPhysicalMaxSamples
        || header.generation == 0
        || ! validConfiguration(header.configuredMaxInputChannels,
                                header.configuredMaxOutputChannels,
                                header.configuredMaxSamples,
                                header.nominalBlockSamples,
                                header.sampleRate))
        return false;

    if (expectedSession != nullptr)
        return header.sessionToken[32] == '\0'
            && std::strncmp(header.sessionToken, expectedSession, 32) == 0;
    return true;
}

inline bool validSlotMetadata(const SharedAudioTransportRegion& region,
                              const SharedAudioSlotMetadata& metadata) noexcept
{
    return metadata.sequence > 0
        && metadata.generation == region.header.generation
        && metadata.numSamples > 0
        && metadata.numSamples <= region.header.configuredMaxSamples
        && metadata.inputChannels > 0
        && metadata.inputChannels <= region.header.configuredMaxInputChannels
        && metadata.outputChannels > 0
        && metadata.outputChannels <= region.header.configuredMaxOutputChannels;
}

inline float gainForMode(PluginSandboxAudioTestDspMode mode,
                         std::uint32_t channel) noexcept
{
    switch (mode)
    {
        case PluginSandboxAudioTestDspMode::GainHalf: return 0.5f;
        case PluginSandboxAudioTestDspMode::GainQuarter: return 0.25f;
        case PluginSandboxAudioTestDspMode::ChannelMarker: return channel == 0 ? 0.5f : 0.25f;
        case PluginSandboxAudioTestDspMode::Identity:
        case PluginSandboxAudioTestDspMode::DeadlineIdentity:
        default: return 1.0f;
    }
}

inline void processDeterministicDsp(SharedAudioSlot& slot,
                                    PluginSandboxAudioTestDspMode mode) noexcept
{
    const auto numSamples = slot.metadata.numSamples;
    const auto inputChannels = slot.metadata.inputChannels;
    const auto outputChannels = slot.metadata.outputChannels;

    for (std::uint32_t channel = 0; channel < outputChannels; ++channel)
    {
        auto* destination = channelData(slot.output, channel);
        if (channel >= inputChannels)
        {
            std::fill_n(destination, numSamples, 0.0f);
            continue;
        }

        const auto* source = channelData(slot.input, channel);
        const auto gain = gainForMode(mode, channel);
        for (std::uint32_t sample = 0; sample < numSamples; ++sample)
            destination[sample] = source[sample] * gain;
    }
}
} // namespace PluginSandboxAudioShared
#endif

} // namespace DAW
