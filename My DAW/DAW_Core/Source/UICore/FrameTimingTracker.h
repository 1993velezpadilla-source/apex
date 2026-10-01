#pragma once

#include <JuceHeader.h>
#include <deque>
#include <algorithm>
#include <cmath>

namespace DAW {

//==============================================================================
/**
    Tracks frame timing for APEX presentation rate measurement.

    Records frame intervals and provides statistics (p50, p95, p99, worst)
    to verify that the presentation target is being met at any display rate
    (60, 90, 120, 144 Hz).

    Detects missed VBlank deadlines: a frame is considered "dropped" when
    its interval exceeds 1.5× the target interval (e.g., >25 ms for 60 Hz,
    >10.4 ms for 144 Hz).

    This is a message-thread utility — NOT for realtime audio use.
    It uses juce::Time for high-resolution timestamps.

    Usage:
        FrameTimingTracker tracker (displayRefreshRate);
        // Call markFrame() at the start of each presentation tick
        void onPresentationTick(double) {
            tracker.markFrame();
            // ... do work ...
        }
        // Query stats periodically
        auto stats = tracker.getStats();
        DBG("p95 frame time: " << stats.p95Ms << " ms, "
            << "dropped: " << stats.droppedFrames);
*/
class FrameTimingTracker
{
public:
    //==============================================================================
    struct Stats
    {
        double meanMs      = 0.0;
        double p50Ms       = 0.0;
        double p95Ms       = 0.0;
        double p99Ms       = 0.0;
        double worstMs     = 0.0;
        double minMs       = 0.0;
        double targetMs    = 0.0;
        double targetHz    = 0.0;
        int    numFrames   = 0;
        int    droppedFrames = 0;   // frames exceeding 1.5× target interval
        double dropRate    = 0.0;   // droppedFrames / numFrames
        double actualHz    = 0.0;   // 1000 / meanMs
    };

    //==============================================================================
    /** Create a tracker for the given display refresh rate.
        @param displayRefreshHz  display refresh rate in Hz (e.g., 60, 90, 120, 144)
        @param maxSamples        maximum number of samples to keep for statistics
    */
    explicit FrameTimingTracker (double displayRefreshHz = 60.0,
                                 int maxSamples = 600)
        : maxSamples_ (maxSamples)
    {
        setTargetRate (displayRefreshHz);
    }

    /** Update the target rate. Call when the display refresh changes. */
    void setTargetRate (double displayRefreshHz)
    {
        targetHz_ = juce::jlimit (1.0, 1000.0, displayRefreshHz);
        targetIntervalMs_ = 1000.0 / targetHz_;
    }

    /** Call this at the beginning of each presentation frame. */
    void markFrame()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (lastFrameTime_ > 0.0)
        {
            const double interval = now - lastFrameTime_;
            intervals_.push_back (interval);
            if (intervals_.size() > (size_t)maxSamples_)
                intervals_.pop_front();

            // A frame is "dropped" if it exceeds 1.5× the target interval.
            // This catches missed VBlank deadlines.
            if (interval > targetIntervalMs_ * 1.5)
                ++droppedFrames_;
        }
        lastFrameTime_ = now;
        ++totalFrames_;
    }

    /** Compute statistics from the recorded intervals. */
    Stats getStats() const
    {
        Stats s;
        s.numFrames = (int)intervals_.size();
        s.droppedFrames = droppedFrames_;
        s.targetMs = targetIntervalMs_;
        s.targetHz = targetHz_;

        if (intervals_.empty())
            return s;

        // Copy and sort for percentile calculation
        std::vector<double> sorted (intervals_.begin(), intervals_.end());
        std::sort (sorted.begin(), sorted.end());

        double sum = 0.0;
        for (auto v : sorted)
            sum += v;

        s.meanMs   = sum / sorted.size();
        s.actualHz = 1000.0 / s.meanMs;
        s.minMs    = sorted.front();
        s.worstMs  = sorted.back();
        s.p50Ms    = percentile (sorted, 0.50);
        s.p95Ms    = percentile (sorted, 0.95);
        s.p99Ms    = percentile (sorted, 0.99);
        s.dropRate = s.numFrames > 0 ? (double)s.droppedFrames / (double)s.numFrames : 0.0;

        return s;
    }

    /** Reset all recorded data. */
    void reset()
    {
        intervals_.clear();
        lastFrameTime_ = 0.0;
        totalFrames_ = 0;
        droppedFrames_ = 0;
    }

    /** Get the total number of frames recorded since last reset. */
    int getTotalFrames() const noexcept { return totalFrames_; }

    /** Get the number of dropped frames (exceeding 1.5× target). */
    int getDroppedFrames() const noexcept { return droppedFrames_; }

    /** Get the target interval in ms. */
    double getTargetIntervalMs() const noexcept { return targetIntervalMs_; }

    /** Get the target rate in Hz. */
    double getTargetHz() const noexcept { return targetHz_; }

private:
    //==============================================================================
    static double percentile (const std::vector<double>& sorted, double p)
    {
        if (sorted.empty())
            return 0.0;

        const double rank = p * (double)(sorted.size() - 1);
        const int lower = (int)std::floor (rank);
        const int upper = (int)std::ceil (rank);

        if (lower == upper)
            return sorted[lower];

        const double frac = rank - (double)lower;
        return sorted[lower] * (1.0 - frac) + sorted[upper] * frac;
    }

    //==============================================================================
    const int maxSamples_;
    std::deque<double> intervals_;
    double targetIntervalMs_ = 16.667;
    double targetHz_ = 60.0;
    double lastFrameTime_ = 0.0;
    int totalFrames_ = 0;
    int droppedFrames_ = 0;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameTimingTracker)
};

} // namespace DAW