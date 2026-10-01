#pragma once
#include <JuceHeader.h>
#include "G10AnalyzerTimingProbe.h"

namespace APEX {
namespace G10 {

// ============================================================================
// G10AnalyzerFifo — lock-free single-producer / single-consumer ring buffer
// for the live spectrum analyzer.
//
// Thread contract:
//   push() / pushStereo() -> audio thread only (processBlock tail)
//   drain() / drainFrames()-> GUI thread only (editor timer)
//   setNumChannels()      -> message thread only (prepareToPlay; the audio
//                            thread is stopped by the host while this runs)
//
// Fixed capacity, power-of-two, preallocated at construction. No locks, no
// allocation, no atomics beyond the two monotonically increasing indices.
// Producer overflow remains fail-fast: the newest visualization block is
// dropped rather than blocking or overwriting unread storage. The GUI-side
// newest-window drain catches up in one bounded operation, discarding stale
// VISUALIZATION history while preserving the newest complete FFT window.
// None of this changes or drops the real audio delivered to the host.
//
// Storage: interleaved L/R frames (stereo) or plain samples (mono, duplicated
// by the consumer). The mode is fixed per prepareToPlay by the bus layout, so
// the consumer never mixes formats.
// ============================================================================

class G10AnalyzerFifo final
{
public:
    static constexpr int kCapacity = 16384; // 8192 stereo frames (~170 ms at 48 kHz)

    G10AnalyzerFifo() = default;

    /** Focused-test hook. Production leaves this null. The caller must detach
        before destroying the fixed-lifetime probe. */
    void setTimingProbe (G10AnalyzerTimingProbe* probe) noexcept
    {
        timingProbe_.store (probe, std::memory_order_release);
    }

    /** Message thread (prepareToPlay): 1 = mono storage, 2 = interleaved stereo.
        Also discards any data buffered under the previous layout. */
    void setNumChannels (int channels) noexcept
    {
        reset();
        channels_ = channels >= 2 ? 2 : 1;
    }

    /** Audio thread. Copies n mono samples into the ring. */
    void push (const float* data, int n) noexcept
    {
        if (data == nullptr || n <= 0)
            return;

        const uint32_t w = write_.load (std::memory_order_relaxed);
        const uint32_t r = read_.load (std::memory_order_acquire);

        if (w - r + (uint32_t) n > (uint32_t) kCapacity)
            return; // full: drop the newest block, never overwrite unread data

        for (int i = 0; i < n; ++i)
            data_[(w + (uint32_t) i) & (kCapacity - 1)] = data[i];

        if (auto* probe = timingProbe_.load (std::memory_order_relaxed))
            probe->recordAcceptedSignalPush (w);
        write_.store (w + (uint32_t) n, std::memory_order_release);
    }

    /** Audio thread. Copies n stereo frames interleaved (L,R,L,R,...) into the
        ring. The analyzer combines the channels by power, so anti-phase
        material is measured instead of cancelled. */
    void pushStereo (const float* l, const float* r, int n) noexcept
    {
        if (l == nullptr || r == nullptr || n <= 0)
            return;

        const uint32_t total = (uint32_t) n * 2u;
        const uint32_t w = write_.load (std::memory_order_relaxed);
        const uint32_t rd = read_.load (std::memory_order_acquire);

        if (w - rd + total > (uint32_t) kCapacity)
            return; // full: drop the newest block

        for (int i = 0; i < n; ++i)
        {
            const uint32_t base = (w + (uint32_t) (i * 2)) & (kCapacity - 1);
            data_[base] = l[i];
            data_[(base + 1u) & (kCapacity - 1)] = r[i];
        }

        if (auto* probe = timingProbe_.load (std::memory_order_relaxed))
            probe->recordAcceptedSignalPush (w);
        write_.store (w + total, std::memory_order_release);
    }

    /** GUI thread: drains up to maxN samples; returns the count copied. */
    int drain (float* dst, int maxN) noexcept
    {
        if (dst == nullptr || maxN <= 0)
            return 0;

        const uint32_t r = read_.load (std::memory_order_relaxed);
        const uint32_t w = write_.load (std::memory_order_acquire);
        const uint32_t available = w - r;

        if (available == 0)
            return 0;

        const int n = (int) juce::jmin ((uint32_t) maxN, available);
        for (int i = 0; i < n; ++i)
            dst[i] = data_[(r + (uint32_t) i) & (kCapacity - 1)];

        read_.store (r + (uint32_t) n, std::memory_order_release);
        return n;
    }

