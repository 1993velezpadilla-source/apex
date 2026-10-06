#pragma once
#include <JuceHeader.h>
#include "AutomationPointCore.h"
#include "../UtilityCore/Types.h"
#include <vector>
#include <algorithm>

namespace DAW {

/**
 * AutomationLaneCore — a single automation lane for one parameter.
 *
 * Stores ordered automation points and provides value lookup
 * at any timeline position with proper interpolation.
 *
 * Each automatable parameter has its own lane:
 *   - Track volume, pan, mute
 *   - Send levels
 *   - Plugin parameters
 *   - Bus controls
 *   - Master controls
 */
class AutomationLaneCore
{
public:
    static constexpr const char* trackVolumeParameterId = "track.volume";
    static constexpr const char* trackPanParameterId = "track.pan";
    static constexpr const char* trackTapeStopParameterId = "track.tape_stop";
    static constexpr const char* clipGainSuffix = ".gain";
    static constexpr const char* clipPanSuffix = ".pan";
    static constexpr const char* clipFadeInSuffix = ".fade_in";
    static constexpr const char* clipFadeOutSuffix = ".fade_out";
    static constexpr const char* clipTapeStopSuffix = ".tape_stop";
    static constexpr const char* clipPitchSuffix = ".pitch_shift";
    static constexpr const char* clipStretchSuffix = ".time_stretch";

    static bool isClipParameterId(const juce::String& parameterId) noexcept
    {
        return parameterId.startsWith("clip.");
    }

    static juce::String makeClipGainParameterId(const juce::String& clipId) { return "clip." + clipId + clipGainSuffix; }
    static juce::String makeClipPanParameterId(const juce::String& clipId) { return "clip." + clipId + clipPanSuffix; }
    static juce::String makeClipFadeInParameterId(const juce::String& clipId) { return "clip." + clipId + clipFadeInSuffix; }
    static juce::String makeClipFadeOutParameterId(const juce::String& clipId) { return "clip." + clipId + clipFadeOutSuffix; }
    static juce::String makeClipTapeStopParameterId(const juce::String& clipId) { return "clip." + clipId + clipTapeStopSuffix; }
    static juce::String makeClipPitchParameterId(const juce::String& clipId) { return "clip." + clipId + clipPitchSuffix; }
    static juce::String makeClipStretchParameterId(const juce::String& clipId) { return "clip." + clipId + clipStretchSuffix; }

    static juce::String tryExtractClipIdFromParameterId(const juce::String& parameterId)
    {
        if (!isClipParameterId(parameterId))
            return {};

        const juce::String body = parameterId.fromFirstOccurrenceOf("clip.", false, false);

        auto extractForSuffix = [&body](const char* suffix) -> juce::String
        {
            const juce::String suffixString(suffix);
            if (!body.endsWith(suffixString) || body.length() <= suffixString.length())
                return {};

            return body.dropLastCharacters(suffixString.length());
        };

        if (auto clipId = extractForSuffix(clipGainSuffix); clipId.isNotEmpty()) return clipId;
        if (auto clipId = extractForSuffix(clipPanSuffix); clipId.isNotEmpty()) return clipId;
        if (auto clipId = extractForSuffix(clipFadeInSuffix); clipId.isNotEmpty()) return clipId;
        if (auto clipId = extractForSuffix(clipFadeOutSuffix); clipId.isNotEmpty()) return clipId;
        if (auto clipId = extractForSuffix(clipTapeStopSuffix); clipId.isNotEmpty()) return clipId;
        if (auto clipId = extractForSuffix(clipPitchSuffix); clipId.isNotEmpty()) return clipId;
        if (auto clipId = extractForSuffix(clipStretchSuffix); clipId.isNotEmpty()) return clipId;

        return {};
    }

    static bool referencesExistingClip(const juce::String& parameterId,
                                       const std::function<bool(const juce::String&)>& clipExists)
    {
        if (!clipExists || !isClipParameterId(parameterId))
            return true;

        const auto clipId = tryExtractClipIdFromParameterId(parameterId);
        return clipId.isEmpty() || clipExists(clipId);
    }

