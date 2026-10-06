#pragma once
#include "TapeStopTypes.h"
#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <memory>

namespace APEX {
namespace TapeStop {

// One AutomationCore per track. Owns the editable point list for that track's
// tape stop automation. UI thread reads and mutates the point list. Audio
// thread will, in Phase 2, only ever read a baked snapshot buffer published
// atomically — it must NEVER read the point vector directly.
//
// This class is the single source of truth for tape stop automation data on
// one track. It is NOT a JUCE Component, has NO paint code, and knows nothing
// about audio rendering.

class AutomationCore
{
public:
    AutomationCore();
    ~AutomationCore() = default;

    // ------------------------------------------------------------------
    // Lifecycle / configuration
    // ------------------------------------------------------------------

    // Called once at track construction. Sample rate is needed for converting
    // beat-based preset durations to sample counts. If sample rate changes
    // mid-session (rare), call this again — but DO NOT remap existing point
    // times; they stay in samples at the rate they were authored.
    void prepare(double sampleRate);

    // ------------------------------------------------------------------
    // Point editing (UI thread only)
    // ------------------------------------------------------------------

    // Add a point. Returns the index of the inserted point in the sorted
    // list. Multiple points at the exact same time are allowed; the later-
    // inserted one wins for evaluation purposes (use removePointAt to clean
    // up duplicates if needed).
    size_t addPoint(const AutomationPoint& p);

    // Remove the point at sortedIndex. No-op if out of range.
    void removePointAt(size_t sortedIndex);

    // Remove all points whose timeSamples lies in [startSamples, endSamples).
    // Returns the number removed. This is used by preset insertion when the
    // preset overlaps an existing region.
    size_t removePointsInRange(int64_t startSamples, int64_t endSamples);

    // Replace the value/curve of an existing point. No-op if out of range.
    void updatePointAt(size_t sortedIndex,
                       float newValue,
                       CurveType newCurveToNext);

    // Move an existing point in time. The point may move past its neighbours;
    // the list will be re-sorted internally. Returns the point's NEW index.
    size_t movePointTime(size_t sortedIndex, int64_t newTimeSamples);

    // Wipe all automation. Used by "Clear Tape Stop Automation" menu item.
    void clearAll();

    // ------------------------------------------------------------------
    // Read access (UI thread only — for the lane component to paint)
    // ------------------------------------------------------------------

    size_t getNumPoints() const noexcept { return points.size(); }
    const AutomationPoint& getPoint(size_t i) const { return points[i]; }
    const std::vector<AutomationPoint>& getAllPoints() const noexcept { return points; }

    // ------------------------------------------------------------------
    // Curve evaluation (callable from any thread, but in Phase 2 the audio
    // thread will NOT call this directly — it will read a baked buffer.
    // This method is provided for the UI to draw ghost curves and for the
    // offline render path to do sample-accurate lookup at low frequency.)
    // ------------------------------------------------------------------

    // Returns the tape-stop value at the given timeline sample position.
    // - If no points exist: returns 0.0f (normal playback).
    // - Before the first point: returns the first point's value.
    // - After the last point:   returns the last point's value.
    // - Between two points:     interpolates using the FIRST point's
    //                           curveToNext.
    float evaluateAt(int64_t timeSamples) const noexcept;

    // ------------------------------------------------------------------
    // Persistence
    // ------------------------------------------------------------------

    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& v);

    // ------------------------------------------------------------------
    // Listener for repaint notifications. Phase 3 (the lane component)
    // will register here. Phase 1 just defines the interface.
    // ------------------------------------------------------------------

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void tapeStopAutomationChanged() = 0;
    };

    void addListener(Listener* l)    { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

    // ------------------------------------------------------------------
    // Sample rate (read-only accessor, used by PresetCore)
    // ------------------------------------------------------------------

    double getSampleRate() const noexcept { return sampleRate; }

private:
    void resortPoints();
    void notifyChanged();

    static float evaluateCurve(CurveType c, float t) noexcept;

    std::vector<AutomationPoint> points;   // ALWAYS kept sorted by timeSamples
    double sampleRate = 44100.0;
    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationCore)
};

} // namespace TapeStop
} // namespace APEX