    /** GUI thread: drains up to maxFrames frames into l/r, de-interleaving
        stereo storage or duplicating the single channel for mono storage. */
    int drainFrames (float* l, float* r, int maxFrames) noexcept
    {
        if (l == nullptr || r == nullptr || maxFrames <= 0)
            return 0;

        const uint32_t rd = read_.load (std::memory_order_relaxed);
        const uint32_t wr = write_.load (std::memory_order_acquire);

        if (channels_ == 2)
        {
            const uint32_t availableFrames = (wr - rd) / 2u;
            if (availableFrames == 0)
                return 0;
            const int n = (int) juce::jmin ((uint32_t) maxFrames, availableFrames);
            for (int i = 0; i < n; ++i)
            {
                const uint32_t base = (rd + (uint32_t) (i * 2)) & (kCapacity - 1);
                l[i] = data_[base];
                r[i] = data_[(base + 1u) & (kCapacity - 1)];
            }
            read_.store (rd + (uint32_t) (n * 2), std::memory_order_release);
            if (auto* probe = timingProbe_.load (std::memory_order_relaxed))
                probe->recordConsumerRange (rd, rd + (uint32_t) (n * 2));
            return n;
        }

        // Mono storage: duplicate the single channel.
        const uint32_t availableFrames = wr - rd;
        if (availableFrames == 0)
            return 0;
        const int n = (int) juce::jmin ((uint32_t) maxFrames, availableFrames);
        for (int i = 0; i < n; ++i)
        {
            const float s = data_[(rd + (uint32_t) i) & (kCapacity - 1)];
            l[i] = s;
            r[i] = s;
        }
        read_.store (rd + (uint32_t) n, std::memory_order_release);
        if (auto* probe = timingProbe_.load (std::memory_order_relaxed))
            probe->recordConsumerRange (rd, rd + (uint32_t) n);
        return n;
    }

    /** GUI thread: consume the complete FIFO snapshot while copying only its
        newest maxFrames frames. Older queued frames are visualization history,
        so they are discarded when the GUI has fallen behind. Copying occurs
        before publishing the new read index; the producer therefore cannot
        overwrite any frame being read. Frames appended after the write-index
        snapshot remain queued for the next tick. */
    int drainNewestFrames (float* l, float* r, int maxFrames) noexcept
    {
        if (l == nullptr || r == nullptr || maxFrames <= 0)
            return 0;

        const uint32_t rd = read_.load (std::memory_order_relaxed);
        const uint32_t wr = write_.load (std::memory_order_acquire);
        const uint32_t slotsPerFrame = channels_ == 2 ? 2u : 1u;
        const uint32_t availableFrames = (wr - rd) / slotsPerFrame;
        if (availableFrames == 0)
            return 0;

        const int framesToCopy = (int) juce::jmin ((uint32_t) maxFrames, availableFrames);
        const uint32_t slotsToCopy = (uint32_t) framesToCopy * slotsPerFrame;
        const uint32_t firstCopiedSlot = wr - slotsToCopy;

        if (channels_ == 2)
        {
            for (int i = 0; i < framesToCopy; ++i)
            {
                const uint32_t base = (firstCopiedSlot + (uint32_t) (i * 2)) & (kCapacity - 1);
                l[i] = data_[base];
                r[i] = data_[(base + 1u) & (kCapacity - 1)];
            }
        }
        else
        {
            for (int i = 0; i < framesToCopy; ++i)
            {
                const float sample = data_[(firstCopiedSlot + (uint32_t) i) & (kCapacity - 1)];
                l[i] = sample;
                r[i] = sample;
            }
        }

        // Consume through the captured write boundary. A concurrent producer
        // may already have appended beyond wr; those newer frames remain.
        read_.store (wr, std::memory_order_release);
        if (auto* probe = timingProbe_.load (std::memory_order_relaxed))
            probe->recordConsumerRange (firstCopiedSlot, wr);
        return framesToCopy;
    }

    /** GUI thread: number of samples currently buffered. */
    int available() const noexcept
    {
        return (int) (write_.load (std::memory_order_acquire)
                    - read_.load (std::memory_order_acquire));
    }

    /** GUI thread: discard everything (e.g. after a prepareToPlay). */
    void reset() noexcept
    {
        read_.store (write_.load (std::memory_order_acquire), std::memory_order_release);
    }

private:
    alignas (64) float data_[kCapacity] = {};
    alignas (64) std::atomic<uint32_t> write_ { 0 };
    alignas (64) std::atomic<uint32_t> read_  { 0 };
    std::atomic<G10AnalyzerTimingProbe*> timingProbe_ { nullptr };
    int channels_ = 2; // fixed per prepareToPlay (message thread), never the audio thread
};

} // namespace G10
} // namespace APEX
