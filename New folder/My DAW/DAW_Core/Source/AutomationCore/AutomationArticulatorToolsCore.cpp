#include "AutomationArticulatorToolsCore.h"
#include "AutomationClipRegionCore.h"
#include "AutomationClipClipboardCore.h"
#include <algorithm>

namespace DAW {

void AutomationArticulatorToolsCore::flipVertically(AutomationClipRegion& clip)
{
    if (clip.localPoints.empty())
        return;

    float minVal = clip.localPoints[0].value;
    float maxVal = clip.localPoints[0].value;

    for (const auto& point : clip.localPoints)
    {
        minVal = std::min(minVal, point.value);
        maxVal = std::max(maxVal, point.value);
    }

    const float center = (minVal + maxVal) * 0.5f;

    for (auto& point : clip.localPoints)
    {
        point.value = center - (point.value - center);
    }
}

void AutomationArticulatorToolsCore::scaleLevels(AutomationClipRegion& clip, float amount)
{
    if (clip.localPoints.empty())
        return;

    amount = std::max(0.01f, std::min(100.0f, amount));

    float minVal = clip.localPoints[0].value;
    float maxVal = clip.localPoints[0].value;

    for (const auto& point : clip.localPoints)
    {
        minVal = std::min(minVal, point.value);
        maxVal = std::max(maxVal, point.value);
    }

    const float center = (minVal + maxVal) * 0.5f;

    for (auto& point : clip.localPoints)
    {
        float relativeToCenter = point.value - center;
        relativeToCenter *= amount;
        point.value = center + relativeToCenter;
        point.value = std::max(0.0f, std::min(1.0f, point.value));
    }
}

void AutomationArticulatorToolsCore::normalizeLevels(AutomationClipRegion& clip)
{
    if (clip.localPoints.empty())
        return;

    float minVal = clip.localPoints[0].value;
    float maxVal = clip.localPoints[0].value;

    for (const auto& point : clip.localPoints)
    {
        minVal = std::min(minVal, point.value);
        maxVal = std::max(maxVal, point.value);
    }

    if (std::abs(maxVal - minVal) < 0.001f)
        return;

    for (auto& point : clip.localPoints)
    {
        point.value = (point.value - minVal) / (maxVal - minVal);
    }
}

void AutomationArticulatorToolsCore::resetLevels(AutomationClipRegion& clip, float defaultValue)
{
    for (auto& point : clip.localPoints)
    {
        point.value = defaultValue;
    }
}

void AutomationArticulatorToolsCore::copyState(const AutomationClipRegion& source, AutomationClipClipboard& clipboard)
{
    clipboard.clear();

    if (source.localPoints.empty() || source.lengthSamples <= 0)
        return;

    float minVal = 1e6f, maxVal = -1e6f;

    for (const auto& point : source.localPoints)
    {
        AutomationClipClipboard::Point clipPoint;
        clipPoint.normalizedTime = (float)point.timeSamples / (float)source.lengthSamples;
        clipPoint.normalizedValue = point.value;
        clipPoint.curveToNext = point.curveToNext;
        clipPoint.tensionToNext = point.tensionToNext;

        clipboard.points.push_back(clipPoint);

        minVal = std::min(minVal, point.value);
        maxVal = std::max(maxVal, point.value);
    }

    if (!clipboard.points.empty())
    {
        clipboard.minValue = minVal;
        clipboard.maxValue = maxVal;
    }
}

bool AutomationArticulatorToolsCore::pasteState(AutomationClipRegion& dest, const AutomationClipClipboard& clipboard)
{
    if (clipboard.isEmpty())
        return false;

    dest.localPoints.clear();

    for (const auto& clipPoint : clipboard.points)
    {
        AutomationPoint destPoint;
        destPoint.timeSamples = (int64_t)(clipPoint.normalizedTime * (float)dest.lengthSamples);
        destPoint.value = clipPoint.normalizedValue;
        destPoint.curveToNext = clipPoint.curveToNext;
        destPoint.tensionToNext = clipPoint.tensionToNext;

        dest.localPoints.push_back(destPoint);
    }

    // Ensure sorted
    std::sort(dest.localPoints.begin(), dest.localPoints.end(), [](const AutomationPoint& a, const AutomationPoint& b)
    {
        return a.timeSamples < b.timeSamples;
    });

    return !dest.localPoints.empty();
}

} // namespace DAW