    static float clampValueForParameter(const juce::String& parameterId, float value) noexcept
    {
        if (parameterId == trackPanParameterId)
            return juce::jlimit(-1.0f, 1.0f, value);

        if (parameterId == trackTapeStopParameterId)
            return juce::jlimit(0.0f, 1.0f, value);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(clipGainSuffix))
            return juce::jlimit(0.0f, 4.0f, value);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(clipPanSuffix))
            return juce::jlimit(-1.0f, 1.0f, value);

        if (parameterId.startsWith("clip.") && (parameterId.endsWith(clipFadeInSuffix) || parameterId.endsWith(clipFadeOutSuffix)))
            return juce::jmax(0.0f, value);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(clipTapeStopSuffix))
            return juce::jlimit(0.0f, 1.0f, value);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(clipPitchSuffix))
            return juce::jlimit(-36.0f, 36.0f, value);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(clipStretchSuffix))
            return juce::jlimit(0.1f, 4.0f, value);

        if (parameterId.startsWith("plugin.") || parameterId.startsWith("instrument."))
            return juce::jlimit(0.0f, 1.0f, value);

        return juce::jlimit(0.0f, 2.0f, value);
    }

    TrackID trackId;
    juce::String parameterId;
    std::vector<AutomationPoint> points;
    bool enabled = true;

    bool isEnabled() const noexcept { return enabled; }
    void setEnabled(bool e) noexcept { enabled = e; }

    AutomationLaneCore() = default;
    AutomationLaneCore(TrackID track, juce::String parameter)
        : trackId(std::move(track)), parameterId(std::move(parameter)) {}

    void setParameterId(const juce::String& id) { parameterId_ = id; parameterId = id; }
    const juce::String& getParameterId() const { return parameterId_.isNotEmpty() ? parameterId_ : parameterId; }

    void setParameterName(const juce::String& name) { parameterName_ = name; }
    const juce::String& getParameterName() const { return parameterName_; }

    // ── Point management ─────────────────────────────────────────────────

    void addPoint(juce::int64 position, float value)
    {
        value = clampValueForParameter(getParameterId(), value);
        points.push_back({ (int64_t)position, value });
        // sortAndResolveDuplicates() already calls rebuildLegacyPointMirror()
        // which fully rebuilds points_ from points — do NOT manually append
        // to points_ here, that caused a duplicate entry after every addPoint().
        sortAndResolveDuplicates();
    }

    void setPoint(int index, int64_t timeSamples, float value)
    {
        if (index < 0 || index >= (int)points.size())
            return;

        points[(size_t)index].timeSamples = timeSamples;
        points[(size_t)index].value = clampValueForParameter(getParameterId(), value);
        sortAndResolveDuplicates();
    }

    void removeAutomationPoint(int index)
    {
        if (index >= 0 && index < (int)points.size())
            points.erase(points.begin() + index);
    }

    void setCurveToNext(int index, AutomationCurveType curve)
    {
        if (index >= 0 && index < (int)points.size())
            points[(size_t)index].curveToNext = curve;
    }

    void setTensionToNext(int index, float tension)
    {
        if (index >= 0 && index < (int)points.size())
            points[(size_t)index].tensionToNext = juce::jlimit(-1.0f, 1.0f, tension);
    }

    int insertPointPreservingLevel(int64_t timeSamples)
    {
        const float value = getValueAtSample(timeSamples, defaultValue_);
        AutomationCurveType curve = AutomationCurveType::Linear;
        float tension = 0.0f;

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            if (timeSamples > points[i].timeSamples && timeSamples < points[i + 1].timeSamples)
            {
                curve = points[i].curveToNext;
                tension = points[i].tensionToNext;
                points[i].curveToNext = curve;
                points[i].tensionToNext = tension;
                break;
            }
        }

        points.push_back({ timeSamples, clampValueForParameter(getParameterId(), value), curve, tension });
        sortAndResolveDuplicates();

        for (size_t i = 0; i < points.size(); ++i)
        {
            if (points[i].timeSamples == timeSamples)
            {
                if (i > 0)
                {
                    points[i - 1].curveToNext = curve;
                    points[i - 1].tensionToNext = tension;
                }
                points[i].curveToNext = curve;
                points[i].tensionToNext = tension;
                return (int)i;
            }
        }

        return -1;
    }

    void removePoint(int index)
    {
        if (index >= 0 && index < (int)points_.size())
            points_.erase(points_.begin() + index);
    }

    void clearAllPoints() { points_.clear(); }
    void clear() noexcept { points.clear(); points_.clear(); }

    int getNumPoints() const { return !points.empty() ? (int)points.size() : (int)points_.size(); }

    const AutomationPointCore* getPoint(int index) const
    {
        if (!points.empty())
        {
            syncLegacyMirrorForRead();
            if (index >= 0 && index < (int)points_.size())
                return &points_[(size_t)index];
        }

        if (index >= 0 && index < (int)points_.size())
            return &points_[(size_t)index];
        return nullptr;
    }

    // ── Value lookup (audio thread safe — read-only) ─────────────────────

    /** Get interpolated value at the given timeline position. */
    float getValueAtPosition(juce::int64 position) const
    {
        return getValueAtSample((int64_t)position, defaultValue_);
    }

    float getValueAtSample(int64_t samplePosition, float defaultValue) const noexcept
    {
        if (!enabled)
            return defaultValue;

        if (!points.empty())
            return evaluatePoints(points, samplePosition, defaultValue);

        if (points_.empty()) return defaultValue;

        // Before first point
        if (samplePosition <= points_.front().position)
            return points_.front().value;

        // After last point
        if (samplePosition >= points_.back().position)
            return points_.back().value;

        // Binary search for surrounding points
        for (size_t i = 0; i + 1 < points_.size(); ++i)
        {
            if (samplePosition >= points_[i].position && samplePosition < points_[i + 1].position)
            {
                juce::int64 span = points_[i + 1].position - points_[i].position;
                if (span <= 0) return points_[i].value;
                float frac = (float)(samplePosition - points_[i].position) / (float)span;
                return points_[i].value + frac * (points_[i + 1].value - points_[i].value);
            }
        }

        return defaultValue;
    }

    void sortAndResolveDuplicates()
    {
        std::sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b)
        {
            return a.timeSamples < b.timeSamples;
        });

        if (points.size() < 2)
            return;

        size_t write = 0;
        for (size_t read = 1; read < points.size(); ++read)
        {
            if (points[read].timeSamples == points[write].timeSamples)
                points[write] = points[read];
            else
                points[++write] = points[read];
        }

        points.resize(write + 1);
        rebuildLegacyPointMirror();
    }

    // ── State ────────────────────────────────────────────────────────────

    void setVisible(bool v) noexcept { visible_ = v; }
    bool isVisible() const noexcept { return visible_; }

    void setDefaultValue(float v) noexcept { defaultValue_ = v; }
    float getDefaultValue() const noexcept { return defaultValue_; }

    bool isEmpty() const { return points.empty() && points_.empty(); }

    // ── Serialization ────────────────────────────────────────────────────

    juce::ValueTree getState() const
    {
        juce::ValueTree state("AutomationLane");
        state.setProperty("parameterId", parameterId_, nullptr);
        state.setProperty("parameterName", parameterName_, nullptr);
        state.setProperty("defaultValue", defaultValue_, nullptr);
        state.setProperty("visible", visible_, nullptr);

        const auto& statePoints = !points.empty() ? points : makeAutomationPointsFromLegacyMirror();
        for (const auto& point : statePoints)
        {
            juce::ValueTree pt("Point");
            pt.setProperty("position", (juce::int64)point.timeSamples, nullptr);
            pt.setProperty("timeSamples", (juce::int64)point.timeSamples, nullptr);
            pt.setProperty("value", point.value, nullptr);
            pt.setProperty("curveToNext", (int)point.curveToNext, nullptr);
            pt.setProperty("curveToNextName", automationCurveTypeToString(point.curveToNext), nullptr);
            pt.setProperty("tensionToNext", point.tensionToNext, nullptr);
            state.addChild(pt, -1, nullptr);
        }
        return state;
    }

    void restoreState(const juce::ValueTree& state)
    {
        parameterId_ = state.getProperty("parameterId", "").toString();
        parameterName_ = state.getProperty("parameterName", "").toString();
        defaultValue_ = (float)state.getProperty("defaultValue", 0.0f);
        visible_ = (bool)state.getProperty("visible", false);

        points_.clear();
        points.clear();
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto child = state.getChild(i);
            if (child.hasType("Point"))
            {
                AutomationPoint point;
                point.timeSamples = (int64_t)(juce::int64)child.getProperty("timeSamples", child.getProperty("position", (juce::int64)0));
                point.value = clampValueForParameter(getParameterId(), (float)child.getProperty("value", defaultValue_));
                point.curveToNext = child.hasProperty("curveToNextName")
                    ? automationCurveTypeFromString(child.getProperty("curveToNextName").toString())
                    : automationCurveTypeFromStoredInt((int)child.getProperty("curveToNext", 0));
                point.tensionToNext = juce::jlimit(-1.0f, 1.0f, (float)child.getProperty("tensionToNext", 0.0f));
                points.push_back(point);
            }
        }

        sortAndResolveDuplicates();
    }

