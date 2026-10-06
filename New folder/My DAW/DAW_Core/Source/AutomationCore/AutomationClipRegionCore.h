#pragma once
#include <JuceHeader.h>
#include "AutomationPointCore.h"
#include "AutomationLaneCore.h"
#include <vector>

namespace DAW {

struct AutomationClipRegionCore
{
    juce::Uuid regionId;
    TrackID trackId;
    juce::String parameterId;
    int64_t startSample = 0;
    int64_t lengthSamples = 0;
    juce::Colour color = juce::Colour(0xfff06bd8);
    juce::String name;
    bool muted = false;
    bool selected = false;
    std::vector<AutomationPoint> localPoints;

    AutomationClipRegionCore()
        : regionId(juce::Uuid()) {}

    int64_t endSample() const noexcept { return startSample + juce::jmax<int64_t>(0, lengthSamples); }
    bool containsSample(int64_t sample) const noexcept { return sample >= startSample && sample <= endSample(); }
    bool isValid() const noexcept { return lengthSamples > 0 && parameterId.isNotEmpty(); }

    float getValueAtLocalSample(int64_t localSample, float defaultValue = 0.0f) const noexcept
    {
        if (localPoints.empty())
            return defaultValue;

        localSample = juce::jlimit<int64_t>(0, juce::jmax<int64_t>(0, lengthSamples), localSample);

        if (localSample <= localPoints.front().timeSamples)
            return localPoints.front().value;

        if (localSample >= localPoints.back().timeSamples)
            return localPoints.back().value;

        for (size_t i = 1; i < localPoints.size(); ++i)
        {
            const auto& b = localPoints[i];
            if (localSample <= b.timeSamples)
            {
                const auto& a = localPoints[i - 1];
                const auto span = b.timeSamples - a.timeSamples;
                if (span <= 0)
                    return b.value;

                const float t = (float)(localSample - a.timeSamples) / (float)span;
                return AutomationCurveEvalCore::evaluate(a.value, b.value, t, a.curveToNext, a.tensionToNext);
            }
        }

        return defaultValue;
    }

    juce::Path buildPreviewPath(juce::Rectangle<float> bounds, int samples = 96, float defaultValue = 0.0f) const
    {
        juce::Path path;
        if (bounds.isEmpty() || lengthSamples <= 0)
            return path;

        samples = juce::jlimit(2, 512, samples);
        for (int i = 0; i < samples; ++i)
        {
            const float xNorm = samples <= 1 ? 0.0f : (float)i / (float)(samples - 1);
            const auto localSample = (int64_t)std::llround((double)xNorm * (double)lengthSamples);
            const float value = juce::jlimit(0.0f, 1.0f, getValueAtLocalSample(localSample, defaultValue));
            const float x = bounds.getX() + xNorm * bounds.getWidth();
            const float y = bounds.getBottom() - value * bounds.getHeight();

            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }

        return path;
    }

    std::vector<AutomationPoint> toAbsolutePoints() const
    {
        std::vector<AutomationPoint> result;
        result.reserve(localPoints.size());
        for (auto point : localPoints)
        {
            point.timeSamples += startSample;
            result.push_back(point);
        }
        return result;
    }

    void captureFromLaneRange(const AutomationLaneCore& lane)
    {
        trackId = lane.trackId;
        parameterId = lane.parameterId;
        localPoints.clear();

        for (auto point : lane.points)
        {
            if (point.timeSamples >= startSample && point.timeSamples <= endSample())
            {
                point.timeSamples -= startSample;
                localPoints.push_back(point);
            }
        }
    }

    void fitStateToLength(const std::vector<AutomationPoint>& sourcePoints, int64_t sourceLength)
    {
        localPoints.clear();
        if (sourcePoints.empty() || lengthSamples <= 0)
            return;

        sourceLength = juce::jmax<int64_t>(1, sourceLength);
        localPoints.reserve(sourcePoints.size());
        for (auto point : sourcePoints)
        {
            const auto clippedSourceTime = juce::jlimit<int64_t>(0, sourceLength, point.timeSamples);
            const double normalised = (double)clippedSourceTime / (double)sourceLength;
            point.timeSamples = (int64_t)std::llround(normalised * (double)lengthSamples);
            point.value = juce::jlimit(0.0f, 1.0f, point.value);
            localPoints.push_back(point);
        }
    }

    juce::ValueTree getState() const
    {
        juce::ValueTree state("AutomationClipRegion");
        state.setProperty("regionId", regionId.toString(), nullptr);
        state.setProperty("trackId", trackId, nullptr);
        state.setProperty("parameterId", parameterId, nullptr);
        state.setProperty("startSample", (juce::int64)startSample, nullptr);
        state.setProperty("lengthSamples", (juce::int64)lengthSamples, nullptr);
        state.setProperty("color", juce::String::toHexString((int64_t)color.getARGB()), nullptr);
        state.setProperty("name", name, nullptr);
        state.setProperty("muted", muted, nullptr);

        for (const auto& point : localPoints)
        {
            juce::ValueTree pt("Point");
            pt.setProperty("timeSamples", (juce::int64)point.timeSamples, nullptr);
            pt.setProperty("value", point.value, nullptr);
            pt.setProperty("curveToNext", (int)point.curveToNext, nullptr);
            pt.setProperty("curveToNextName", automationCurveTypeToString(point.curveToNext), nullptr);
            pt.setProperty("tensionToNext", point.tensionToNext, nullptr);
            state.addChild(pt, -1, nullptr);
        }

        return state;
    }

    static AutomationClipRegionCore fromState(const juce::ValueTree& state)
    {
        AutomationClipRegionCore region;
        if (!state.hasType("AutomationClipRegion"))
            return region;

        region.regionId = juce::Uuid(state.getProperty("regionId", juce::Uuid().toString()).toString());
        region.trackId = state.getProperty("trackId", {}).toString();
        region.parameterId = state.getProperty("parameterId", {}).toString();
        region.startSample = (int64_t)(juce::int64)state.getProperty("startSample", (juce::int64)0);
        region.lengthSamples = (int64_t)(juce::int64)state.getProperty("lengthSamples", (juce::int64)0);
        region.color = juce::Colour((uint32_t)state.getProperty("color", "fff06bd8").toString().getHexValue64());
        region.name = state.getProperty("name", {}).toString();
        region.muted = (bool)state.getProperty("muted", false);

        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            const auto child = state.getChild(i);
            if (!child.hasType("Point"))
                continue;

            AutomationPoint point;
            point.timeSamples = (int64_t)(juce::int64)child.getProperty("timeSamples", (juce::int64)0);
            point.value = (float)child.getProperty("value", 1.0f);
            point.curveToNext = child.hasProperty("curveToNextName")
                ? automationCurveTypeFromString(child.getProperty("curveToNextName").toString())
                : automationCurveTypeFromStoredInt((int)child.getProperty("curveToNext", 0));
            point.tensionToNext = juce::jlimit(-1.0f, 1.0f, (float)child.getProperty("tensionToNext", 0.0f));
            region.localPoints.push_back(point);
        }

        return region;
    }
};

} // namespace DAW
