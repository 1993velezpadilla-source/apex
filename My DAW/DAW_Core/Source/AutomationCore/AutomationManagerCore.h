#pragma once
#include <JuceHeader.h>
#include "AutomationLaneCore.h"
#include "AutomationSnapshotPublisherCore.h"
#include "AutomationClipRegionCore.h"
#include "AutomationClipClipboardCore.h"
#include "AutomationArticulatorToolsCore.h"
#include <cstdint>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

namespace DAW {

class AutomationManagerCore
{
public:
    struct PluginParameterTarget
    {
        TrackID trackId;
        juce::String pluginInstanceId;
        int pluginSlotIndex = -1;
        juce::String parameterId;
    };

    struct PluginSlotMixTarget
    {
        TrackID trackId;
        int pluginSlotIndex = -1;
    };

    struct SegmentShapeClipboard
    {
        bool valid = false;
        AutomationCurveType curve = AutomationCurveType::Linear;
        float tension = 0.0f;
        float startValue = 0.0f;
        float endValue = 1.0f;
    };

    static juce::String makePluginParameterId(const PluginParameterTarget& target)
    {
        return "plugin." + juce::String(target.pluginSlotIndex) + "." + target.pluginInstanceId + "." + target.parameterId;
    }

    template <typename Target>
    static juce::String makePluginParameterId(const Target& target)
    {
        return "plugin." + juce::String(target.pluginSlotIndex) + "." + target.pluginInstanceId + "." + target.parameterId;
    }

    static juce::String makePluginSlotMixId(int pluginSlotIndex)
    {
        return "plugin." + juce::String(pluginSlotIndex) + ".__slot_mix__";
    }

    /** Keep legacy parameter, wet/dry and clip-region lanes with the
        processor instance when insert positions change. Message thread. */
    void remapPluginSlots(const TrackID& trackId, const std::vector<int>& oldToNew)
    {
        const auto remap = [&](const juce::String& parameterId)
        {
            for (size_t oldIndex = 0; oldIndex < oldToNew.size(); ++oldIndex)
            {
                const auto prefix = "plugin." + juce::String((int)oldIndex) + ".";
                if (parameterId.startsWith(prefix))
                    return "plugin." + juce::String(oldToNew[oldIndex]) + "."
                        + parameterId.substring(prefix.length());
            }
            return parameterId;
        };
        for (auto& lane : lanes_)
            if (lane.trackId == trackId)
                lane.setParameterId(remap(lane.getParameterId()));
        for (auto& region : clipRegions_)
            if (region.trackId == trackId)
                region.parameterId = remap(region.parameterId);
        publishSnapshot();
    }

    AutomationManagerCore()
    {
        publishSnapshot();
    }

    AutomationLaneCore& getOrCreateLane(const TrackID& trackId, const juce::String& parameterId)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
            return *lane;

        lanes_.emplace_back(trackId, parameterId);
        return lanes_.back();
    }

    void addPoint(const TrackID& trackId, const juce::String& parameterId, int64_t timeSamples, float value)
    {
        getOrCreateLane(trackId, parameterId).addPoint(timeSamples, AutomationLaneCore::clampValueForParameter(parameterId, value));
        publishSnapshot();
    }

    /** Insert a full point (time, value, curve, tension) preserving its shape.
     *  Used by Undo/Redo so a restored point keeps its exact curve/tension. */
    void addPoint(const TrackID& trackId, const juce::String& parameterId, const AutomationPoint& point)
    {
        getOrCreateLane(trackId, parameterId).addPoint(point);
        publishSnapshot();
    }

    void addOrReplacePoint(const TrackID& trackId, const juce::String& parameterId, int64_t timeSamples, float value, bool deferPublish = false)
    {
        auto& lane = getOrCreateLane(trackId, parameterId);
        value = AutomationLaneCore::clampValueForParameter(parameterId, value);
        for (auto& point : lane.points)
        {
            if (point.timeSamples == timeSamples)
            {
                point.value = value;
                if (!deferPublish)
                    publishSnapshot();
                return;
            }
        }

        lane.addPoint(timeSamples, value);
        if (!deferPublish)
            publishSnapshot();
    }

