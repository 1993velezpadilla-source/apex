#include "TapeStopAutomationCore.h"
#include <algorithm>
#include <cmath>

namespace APEX {
namespace TapeStop {

AutomationCore::AutomationCore()
{
}

void AutomationCore::prepare(double sr)
{
    sampleRate = sr;
}

size_t AutomationCore::addPoint(const AutomationPoint& p)
{
    points.push_back(p);
    resortPoints();
    notifyChanged();

    // Find the index of the newly inserted point (last one with matching time,
    // since stable_sort preserves insertion order for equal keys).
    auto it = std::lower_bound(points.begin(), points.end(), p);
    // Walk forward to the last point with matching timeSamples (our new one).
    while (it != points.end() && it->timeSamples == p.timeSamples)
        ++it;
    return (size_t)(it - points.begin()) - 1;
}

void AutomationCore::removePointAt(size_t sortedIndex)
{
    if (sortedIndex >= points.size())
        return;
    points.erase(points.begin() + (ptrdiff_t)sortedIndex);
    notifyChanged();
}

size_t AutomationCore::removePointsInRange(int64_t startSamples, int64_t endSamples)
{
    const size_t before = points.size();
    points.erase(
        std::remove_if(points.begin(), points.end(),
            [startSamples, endSamples](const AutomationPoint& pt) {
                return pt.timeSamples >= startSamples && pt.timeSamples < endSamples;
            }),
        points.end());
    const size_t removed = before - points.size();
    if (removed > 0)
        notifyChanged();
    return removed;
}

void AutomationCore::updatePointAt(size_t sortedIndex,
                                   float newValue,
                                   CurveType newCurveToNext)
{
    if (sortedIndex >= points.size())
        return;
    points[sortedIndex].value       = newValue;
    points[sortedIndex].curveToNext = newCurveToNext;
    notifyChanged();
}

size_t AutomationCore::movePointTime(size_t sortedIndex, int64_t newTimeSamples)
{
    if (sortedIndex >= points.size())
        return sortedIndex;
    points[sortedIndex].timeSamples = newTimeSamples;
    resortPoints();
    notifyChanged();

    // Find the new index: find the first point with this timeSamples.
    AutomationPoint dummy;
    dummy.timeSamples = newTimeSamples;
    auto it = std::lower_bound(points.begin(), points.end(), dummy);
    return (size_t)(it - points.begin());
}

void AutomationCore::clearAll()
{
    if (!points.empty())
    {
        points.clear();
        notifyChanged();
    }
}

float AutomationCore::evaluateAt(int64_t timeSamples) const noexcept
{
    if (points.empty())
        return 0.0f;

    if (timeSamples <= points.front().timeSamples)
        return points.front().value;

    if (timeSamples >= points.back().timeSamples)
        return points.back().value;

    // Find the first point AFTER timeSamples.
    AutomationPoint key;
    key.timeSamples = timeSamples;
    auto it = std::upper_bound(points.begin(), points.end(), key);

    // it points to p1; p0 is the one before it.
    const AutomationPoint& p1 = *it;
    const AutomationPoint& p0 = *(it - 1);

    const double span = (double)(p1.timeSamples - p0.timeSamples);
    const double t    = (span > 0.0) ? ((double)(timeSamples - p0.timeSamples) / span) : 0.0;

    const float curved = evaluateCurve(p0.curveToNext, (float)t);
    return p0.value + curved * (p1.value - p0.value);
}

float AutomationCore::evaluateCurve(CurveType c, float t) noexcept
{
    // Clamp t defensively to [0, 1].
    t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;

    switch (c)
    {
        case CurveType::Linear:
            return t;
        case CurveType::SCurve:
            return t * t * (3.0f - 2.0f * t);
        case CurveType::Exponential:
            return t * t;
        case CurveType::VinylBrake:
            return 1.0f - std::pow(1.0f - t, 2.5f);
    }
    return t;
}

juce::ValueTree AutomationCore::toValueTree() const
{
    juce::ValueTree root("TapeStopAutomation");
    root.setProperty("sampleRate", sampleRate, nullptr);
    root.setProperty("numPoints",  (int)points.size(), nullptr);

    for (const auto& pt : points)
    {
        juce::ValueTree child("Point");
        child.setProperty("time",  (juce::int64)pt.timeSamples,          nullptr);
        child.setProperty("value", pt.value,                              nullptr);
        child.setProperty("curve", (int)(uint8_t)pt.curveToNext,         nullptr);
        root.addChild(child, -1, nullptr);
    }
    return root;
}

void AutomationCore::fromValueTree(const juce::ValueTree& v)
{
    points.clear();

    if (!v.isValid())
        return;

    sampleRate = v.getProperty("sampleRate", 44100.0);

    const int numPoints = v.getProperty("numPoints", 0);
    points.reserve((size_t)numPoints);

    for (int i = 0; i < v.getNumChildren(); ++i)
    {
        juce::ValueTree child = v.getChild(i);
        if (!child.hasType("Point"))
            continue;

        AutomationPoint pt;
        pt.timeSamples = (int64_t)(juce::int64)child.getProperty("time",  (juce::int64)0);
        pt.value       = (float)child.getProperty("value", 0.0f);
        const int curveInt = child.getProperty("curve", (int)CurveType::SCurve);
        pt.curveToNext = (CurveType)(uint8_t)curveInt;
        points.push_back(pt);
    }

    resortPoints();
    notifyChanged();
}

void AutomationCore::resortPoints()
{
    std::stable_sort(points.begin(), points.end());
}

void AutomationCore::notifyChanged()
{
    listeners.call(&Listener::tapeStopAutomationChanged);
}

} // namespace TapeStop
} // namespace APEX
