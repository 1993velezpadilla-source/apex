#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace DAW {

/**
 * LiveRecordWaveformCore
 *
 * Lock-free peak buffer for live recording waveform display.
 * Written from the audio thread (pushPeak), read from the UI thread (draw).
 * Each slot holds the min/max amplitude of one audio block.
 *
 * Thread safety: single writer (audio thread), single reader (UI thread).
 * Uses a fixed-size pre-allocated ring. On overflow the oldest peaks are
 * silently dropped — the UI always shows the most-recent window.
 */
class LiveRecordWaveformCore
{
public:
    static constexpr int kCapacity = 65536; // peaks; @512 spp covers ~12 min @ 44100

    LiveRecordWaveformCore()
    {
        mins_ = std::make_unique<std::atomic<float>[]>(kCapacity);
        maxs_ = std::make_unique<std::atomic<float>[]>(kCapacity);
        ends_ = std::make_unique<std::atomic<std::int64_t>[]>(kCapacity);

        for (int i = 0; i < kCapacity; ++i)
        {
            mins_[i].store(0.0f, std::memory_order_relaxed);
            maxs_[i].store(0.0f, std::memory_order_relaxed);
            ends_[i].store(0, std::memory_order_relaxed);
        }
    }

    // ── Audio thread ─────────────────────────────────────────────────────────

    /** Call once when recording starts (message thread, before audio runs). */
    void reset(int samplesPerBlock, std::int64_t start = 0,
               std::int64_t loopStart = 0, std::int64_t loopEnd = 0) noexcept
    {
        takeStart_ = start;
        loopStart_ = loopStart;
        loopEnd_ = loopEnd;
        samplesPerPeak_.store(juce::jmax(1, samplesPerBlock), std::memory_order_relaxed);
        inputAvailable_.store(false, std::memory_order_release);
        takeActive_.store(false, std::memory_order_release);
        capturedSamples_.store(0, std::memory_order_release);
        writeHead_.store(0, std::memory_order_release);
    }

    void setInputAvailable(bool available) noexcept
    {
        inputAvailable_.store(available, std::memory_order_release);
    }

    bool isInputAvailable() const noexcept
    {
        return inputAvailable_.load(std::memory_order_acquire);
    }

    void setTakeActive(bool active) noexcept
    {
        takeActive_.store(active, std::memory_order_release);
    }

    bool isTakeActive() const noexcept
    {
        return takeActive_.load(std::memory_order_acquire);
    }

    /** Push one block's worth of peaks from the audio thread. */
    void pushPeak(float minVal, float maxVal, int samples = 0) noexcept
    {
        if (! std::isfinite(minVal) || ! std::isfinite(maxVal))
            return;

        minVal = juce::jlimit(-1.0f, 1.0f, minVal);
        maxVal = juce::jlimit(-1.0f, 1.0f, maxVal);

        if (minVal > maxVal)
        {
            const float tmp = minVal;
            minVal = maxVal;
            maxVal = tmp;
        }

        const auto head = writeHead_.load(std::memory_order_relaxed);
        const auto end = capturedSamples_.load(std::memory_order_relaxed)
            + (samples > 0 ? samples : getSamplesPerPeak());
        const auto slot = slotFor(head);
        // Mark a recycled slot before replacing its peaks. A reader which
        // sees either new value will also see the invalidation or the new end.
        ends_[slot].store(0, std::memory_order_relaxed);
        mins_[slot].store(minVal, std::memory_order_release);
        maxs_[slot].store(maxVal, std::memory_order_release);
        ends_[slot].store(end, std::memory_order_release);
        writeHead_.store(head + 1, std::memory_order_release);
        capturedSamples_.store(end, std::memory_order_release);
    }

    // The producer's accepted frame count is independent of the playhead,
    // which may stop, seek or wrap while the take is still being written.
    struct RecordedSpan
    {
        std::int64_t timelineStart;
        std::int64_t sourceOffset;
        std::int64_t length;
    };

    RecordedSpan getRecordedSpan() const noexcept
    {
        const auto captured = capturedSamples_.load(std::memory_order_acquire);
        if (loopEnd_ > takeStart_ && takeStart_ >= loopStart_)
        {
            const auto initialLength = loopEnd_ - takeStart_;
            if (captured > initialLength)
            {
                const auto loopLength = loopEnd_ - loopStart_;
                const auto sourceOffset = initialLength + ((captured - initialLength - 1) / loopLength) * loopLength;
                return { loopStart_, sourceOffset, captured - sourceOffset };
            }
        }
        return { takeStart_, 0, captured };
    }

    int getSamplesPerPeak() const noexcept { return samplesPerPeak_.load(std::memory_order_relaxed); }
    int getTotalPeaks() const noexcept
    {
        return static_cast<int>(juce::jmin<std::int64_t>(writeHead_.load(std::memory_order_acquire), kCapacity));
    }