private:
    static float evaluatePoints(const std::vector<AutomationPoint>& sourcePoints, int64_t samplePosition, float defaultValue) noexcept
    {
        if (sourcePoints.empty())
            return defaultValue;

        if (samplePosition <= sourcePoints.front().timeSamples)
            return sourcePoints.front().value;

        if (samplePosition >= sourcePoints.back().timeSamples)
            return sourcePoints.back().value;

        for (size_t i = 1; i < sourcePoints.size(); ++i)
        {
            const auto& p1 = sourcePoints[i];
            if (samplePosition <= p1.timeSamples)
            {
                const auto& p0 = sourcePoints[i - 1];
                const auto span = p1.timeSamples - p0.timeSamples;
                if (span <= 0)
                    return p1.value;

                const float t = (float)(samplePosition - p0.timeSamples) / (float)span;
                return AutomationCurveEvalCore::evaluate(p0.value, p1.value, t, p0.curveToNext, p0.tensionToNext);
            }
        }

        return defaultValue;
    }

    std::vector<AutomationPoint> makeAutomationPointsFromLegacyMirror() const
    {
        std::vector<AutomationPoint> result;
        result.reserve(points_.size());
        for (const auto& point : points_)
            result.push_back({ (int64_t)point.position, point.value, AutomationCurveType::Linear, 0.0f });
        return result;
    }

    void rebuildLegacyPointMirror()
    {
        points_.clear();
        points_.reserve(points.size());
        for (const auto& point : points)
        {
            AutomationPointCore mirror;
            mirror.position = (juce::int64)point.timeSamples;
            mirror.value = point.value;
            mirror.selected = false;
            points_.push_back(mirror);
        }
    }

    void syncLegacyMirrorForRead() const
    {
        points_.clear();
        points_.reserve(points.size());
        for (const auto& point : points)
        {
            AutomationPointCore mirror;
            mirror.position = (juce::int64)point.timeSamples;
            mirror.value = point.value;
            mirror.selected = false;
            points_.push_back(mirror);
        }
    }

    juce::String parameterId_;
    juce::String parameterName_;
    float defaultValue_ = 0.0f;
    bool visible_ = false;
    mutable std::vector<AutomationPointCore> points_;
};

} // namespace DAW