    void movePoint(const TrackID& trackId, const juce::String& parameterId, int pointIndex, int64_t timeSamples, float value)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            lane->setPoint(pointIndex, timeSamples, AutomationLaneCore::clampValueForParameter(parameterId, value));
            publishSnapshot();
        }
    }

    void removePoint(const TrackID& trackId, const juce::String& parameterId, int pointIndex)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            lane->removeAutomationPoint(pointIndex);
            publishSnapshot();
        }
    }

    bool resetPointValue(const TrackID& trackId, const juce::String& parameterId, int pointIndex)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            if (pointIndex < 0 || pointIndex >= (int)lane->points.size())
                return false;

            lane->points[(size_t)pointIndex].value = AutomationLaneCore::clampValueForParameter(parameterId, lane->getDefaultValue());
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool copyPointValue(const TrackID& trackId, const juce::String& parameterId, int pointIndex)
    {
        if (const auto* lane = findLane(trackId, parameterId))
        {
            if (pointIndex < 0 || pointIndex >= (int)lane->points.size())
                return false;

            copiedPointValue_ = lane->points[(size_t)pointIndex].value;
            hasCopiedPointValue_ = true;
            return true;
        }

        return false;
    }

    bool canPastePointValue() const noexcept { return hasCopiedPointValue_; }

    bool pastePointValue(const TrackID& trackId, const juce::String& parameterId, int pointIndex)
    {
        if (!hasCopiedPointValue_)
            return false;

        return setPointExactValue(trackId, parameterId, pointIndex, copiedPointValue_);
    }

    bool setPointExactValue(const TrackID& trackId, const juce::String& parameterId, int pointIndex, float value)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            if (pointIndex < 0 || pointIndex >= (int)lane->points.size())
                return false;

            lane->points[(size_t)pointIndex].value = AutomationLaneCore::clampValueForParameter(parameterId, value);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool copySegmentShape(const TrackID& trackId, const juce::String& parameterId, int segmentIndex)
    {
        if (const auto* lane = findLane(trackId, parameterId))
        {
            if (segmentIndex < 0 || segmentIndex + 1 >= (int)lane->points.size())
                return false;

            const auto& a = lane->points[(size_t)segmentIndex];
            const auto& b = lane->points[(size_t)segmentIndex + 1];
            segmentClipboard_.valid = true;
            segmentClipboard_.curve = a.curveToNext;
            segmentClipboard_.tension = a.tensionToNext;
            segmentClipboard_.startValue = a.value;
            segmentClipboard_.endValue = b.value;
            return true;
        }

        return false;
    }

    bool canPasteSegmentShape() const noexcept { return segmentClipboard_.valid; }

    bool pasteSegmentShape(const TrackID& trackId, const juce::String& parameterId, int segmentIndex, bool pasteValues)
    {
        if (!segmentClipboard_.valid)
            return false;

        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            if (segmentIndex < 0 || segmentIndex + 1 >= (int)lane->points.size())
                return false;

            auto& a = lane->points[(size_t)segmentIndex];
            auto& b = lane->points[(size_t)segmentIndex + 1];
            a.curveToNext = segmentClipboard_.curve;
            a.tensionToNext = segmentClipboard_.tension;

            if (pasteValues)
            {
                a.value = AutomationLaneCore::clampValueForParameter(parameterId, segmentClipboard_.startValue);
                b.value = AutomationLaneCore::clampValueForParameter(parameterId, segmentClipboard_.endValue);
            }

            publishSnapshot();
            return true;
        }

        return false;
    }

    bool setPointExactTime(const TrackID& trackId, const juce::String& parameterId, int pointIndex, int64_t timeSamples)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            if (pointIndex < 0 || pointIndex >= (int)lane->points.size())
                return false;

            // Route through the lane's collision-aware setPointTime so the
            // moved point wins deterministically if another point already
            // occupies the new timestamp.
            lane->setPointTime(pointIndex, timeSamples);
            publishSnapshot();
            return true;
        }

        return false;
    }

    void setPointCurveToNext(const TrackID& trackId, const juce::String& parameterId, int pointIndex, AutomationCurveType curve)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            lane->setCurveToNext(pointIndex, curve);
            publishSnapshot();
        }
    }

    bool resetSegmentTension(const TrackID& trackId, const juce::String& parameterId, int segmentIndex)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            if (segmentIndex < 0 || segmentIndex + 1 >= (int)lane->points.size())
                return false;

            lane->setTensionToNext(segmentIndex, 0.0f);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool setSegmentCurve(const TrackID& trackId, const juce::String& parameterId, int segmentIndex, AutomationCurveType curve)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            if (segmentIndex < 0 || segmentIndex + 1 >= (int)lane->points.size())
                return false;

            lane->setCurveToNext(segmentIndex, curve);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool deletePointsInSegment(const TrackID& trackId, const juce::String& parameterId, int64_t startSample, int64_t endSample)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            if (startSample > endSample)
                std::swap(startSample, endSample);

            const auto oldSize = lane->points.size();
            lane->points.erase(std::remove_if(lane->points.begin(), lane->points.end(),
                [startSample, endSample](const AutomationPoint& point)
                {
                    return point.timeSamples > startSample && point.timeSamples < endSample;
                }), lane->points.end());

            if (lane->points.size() != oldSize)
            {
                lane->sortAndResolveDuplicates();
                publishSnapshot();
                return true;
            }
        }

        return false;
    }

    void setPointTensionToNext(const TrackID& trackId, const juce::String& parameterId, int pointIndex, float tension)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            lane->setTensionToNext(pointIndex, tension);
            publishSnapshot();
        }
    }

    int insertPointPreservingLevel(const TrackID& trackId, const juce::String& parameterId, int64_t timeSamples)
    {
        if (auto* lane = findEditableLane(trackId, parameterId))
        {
            const int index = lane->insertPointPreservingLevel(timeSamples);
            publishSnapshot();
            return index;
        }

        return -1;
    }

    void setLaneEnabled(const TrackID& trackId, const juce::String& parameterId, bool enabled)
    {
        auto& lane = getOrCreateLane(trackId, parameterId);
        lane.setEnabled(enabled);
        publishSnapshot();
    }

    void setLaneVisible(const TrackID& trackId, const juce::String& parameterId, bool visible)
    {
        auto& lane = getOrCreateLane(trackId, parameterId);
        lane.setVisible(visible);
        publishSnapshot();
    }

    void showLane(const TrackID& trackId, const juce::String& parameterId)
    {
        setLaneVisible(trackId, parameterId, true);
    }

    void hideLane(const TrackID& trackId, const juce::String& parameterId)
    {
        setLaneVisible(trackId, parameterId, false);
    }

    const AutomationLaneCore* findLane(const TrackID& trackId, const juce::String& parameterId) const noexcept
    {
        for (const auto& lane : lanes_)
            if (lane.trackId == trackId && lane.parameterId == parameterId)
                return &lane;

        return nullptr;
    }

    void clearLane(const TrackID& trackId, const juce::String& parameterId)
    {
        clipRegions_.erase(std::remove_if(clipRegions_.begin(), clipRegions_.end(),
            [&trackId, &parameterId](const AutomationClipRegionCore& region)
            {
                return region.trackId == trackId && region.parameterId == parameterId;
            }), clipRegions_.end());

        for (auto it = lanes_.begin(); it != lanes_.end(); ++it)
        {
            if (it->trackId == trackId && it->parameterId == parameterId)
            {
                lanes_.erase(it);
                publishSnapshot();
                return;
            }
        }
    }

    void clearAll()
    {
        lanes_.clear();
        clipRegions_.clear();
        publishSnapshot();
    }

    /** Scale every sample-domain automation position when the project timeline
        is reconciled to a different, proven audio-device sample rate.
        Message thread only (project load / undo). */
    void scaleSamplePositions(double factor)
    {
        if (!std::isfinite(factor) || factor <= 0.0 || std::abs(factor - 1.0) < 1.0e-12)
            return;

        const auto scale = [factor](int64_t position)
        {
            return (int64_t)std::llround((double)position * factor);
        };

        for (auto& lane : lanes_)
        {
            for (auto& point : lane.points)
                point.timeSamples = scale(point.timeSamples);
            lane.sortAndResolveDuplicates();
        }

        for (auto& region : clipRegions_)
        {
            region.startSample = scale(region.startSample);
            region.lengthSamples = juce::jmax<int64_t>(0, scale(region.lengthSamples));
            for (auto& point : region.localPoints)
                point.timeSamples = juce::jlimit<int64_t>(0, region.lengthSamples, scale(point.timeSamples));

            std::stable_sort(region.localPoints.begin(), region.localPoints.end(),
                [](const AutomationPoint& a, const AutomationPoint& b)
                {
                    return a.timeSamples < b.timeSamples;
                });
            if (region.localPoints.size() > 1)
            {
                size_t write = 0;
                for (size_t read = 1; read < region.localPoints.size(); ++read)
                {
                    if (region.localPoints[read].timeSamples == region.localPoints[write].timeSamples)
                        region.localPoints[write] = region.localPoints[read];
                    else
                        region.localPoints[++write] = region.localPoints[read];
                }
                region.localPoints.resize(write + 1);
            }
        }

        publishSnapshot();
    }

    void publishSnapshot()
    {
        syncAllClipRegionsToLanes();
        publisher_.publish(*this);
    }

    AutomationSnapshotPublisherCore& getSnapshotPublisher() noexcept { return publisher_; }
    const AutomationSnapshotPublisherCore& getSnapshotPublisher() const noexcept { return publisher_; }

    const std::vector<AutomationLaneCore>& getLanes() const noexcept { return lanes_; }
    const std::vector<AutomationClipRegionCore>& getClipRegions() const noexcept { return clipRegions_; }

    AutomationClipRegionCore* findClipRegion(const juce::Uuid& regionId) noexcept
    {
        for (auto& region : clipRegions_)
            if (region.regionId == regionId)
                return &region;

        return nullptr;
    }

    const AutomationClipRegionCore* findClipRegion(const juce::Uuid& regionId) const noexcept
    {
        for (const auto& region : clipRegions_)
            if (region.regionId == regionId)
                return &region;

        return nullptr;
    }

    AutomationClipRegionCore& addClipRegion(AutomationClipRegionCore region)
    {
        if (region.name.isEmpty())
            region.name = region.parameterId;

        clipRegions_.push_back(std::move(region));
        syncClipRegionToLane(clipRegions_.back());
        publishSnapshot();
        return clipRegions_.back();
    }

    bool duplicateClipRegion(const juce::Uuid& regionId, int64_t newStartSample)
    {
        for (const auto& region : clipRegions_)
        {
            if (region.regionId == regionId)
            {
                auto duplicate = region;
                duplicate.regionId = juce::Uuid();
                duplicate.startSample = newStartSample;
                duplicate.selected = false;
                clipRegions_.push_back(std::move(duplicate));
                syncClipRegionToLane(clipRegions_.back());
                publishSnapshot();
                return true;
            }
        }

        return false;
    }

    bool moveClipRegion(const juce::Uuid& regionId, int64_t newStartSample)
    {
        if (auto* region = findClipRegion(regionId))
        {
            clearLanePointsInRegionRange(*region);
            region->startSample = juce::jmax<int64_t>(0, newStartSample);
            syncClipRegionToLane(*region);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool resizeClipRegion(const juce::Uuid& regionId, int64_t newLengthSamples)
    {
        if (auto* region = findClipRegion(regionId))
        {
            clearLanePointsInRegionRange(*region);
            region->lengthSamples = juce::jmax<int64_t>(1, newLengthSamples);
            for (auto& point : region->localPoints)
                point.timeSamples = juce::jlimit<int64_t>(0, region->lengthSamples, point.timeSamples);
            syncClipRegionToLane(*region);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool setClipRegionMuted(const juce::Uuid& regionId, bool muted)
    {
        if (auto* region = findClipRegion(regionId))
        {
            clearLanePointsInRegionRange(*region);
            region->muted = muted;
            syncClipRegionToLane(*region);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool renameClipRegion(const juce::Uuid& regionId, const juce::String& name)
    {
        if (auto* region = findClipRegion(regionId))
        {
            region->name = name;
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool setClipRegionColor(const juce::Uuid& regionId, juce::Colour color)
    {
        if (auto* region = findClipRegion(regionId))
        {
            region->color = color;
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool deleteClipRegion(const juce::Uuid& regionId)
    {
        for (auto it = clipRegions_.begin(); it != clipRegions_.end(); ++it)
        {
            if (it->regionId == regionId)
            {
                clearLanePointsInRegionRange(*it);
                clipRegions_.erase(it);
                publishSnapshot();
                return true;
            }
        }

        return false;
    }

    bool copyClipRegionState(const juce::Uuid& regionId)
    {
        if (const auto* region = findClipRegion(regionId))
        {
            AutomationArticulatorToolsCore::copyState(*region, clipClipboard_);
            return true;
        }

        return false;
    }

    bool pasteClipRegionState(const juce::Uuid& regionId)
    {
        if (auto* region = findClipRegion(regionId))
        {
            clearLanePointsInRegionRange(*region);
            const bool changed = AutomationArticulatorToolsCore::pasteState(*region, clipClipboard_);
            if (changed)
                syncClipRegionToLane(*region);
            if (changed)
                publishSnapshot();
            return changed;
        }

        return false;
    }

    bool flipClipRegionVertically(const juce::Uuid& regionId)
    {
        if (auto* region = findClipRegion(regionId))
        {
            clearLanePointsInRegionRange(*region);
            AutomationArticulatorToolsCore::flipVertically(*region);
            syncClipRegionToLane(*region);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool scaleClipRegionLevels(const juce::Uuid& regionId, float amount)
    {
        if (auto* region = findClipRegion(regionId))
        {
            clearLanePointsInRegionRange(*region);
            AutomationArticulatorToolsCore::scaleLevels(*region, amount);
            syncClipRegionToLane(*region);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool normalizeClipRegionLevels(const juce::Uuid& regionId)
    {
        if (auto* region = findClipRegion(regionId))
        {
            clearLanePointsInRegionRange(*region);
            AutomationArticulatorToolsCore::normalizeLevels(*region);
            syncClipRegionToLane(*region);
            publishSnapshot();
            return true;
        }

        return false;
    }

    bool resetClipRegionLevels(const juce::Uuid& regionId, float defaultValue = 0.0f)
    {
        if (auto* region = findClipRegion(regionId))
        {
            clearLanePointsInRegionRange(*region);
            AutomationArticulatorToolsCore::resetLevels(*region, defaultValue);
            syncClipRegionToLane(*region);
            publishSnapshot();
            return true;
        }

        return false;
    }

    AutomationClipClipboardCore& getClipClipboard() noexcept { return clipClipboard_; }
    const AutomationClipClipboardCore& getClipClipboard() const noexcept { return clipClipboard_; }

    juce::ValueTree getState() const
    {
        return getStateFilteredByClipExistence({});
    }

    juce::ValueTree getStateFilteredByClipExistence(const std::function<bool(const juce::String&)>& clipExists) const
    {
        juce::ValueTree automation("Automation");
        automation.setProperty("version", 2, nullptr);

        for (const auto& lane : lanes_)
        {
            if (!AutomationLaneCore::referencesExistingClip(lane.parameterId, clipExists))
                continue;

            juce::ValueTree laneTree("Lane");
            laneTree.setProperty("trackId", lane.trackId, nullptr);
            laneTree.setProperty("parameterId", lane.parameterId, nullptr);
            laneTree.setProperty("enabled", lane.enabled, nullptr);

            for (const auto& point : lane.points)
            {
                juce::ValueTree pointTree("Point");
                pointTree.setProperty("timeSamples", (juce::int64)point.timeSamples, nullptr);
                pointTree.setProperty("value", point.value, nullptr);
                pointTree.setProperty("curveToNext", (int)point.curveToNext, nullptr);
                pointTree.setProperty("curveToNextName", automationCurveTypeToString(point.curveToNext), nullptr);
                pointTree.setProperty("tensionToNext", point.tensionToNext, nullptr);
                laneTree.addChild(pointTree, -1, nullptr);
            }

            automation.addChild(laneTree, -1, nullptr);
        }

        juce::ValueTree regions("AutomationClipRegions");
        for (const auto& region : clipRegions_)
        {
            if (!AutomationLaneCore::referencesExistingClip(region.parameterId, clipExists))
                continue;

            regions.addChild(region.getState(), -1, nullptr);
        }
        automation.addChild(regions, -1, nullptr);

        return automation;
    }

    void restoreState(const juce::ValueTree& automation,
                      const std::function<bool(const juce::String&)>& clipExists = {})
    {
        lanes_.clear();
        clipRegions_.clear();

        if (automation.isValid())
        {
            const int version = (int)automation.getProperty("version", 1);
            for (int i = 0; i < automation.getNumChildren(); ++i)
            {
                auto laneTree = automation.getChild(i);
                if (laneTree.hasType("AutomationClipRegions"))
                {
                    for (int r = 0; r < laneTree.getNumChildren(); ++r)
                    {
                        auto region = AutomationClipRegionCore::fromState(laneTree.getChild(r));
                        if (region.isValid() && AutomationLaneCore::referencesExistingClip(region.parameterId, clipExists))
                            clipRegions_.push_back(std::move(region));
                    }
                    continue;
                }

                if (!laneTree.hasType("Lane"))
                    continue;

                const auto parameterId = laneTree.getProperty("parameterId", {}).toString();
                if (!AutomationLaneCore::referencesExistingClip(parameterId, clipExists))
                    continue;

                AutomationLaneCore lane(
                    laneTree.getProperty("trackId", {}).toString(),
                    parameterId);
                lane.enabled = (bool)laneTree.getProperty("enabled", true);

                for (int p = 0; p < laneTree.getNumChildren(); ++p)
                {
                    auto pointTree = laneTree.getChild(p);
                    if (!pointTree.hasType("Point"))
                        continue;

                    const auto curve = pointTree.hasProperty("curveToNextName")
                        ? automationCurveTypeFromString(pointTree.getProperty("curveToNextName").toString())
                        : (version >= 2
                            ? automationCurveTypeFromStoredInt((int)pointTree.getProperty("curveToNext", 0))
                            : legacyAutomationCurveTypeFromStoredInt((int)pointTree.getProperty("curveToNext", 0)));

                    lane.points.push_back({
                        (int64_t)(juce::int64)pointTree.getProperty("timeSamples", (juce::int64)0),
                        (float)pointTree.getProperty("value", 1.0f),
                        curve,
                        juce::jlimit(-1.0f, 1.0f, (float)pointTree.getProperty("tensionToNext", 0.0f))
                    });
                }

                lane.sortAndResolveDuplicates();
                lanes_.push_back(std::move(lane));
            }
        }

        publishSnapshot();
    }

    bool removeOrphanedClipAutomation(const std::function<bool(const juce::String&)>& clipExists)
    {
        if (!clipExists)
            return false;

        const auto laneEnd = std::remove_if(lanes_.begin(), lanes_.end(),
            [&clipExists](const AutomationLaneCore& lane)
            {
                return !AutomationLaneCore::referencesExistingClip(lane.parameterId, clipExists);
            });

        const bool removedLanes = laneEnd != lanes_.end();
        if (removedLanes)
            lanes_.erase(laneEnd, lanes_.end());

        const auto regionEnd = std::remove_if(clipRegions_.begin(), clipRegions_.end(),
            [&clipExists](const AutomationClipRegionCore& region)
            {
                return !AutomationLaneCore::referencesExistingClip(region.parameterId, clipExists);
            });

        const bool removedRegions = regionEnd != clipRegions_.end();
        if (removedRegions)
            clipRegions_.erase(regionEnd, clipRegions_.end());

        if (removedLanes || removedRegions)
            publishSnapshot();

        return removedLanes || removedRegions;
    }

    /** Monotonically increasing version counter incremented on every
     *  publishSnapshot(). UI timers use this to detect whether automation
     *  data has changed since the last paint cycle — zero change = zero repaint.
     *  Thread-safe via std::atomic.  Acquire semantics ensure the caller
     *  sees all prior stores to lanes / clip regions.
     *
     *  HONESTY NOTE: This is a global version gate.  Any change to any lane
     *  invalidates ALL curve paths and triggers a full repaint.  Per-lane
     *  dirty invalidation is NOT implemented. */
    uint64_t getSnapshotVersion() const noexcept
    {
        return nextVersion_.load(std::memory_order_acquire);
    }

    /**
     * Capture only the clip-scoped automation owned by one stable clip ID.
     * The returned ValueTree is detached from the live vectors and is safe to
     * retain as an immutable command payload.  Track/global automation is not
     * included, so restoring a clip edit cannot overwrite unrelated edits.
     */
    juce::ValueTree captureClipAutomationState(const juce::String& clipId,
                                               const std::vector<juce::String>& pluginInstanceIds = {}) const
    {
        juce::ValueTree snapshot("ClipAutomationSnapshot");
        snapshot.setProperty("clipId", clipId, nullptr);

        const auto belongsToClip = [&clipId, &pluginInstanceIds](const juce::String& parameterId)
        {
            if (AutomationLaneCore::tryExtractClipIdFromParameterId(parameterId) == clipId)
                return true;

            for (const auto& instanceId : pluginInstanceIds)
            {
                if (instanceId.isNotEmpty()
                    && parameterId.contains("." + instanceId + "."))
                    return true;
            }
            return false;
        };

        for (const auto& lane : lanes_)
        {
            if (!belongsToClip(lane.parameterId))
                continue;

            auto laneState = lane.getState();
            laneState.setProperty("trackId", lane.trackId, nullptr);
            laneState.setProperty("parameterId", lane.parameterId, nullptr);
            laneState.setProperty("enabled", lane.enabled, nullptr);
            snapshot.addChild(laneState, -1, nullptr);
        }

        for (const auto& region : clipRegions_)
        {
            if (belongsToClip(region.parameterId))
                snapshot.addChild(region.getState(), -1, nullptr);
        }

        return snapshot.createCopy();
    }

    /** Remove clip lanes and clip-region records for the supplied IDs. */
    bool removeClipAutomationForIds(const std::vector<juce::String>& clipIds,
                                    bool publish = true)
    {
        const auto containsId = [&clipIds](const juce::String& parameterId)
        {
            const auto id = AutomationLaneCore::tryExtractClipIdFromParameterId(parameterId);
            return std::find(clipIds.begin(), clipIds.end(), id) != clipIds.end();
        };

        const auto laneEnd = std::remove_if(lanes_.begin(), lanes_.end(),
            [&containsId](const AutomationLaneCore& lane)
            {
                return containsId(lane.parameterId);
            });
        const bool removedLanes = laneEnd != lanes_.end();
        if (removedLanes)
            lanes_.erase(laneEnd, lanes_.end());

        const auto regionEnd = std::remove_if(clipRegions_.begin(), clipRegions_.end(),
            [&containsId](const AutomationClipRegionCore& region)
            {
                return containsId(region.parameterId);
            });
        const bool removedRegions = regionEnd != clipRegions_.end();
        if (removedRegions)
            clipRegions_.erase(regionEnd, clipRegions_.end());

        if (publish && (removedLanes || removedRegions))
            publishSnapshot();

        return removedLanes || removedRegions;
    }

    /**
     * Restore a detached clip-scoped snapshot after clearing the supplied
     * current IDs.  This is the Split Undo path: it restores the historical
     * payload and never reads mutable fragment state.
     */
    bool restoreClipAutomationState(const juce::ValueTree& snapshot,
                                    const std::vector<juce::String>& clearClipIds)
    {
        if (!snapshot.isValid() || !snapshot.hasType("ClipAutomationSnapshot"))
            return false;

        removeClipAutomationForIds(clearClipIds, false);

        for (int i = 0; i < snapshot.getNumChildren(); ++i)
        {
            const auto child = snapshot.getChild(i);
            if (child.hasType("AutomationLane"))
            {
                AutomationLaneCore lane(child.getProperty("trackId", {}).toString(),
                                         child.getProperty("parameterId", {}).toString());
                lane.restoreState(child);
                lane.trackId = child.getProperty("trackId", {}).toString();
                lane.parameterId = child.getProperty("parameterId", {}).toString();
                lane.setParameterId(lane.parameterId);
                lane.enabled = (bool) child.getProperty("enabled", true);
                lanes_.push_back(std::move(lane));
            }
            else if (child.hasType("AutomationClipRegion"))
            {
                auto region = AutomationClipRegionCore::fromState(child);
                if (region.isValid())
                    clipRegions_.push_back(std::move(region));
            }
        }

        publishSnapshot();
        return true;
    }

    /**
     * Partition one captured clip automation payload at an absolute engine
     * sample boundary.  Existing point interpolation/tension fields are
     * copied unchanged; a point exactly on the boundary belongs to both
     * fragments, matching the inclusive APEX region convention.
     */
    bool applySplitClipAutomation(const juce::ValueTree& snapshot,
                                  const juce::String& originalClipId,
                                  const juce::String& rightClipId,
                                  int64_t splitSample,
                                  const std::vector<std::pair<juce::String, juce::String>>& pluginInstanceMap = {})
    {
        if (!snapshot.isValid() || !snapshot.hasType("ClipAutomationSnapshot"))
            return false;

        const auto remapParameter = [&originalClipId, &rightClipId, &pluginInstanceMap](const juce::String& parameterId,
                                                                                          bool rightFragment)
        {
            if (!rightFragment)
                return parameterId;

            const auto prefix = juce::String("clip.") + originalClipId;
            if (parameterId.startsWith(prefix))
                return juce::String("clip.") + rightClipId
                    + parameterId.substring(prefix.length());

            for (const auto& [sourceInstanceId, targetInstanceId] : pluginInstanceMap)
            {
                if (sourceInstanceId.isEmpty() || targetInstanceId.isEmpty())
                    continue;

                const auto token = juce::String(".") + sourceInstanceId + ".";
                if (parameterId.contains(token))
                    return parameterId.replaceFirstOccurrenceOf(token,
                                                                juce::String(".") + targetInstanceId + ".");
            }

            return parameterId;
        };

        std::vector<AutomationLaneCore> leftLanes;
        std::vector<AutomationLaneCore> rightLanes;
        std::vector<AutomationClipRegionCore> leftRegions;
        std::vector<AutomationClipRegionCore> rightRegions;

        for (int i = 0; i < snapshot.getNumChildren(); ++i)
        {
            const auto child = snapshot.getChild(i);
            if (child.hasType("AutomationLane"))
            {
                const auto sourceParameter = child.getProperty("parameterId", {}).toString();
                AutomationLaneCore source(child.getProperty("trackId", {}).toString(), sourceParameter);
                source.restoreState(child);
                source.trackId = child.getProperty("trackId", {}).toString();
                source.parameterId = sourceParameter;
                source.setParameterId(sourceParameter);
                source.enabled = (bool) child.getProperty("enabled", true);

                auto makeFragment = [&](bool rightFragment,
                                        std::vector<AutomationPoint> points)
                {
                    AutomationLaneCore fragment(source.trackId,
                        remapParameter(sourceParameter, rightFragment));
                    fragment.points = std::move(points);
                    fragment.enabled = source.enabled;
                    fragment.setParameterId(fragment.parameterId);
                    fragment.setParameterName(source.getParameterName());
                    fragment.setDefaultValue(source.getDefaultValue());
                    fragment.setVisible(source.isVisible());
                    return fragment;
                };

                std::vector<AutomationPoint> leftPoints;
                std::vector<AutomationPoint> rightPoints;
                leftPoints.reserve(source.points.size());
                rightPoints.reserve(source.points.size());
                for (const auto& point : source.points)
                {
                    if (point.timeSamples <= splitSample)
                        leftPoints.push_back(point);
                    if (point.timeSamples >= splitSample)
                        rightPoints.push_back(point);
                }

                leftLanes.push_back(makeFragment(false, std::move(leftPoints)));
                rightLanes.push_back(makeFragment(true, std::move(rightPoints)));
            }
            else if (child.hasType("AutomationClipRegion"))
            {
                const auto source = AutomationClipRegionCore::fromState(child);
                if (!source.isValid())
                    continue;

                const auto sourceStart = source.startSample;
                const auto sourceEnd = source.endSample();
                const auto parameter = source.parameterId;
                const auto absolutePoints = source.toAbsolutePoints();

                auto makeRegion = [&](int64_t start, int64_t end,
                                      bool rightFragment,
                                      juce::Uuid regionId)
                {
                    auto fragment = source;
                    fragment.regionId = std::move(regionId);
                    fragment.parameterId = remapParameter(parameter, rightFragment);
                    fragment.startSample = start;
                    fragment.lengthSamples = juce::jmax<int64_t>(0, end - start);
                    fragment.localPoints.clear();
                    for (auto point : absolutePoints)
                    {
                        if (point.timeSamples >= start && point.timeSamples <= end)
                        {
                            point.timeSamples -= start;
                            fragment.localPoints.push_back(point);
                        }
                    }
                    return fragment;
                };

                if (sourceEnd <= splitSample)
                {
                    leftRegions.push_back(makeRegion(sourceStart, sourceEnd,
                                                      false, source.regionId));
                }
                else if (sourceStart >= splitSample)
                {
                    rightRegions.push_back(makeRegion(sourceStart, sourceEnd,
                                                       true, juce::Uuid()));
                }
                else
                {
                    leftRegions.push_back(makeRegion(sourceStart, splitSample,
                                                      false, source.regionId));
                    rightRegions.push_back(makeRegion(splitSample, sourceEnd,
                                                       true, juce::Uuid()));
                }
            }
        }

        removeClipAutomationForIds({ originalClipId, rightClipId }, false);
        for (auto& lane : leftLanes) lanes_.push_back(std::move(lane));
        for (auto& lane : rightLanes) lanes_.push_back(std::move(lane));
        for (auto& region : leftRegions) clipRegions_.push_back(std::move(region));
        for (auto& region : rightRegions) clipRegions_.push_back(std::move(region));
        publishSnapshot();
        return true;
    }

private:
    AutomationLaneCore* findEditableLane(const TrackID& trackId, const juce::String& parameterId) noexcept
    {
        for (auto& lane : lanes_)
            if (lane.trackId == trackId && lane.parameterId == parameterId)
                return &lane;

        return nullptr;
    }

    void clearLanePointsInRegionRange(const AutomationClipRegionCore& region)
    {
        if (auto* lane = findEditableLane(region.trackId, region.parameterId))
        {
            const int64_t start = region.startSample;
            const int64_t end = region.endSample();
            lane->points.erase(std::remove_if(lane->points.begin(), lane->points.end(),
                [start, end](const AutomationPoint& point)
                {
                    return point.timeSamples >= start && point.timeSamples <= end;
                }), lane->points.end());
            lane->sortAndResolveDuplicates();
        }
    }

    void syncClipRegionToLane(const AutomationClipRegionCore& region)
    {
        if (!region.isValid())
            return;

        auto& lane = getOrCreateLane(region.trackId, region.parameterId);
        const int64_t start = region.startSample;
        const int64_t end = region.endSample();
        lane.points.erase(std::remove_if(lane.points.begin(), lane.points.end(),
            [start, end](const AutomationPoint& point)
            {
                return point.timeSamples >= start && point.timeSamples <= end;
            }), lane.points.end());

        if (region.muted)
        {
            lane.sortAndResolveDuplicates();
            return;
        }

        for (auto point : region.localPoints)
        {
            point.timeSamples = juce::jlimit<int64_t>(start, end, start + juce::jlimit<int64_t>(0, region.lengthSamples, point.timeSamples));
            point.value = AutomationLaneCore::clampValueForParameter(region.parameterId, point.value);
            point.tensionToNext = juce::jlimit(-1.0f, 1.0f, point.tensionToNext);
            lane.points.push_back(point);
        }

        lane.sortAndResolveDuplicates();
    }

    void syncAllClipRegionsToLanes()
    {
        for (const auto& region : clipRegions_)
            clearLanePointsInRegionRange(region);

        for (const auto& region : clipRegions_)
            syncClipRegionToLane(region);
    }

    std::vector<AutomationLaneCore> lanes_;
    std::vector<AutomationClipRegionCore> clipRegions_;
    AutomationClipClipboardCore clipClipboard_;
    SegmentShapeClipboard segmentClipboard_;
    float copiedPointValue_ = 0.0f;
    bool hasCopiedPointValue_ = false;
    mutable std::atomic<uint64_t> nextVersion_{1};
    AutomationSnapshotPublisherCore publisher_;

    friend class AutomationSnapshotPublisherCore;
};

inline void AutomationSnapshotPublisherCore::publish(const AutomationManagerCore& manager)
{
    auto snap = std::make_shared<AutomationSnapshot>();
    snap->version = manager.nextVersion_++;
    snap->lanes.reserve(manager.lanes_.size());

    for (const auto& lane : manager.lanes_)
    {
        AutomationSnapshot::LaneSnapshot laneSnap;
        laneSnap.trackId = lane.trackId;
        laneSnap.parameterId = lane.parameterId;
        laneSnap.enabled = lane.isEnabled();
        laneSnap.points = lane.points;
        snap->lanes.push_back(std::move(laneSnap));
    }

    std::atomic_store_explicit(&snapshot_, snap, std::memory_order_release);
}

} // namespace DAW