    /** Snapshot peaks into caller-supplied vectors for safe UI rendering. */
    void snapshotPeaks(std::vector<float>& outMins,
                       std::vector<float>& outMaxs,
                       int& outCount) const noexcept
    {
        const auto head = writeHead_.load(std::memory_order_acquire);
        outCount = static_cast<int>(juce::jmin<std::int64_t>(head, kCapacity));
        outMins.resize(static_cast<size_t>(outCount));
        outMaxs.resize(static_cast<size_t>(outCount));
        for (int i = 0; i < outCount; ++i)
        {
            const auto slot = slotFor(head - outCount + i);
            outMins[static_cast<size_t>(i)] = mins_[slot].load(std::memory_order_relaxed);
            outMaxs[static_cast<size_t>(i)] = maxs_[slot].load(std::memory_order_relaxed);
        }
    }

    /** Render only the audible samples intersecting the current paint region. */
    bool draw(juce::Graphics& g, juce::Rectangle<float> region, juce::Colour colour,
              double pxPerSample, double samplesRecordedSoFar,
              std::int64_t sourceOffset = 0) const noexcept
    {
        if (region.getWidth() < 1.f || region.getHeight() < 2.f || !isInputAvailable()
            || !std::isfinite(pxPerSample) || pxPerSample <= 0.0
            || !std::isfinite(samplesRecordedSoFar) || samplesRecordedSoFar <= 0.0)
            return false;

        const auto dirty = region.getIntersection(g.getClipBounds().toFloat());
        if (dirty.isEmpty())
            return false;

        const auto captured = capturedSamples_.load(std::memory_order_acquire);
        const auto head = writeHead_.load(std::memory_order_acquire);
        const double sourceLimit = juce::jmin(static_cast<double>(captured),
                                               sourceOffset + samplesRecordedSoFar);
        const int firstColumn = static_cast<int>(std::floor(dirty.getX() - region.getX()));
        const int lastColumn = static_cast<int>(std::ceil(dirty.getRight() - region.getX()));
        const double firstSample = sourceOffset + firstColumn / pxPerSample;
        if (firstSample >= sourceLimit || head == 0)
            return false;

        // Search the retained blocks by their real sample endpoints. A
        // callback may be shorter than the device's negotiated block size.
        auto first = juce::jmax<std::int64_t>(0, head - kCapacity + 1);
        auto last = head;
        while (first < last)
        {
            const auto middle = first + (last - first) / 2;
            if (endSample(middle) <= firstSample)
                first = middle + 1;
            else
                last = middle;
        }

        juce::Graphics::ScopedSaveState state(g);
        if (!g.reduceClipRegion(dirty.getSmallestIntegerContainer()))
            return false;
        g.setColour(colour);
        const auto centre = region.getCentreY();
        const auto halfHeight = region.getHeight() * 0.44f;
        bool drewWaveform = false;
        auto blockStart = first > 0 ? endSample(first - 1) : 0;
        for (int column = firstColumn; column < lastColumn && first < head; ++column)
        {
            const double left = sourceOffset + column / pxPerSample;
            const double right = juce::jmin(sourceLimit, sourceOffset + (column + 1) / pxPerSample);
            if (right <= left)
                break;

            float lo = 1.0f, hi = -1.0f;
            while (first < head && blockStart < right)
            {
                const auto blockEnd = endSample(first);
                if (blockEnd <= 0 || blockEnd > captured)
                    break; // This slot was replaced after the snapshot.
                if (blockEnd > left && blockEnd > blockStart)
                {
                    const auto slot = slotFor(first);
                    const auto mn = mins_[slot].load(std::memory_order_acquire);
                    const auto mx = maxs_[slot].load(std::memory_order_acquire);
                    if (mn >= -1.0f && mx <= 1.0f && mn <= mx && endSample(first) == blockEnd)
                    {
                        lo = juce::jmin(lo, mn);
                        hi = juce::jmax(hi, mx);
                    }
                }
                if (blockEnd > right)
                    break;
                blockStart = blockEnd;
                ++first;
            }
            if (lo <= hi)
            {
                const float top = centre - hi * halfHeight;
                g.fillRect(region.getX() + static_cast<float>(column), top, 1.0f,
                           juce::jmax(1.0f, (hi - lo) * halfHeight));
                drewWaveform = true;
            }
        }
        return drewWaveform;
    }

private:
    static size_t slotFor(std::int64_t block) noexcept { return static_cast<size_t>(block % kCapacity); }

    std::int64_t endSample(std::int64_t block) const noexcept
    {
        return ends_[slotFor(block)].load(std::memory_order_acquire);
    }

    std::unique_ptr<std::atomic<float>[]> mins_;
    std::unique_ptr<std::atomic<float>[]> maxs_;
    std::unique_ptr<std::atomic<std::int64_t>[]> ends_;
    std::atomic<std::int64_t> writeHead_ { 0 };
    std::atomic<std::int64_t> capturedSamples_ { 0 };
    std::atomic<int> samplesPerPeak_ { 512 };
    std::atomic<bool> inputAvailable_ { false };
    std::atomic<bool> takeActive_ { false };
    std::int64_t takeStart_ = 0, loopStart_ = 0, loopEnd_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveRecordWaveformCore)
};

} // namespace DAW
