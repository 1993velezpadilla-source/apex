#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
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

        for (int i = 0; i < kCapacity; ++i)
        {
            mins_[i].store(0.0f, std::memory_order_relaxed);
            maxs_[i].store(0.0f, std::memory_order_relaxed);
        }
    }

    // ── Audio thread ─────────────────────────────────────────────────────────

    /** Call once when recording starts (message thread, before audio runs). */
    void reset(int samplesPerBlock) noexcept
    {
        samplesPerPeak_.store(juce::jmax(1, samplesPerBlock), std::memory_order_relaxed);
        inputAvailable_.store(false, std::memory_order_release);
        takeActive_.store(false, std::memory_order_release);
        totalPeaks_.store(0, std::memory_order_release);
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
    void pushPeak(float minVal, float maxVal) noexcept
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

        const int idx = writeHead_.load(std::memory_order_relaxed) % kCapacity;
        mins_[idx].store(minVal, std::memory_order_relaxed);
        maxs_[idx].store(maxVal, std::memory_order_relaxed);
        writeHead_.fetch_add(1, std::memory_order_release);
        const int prev = totalPeaks_.load(std::memory_order_relaxed);
        if (prev < kCapacity)
            totalPeaks_.store(prev + 1, std::memory_order_release);
    }

    // ── UI thread ─────────────────────────────────────────────────────────────

    int  getSamplesPerPeak() const noexcept { return samplesPerPeak_.load(std::memory_order_acquire); }
    int  getTotalPeaks()     const noexcept { return totalPeaks_.load(std::memory_order_acquire); }

    /** Snapshot peaks into caller-supplied vectors for safe UI rendering. */
    void snapshotPeaks(std::vector<float>& outMins,
                       std::vector<float>& outMaxs,
                       int& outCount) const noexcept
    {
        const int total = totalPeaks_.load(std::memory_order_acquire);
        outCount = juce::jmin(total, kCapacity);
        outMins.resize((size_t) outCount);
        outMaxs.resize((size_t) outCount);

        // writeHead points one past the last written slot
        const int head = writeHead_.load(std::memory_order_acquire);
        // If we have a full buffer the oldest entry is at head % kCapacity
        const int startIdx = (outCount < kCapacity) ? 0 : (head % kCapacity);

        for (int i = 0; i < outCount; ++i)
        {
            const int src = (startIdx + i) % kCapacity;
            outMins[(size_t)i] = mins_[src].load(std::memory_order_relaxed);
            outMaxs[(size_t)i] = maxs_[src].load(std::memory_order_relaxed);
        }
    }

    /** Draw the live waveform into 'region' using the supplied Graphics context. */
    void draw(juce::Graphics& g,
              juce::Rectangle<float> region,
              juce::Colour colour,
              double pxPerSample,
              double samplesRecordedSoFar) const noexcept
    {
        if (region.getWidth() < 1.f || region.getHeight() < 2.f) return;
        if (!isInputAvailable()) return;
        if (! std::isfinite(pxPerSample) || pxPerSample <= 0.0) return;

        const int spp  = samplesPerPeak_.load(std::memory_order_acquire);
        const int total = juce::jmin(getTotalPeaks(), kCapacity);
        if (total == 0 || spp <= 0) return;

        const double pxPerPeak = pxPerSample * (double)spp;

        const float cy    = region.getCentreY();
        const float halfH = region.getHeight() * 0.5f * 0.88f; // 88% of half-height

        const int head = writeHead_.load(std::memory_order_acquire);
        const int startIdx = (total < kCapacity) ? 0 : (head % kCapacity);

        const float regionX = region.getX();

        g.saveState();
        g.reduceClipRegion(region.toNearestIntEdges());
        g.setColour(colour);

        if (pxPerPeak < 1.0)
        {
            const int pixelColumns = juce::jmin(
                juce::jmax(1, (int) std::ceil((double) total * pxPerPeak)),
                juce::jmax(1, (int) std::ceil((double) region.getWidth())));

            for (int column = 0; column < pixelColumns; ++column)
            {
                const int firstPeak = juce::jlimit(
                    0, total - 1, (int) std::floor((double) column / pxPerPeak));
                const int endPeak = juce::jlimit(
                    firstPeak + 1, total,
                    (int) std::ceil((double) (column + 1) / pxPerPeak));

                float aggregateMin = 1.0f;
                float aggregateMax = -1.0f;
                bool foundValidPeak = false;

                for (int i = firstPeak; i < endPeak; ++i)
                {
                    const int src = (startIdx + i) % kCapacity;
                    const float mn = mins_[src].load(std::memory_order_relaxed);
                    const float mx = maxs_[src].load(std::memory_order_relaxed);

                    if (! std::isfinite(mn) || ! std::isfinite(mx)
                        || mn < -1.0f || mx > 1.0f || mn > mx)
                        continue;

                    aggregateMin = juce::jmin(aggregateMin, mn);
                    aggregateMax = juce::jmax(aggregateMax, mx);
                    foundValidPeak = true;
                }

                if (! foundValidPeak)
                    continue;

                const float px = regionX + (float) column;
                const float barTop = cy - aggregateMax * halfH;
                const float barBottom = cy - aggregateMin * halfH;
                g.fillRect(px, barTop, 1.0f, juce::jmax(1.0f, barBottom - barTop));
            }
        }
        else
        {
            for (int i = 0; i < total; ++i)
            {
                const float px = regionX + (float)(i * pxPerPeak);
                if (px > region.getRight()) break;

                const int src = (startIdx + i) % kCapacity;
                const float mn = mins_[src].load(std::memory_order_relaxed);
                const float mx = maxs_[src].load(std::memory_order_relaxed);

                if (! std::isfinite(mn) || ! std::isfinite(mx) || mn < -1.0f || mx > 1.0f || mn > mx)
                    continue;

                const float barTop    = cy - mx * halfH;
                const float barBottom = cy - mn * halfH;
                const float barH      = juce::jmax(1.f, barBottom - barTop);
                const float barW      = juce::jmax(1.f, (float)pxPerPeak - 0.5f);

                g.fillRect(px, barTop, barW, barH);
            }
        }

        g.restoreState();
    }

private:
    std::unique_ptr<std::atomic<float>[]> mins_;
    std::unique_ptr<std::atomic<float>[]> maxs_;
    std::atomic<int>    writeHead_     { 0 };
    std::atomic<int>    totalPeaks_    { 0 };
    std::atomic<int>    samplesPerPeak_{ 512 };
    std::atomic<bool>   inputAvailable_{ false };
    std::atomic<bool>   takeActive_    { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveRecordWaveformCore)
};

} // namespace DAW
