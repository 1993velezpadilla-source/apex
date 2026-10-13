#pragma once
#include "AutomationManagerCore.h"
#include "AutomationLaneStateValidationCore.h"
#include "AutomationCurveMorphCore.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace DAW {

/**
 * Offline, explicitly targeted A/B automation snapshots across multiple
 * parameters and tracks. No callback/audio-thread work: capture, plan and
 * apply belong exclusively on the message/control thread.
 *
 * A plug-in key must be its exact slot + INSTANCE ID + parameter, never a
 * display name or a shared slot number. If an instance/track is absent,
 * EVERY parameter in the operation is rejected before a single edit.
 *
 * This core models curves, not live VST3 parameter writes. GUI/host mapping
 * and global APEX PPQ mirroring need a separate qualified integration gate.
 */
struct AutomationMultiParameterSnapshotCore
{
    struct Target
    {
        TrackID trackId;
        juce::String parameterId;

        bool operator==(const Target& other) const noexcept
        {
            return trackId == other.trackId && parameterId == other.parameterId;
        }
    };

    struct LaneCapture
    {
        Target target;
        std::vector<AutomationPoint> points;
    };

    struct Snapshot
    {
        std::vector<LaneCapture> lanes;
        bool isValid() const noexcept { return !lanes.empty(); }
    };

    struct LaneChange
    {
        Target target;
        juce::ValueTree before;
        juce::ValueTree after;
    };

    struct Plan
    {
        std::vector<LaneChange> changes;
        bool isValid() const noexcept { return !changes.empty(); }
    };

    static constexpr size_t maxParameters = 64;
    static constexpr size_t maxKnotsPerLane = 2048;

    static bool supported(const juce::String& parameter)
    {
        if (parameter == AutomationLaneCore::trackVolumeParameterId
            || parameter == AutomationLaneCore::trackPanParameterId)
            return true;

        // Parse plugin.<numeric-slot>.<instance-id>.<parameter-name>.
        // Do not group plug-ins by display name, because many tracks can
        // contain different instances of the SAME processor.
        if (!parameter.startsWith("plugin."))
            return false;

        const auto rest = parameter.substring(7);
        const auto slot = rest.upToFirstOccurrenceOf(".", false, false);
        if (slot.isEmpty() || !slot.containsOnly("0123456789"))
            return false;
        const auto afterSlot = rest.fromFirstOccurrenceOf(".", false, false);
        const auto instance = afterSlot.upToFirstOccurrenceOf(".", false, false);
        const auto parameterName = afterSlot.fromFirstOccurrenceOf(".", false, false);
        return instance.isNotEmpty() && parameterName.isNotEmpty()
            && afterSlot.containsChar('.');
    }

    static bool validPoints(const std::vector<AutomationPoint>& points) noexcept
    {
        if (points.empty() || points.size() > maxKnotsPerLane)
            return false;
        int64_t previous = -1;
        for (const auto& p : points)
        {
            if (p.timeSamples < 0 || p.timeSamples <= previous
                || !std::isfinite(p.value) || !std::isfinite(p.tensionToNext))
                return false;
            previous = p.timeSamples;
        }
        return true;
    }

    static bool validPointsForParameter(
        const juce::String& parameterId,
        const std::vector<AutomationPoint>& points) noexcept
    {
        if (!validPoints(points))
            return false;

        for (const auto& point : points)
            if (point.value != AutomationLaneCore::clampValueForParameter(
                    parameterId, point.value)
                || point.tensionToNext < -1.0f || point.tensionToNext > 1.0f)
                return false; // no silent level clamp during snapshot morph

        return true;
    }

    // Persist A/B memories separately from the project's currently playing
    // envelopes. Metadata is exact (track+plugin instance+parameter). No
    // generated VST3 processor IDs or pointers are serialized here.
    static juce::ValueTree serializeSnapshot(const Snapshot& snapshot)
    {
        juce::ValueTree root("AutomationMultiSnapshot");
        root.setProperty("version", 1, nullptr);
        if (!snapshot.isValid() || snapshot.lanes.size() > maxParameters)
            return root;

        for (const auto& lane : snapshot.lanes)
        {
            if (lane.target.trackId.isEmpty()
                || !supported(lane.target.parameterId)
                || !validPointsForParameter(lane.target.parameterId, lane.points))
                return juce::ValueTree("AutomationMultiSnapshot");

            for (int existing = 0; existing < root.getNumChildren(); ++existing)
            {
                const auto previous = root.getChild(existing);
                if (previous.getProperty("trackId").toString() == lane.target.trackId
                    && previous.getProperty("parameterId").toString() == lane.target.parameterId)
                    return juce::ValueTree("AutomationMultiSnapshot");
            }

            juce::ValueTree t("Target");
            t.setProperty("trackId", lane.target.trackId, nullptr);
            t.setProperty("parameterId", lane.target.parameterId, nullptr);
            for (const auto& point : lane.points)
            {
                juce::ValueTree p("Point");
                p.setProperty("timeSamples", (juce::int64)point.timeSamples, nullptr);
                p.setProperty("value", point.value, nullptr);
                p.setProperty("curveToNextName",
                    automationCurveTypeToString(point.curveToNext), nullptr);
                p.setProperty("tensionToNext", point.tensionToNext, nullptr);
                t.addChild(p, -1, nullptr);
            }
            root.addChild(t, -1, nullptr);
        }
        return root;
    }

    // Invalid/malicious old project snapshots never partially load. All
    // validation is completed into a detached temporary object first.
    static bool deserializeSnapshot(const juce::ValueTree& root, Snapshot& output)
    {
        output.lanes.clear();
        if (!root.hasType("AutomationMultiSnapshot")
            || (int)root.getProperty("version", 0) != 1
            || root.getNumChildren() <= 0
            || root.getNumChildren() > (int)maxParameters)
            return false;

        Snapshot staged;
        staged.lanes.reserve((size_t)root.getNumChildren());
        for (int i = 0; i < root.getNumChildren(); ++i)
        {
            const auto targetTree = root.getChild(i);
            if (!targetTree.hasType("Target")
                || targetTree.getNumChildren() <= 0
                || targetTree.getNumChildren() > (int)maxKnotsPerLane)
                return false;

            LaneCapture entry;
            entry.target.trackId = targetTree.getProperty("trackId", {}).toString();
            entry.target.parameterId = targetTree.getProperty("parameterId", {}).toString();
            if (entry.target.trackId.isEmpty()
                || !supported(entry.target.parameterId))
                return false;

            for (const auto& existing : staged.lanes)
                if (existing.target == entry.target)
                    return false;

            entry.points.reserve((size_t)targetTree.getNumChildren());
            for (int p = 0; p < targetTree.getNumChildren(); ++p)
            {
                const auto child = targetTree.getChild(p);
                if (!child.hasType("Point")
                    || !child.hasProperty("timeSamples")
                    || !child.hasProperty("value")
                    || !child.hasProperty("curveToNextName")
                    || !child.hasProperty("tensionToNext"))
                    return false;

                const auto curve = child.getProperty("curveToNextName").toString();
                if (curve.isEmpty())
                    return false;

                AutomationPoint point;
                point.timeSamples = (int64_t)(juce::int64)child.getProperty("timeSamples");
                point.value = (float)child.getProperty("value");
                point.curveToNext = automationCurveTypeFromString(curve);
                if (automationCurveTypeToString(point.curveToNext) != curve)
                    return false; // reject corrupted or unknown curve families
                point.tensionToNext = (float)child.getProperty("tensionToNext");
                entry.points.push_back(point);
            }
            if (!validPointsForParameter(entry.target.parameterId, entry.points))
                return false;
            staged.lanes.push_back(std::move(entry));
        }

        output = std::move(staged);
        return true;
    }

    static bool capture(const AutomationManagerCore& manager,
                        const std::vector<Target>& targets,
                        Snapshot& result)
    {
        result.lanes.clear();
        if (targets.empty() || targets.size() > maxParameters)
            return false;

        Snapshot staged;
        staged.lanes.reserve(targets.size());
        for (const auto& target : targets)
        {
            if (target.trackId.isEmpty() || !supported(target.parameterId)
                || manager.hasClipRegionsForParameter(target.trackId, target.parameterId))
                return false;

            const auto* lane = manager.findLane(target.trackId, target.parameterId);
            if (lane == nullptr || !lane->isEnabled() || !validPointsForParameter(
                    target.parameterId, lane->points))
                return false;

            for (const auto& captured : staged.lanes)
                if (captured.target == target)
                    return false;

            staged.lanes.push_back({target, lane->points});
        }

        result = std::move(staged);
        return true;
    }

    // For VST3 interpolation an external host must certify each EXACT
    // track+slot+instance+parameter as continuous (stepCount == 0). Unknown
    // plug-in parameters are deliberately rejected, rather than assuming
    // they can be interpolated as floats (many are enums, toggles or steps).
    // Native volume and PAN need no host callback.
    using ContinuousPluginGuard = std::function<bool(const Target&)>;
    static bool planMorph(const AutomationManagerCore& manager,
                          const Snapshot& left, const Snapshot& right,
                          float amount, Plan& result,
                          const ContinuousPluginGuard& isContinuousPlugin = {})
    {
        result.changes.clear();
        if (!left.isValid() || !right.isValid()
            || left.lanes.size() != right.lanes.size()
            || left.lanes.size() > maxParameters
            || !std::isfinite(amount) || amount < 0.0f || amount > 1.0f)
            return false;

        Plan staged;
        staged.changes.reserve(left.lanes.size());
        for (const auto& a : left.lanes)
        {
            if (a.target.trackId.isEmpty() || !supported(a.target.parameterId)
                || (a.target.parameterId.startsWith("plugin.")
                    && (!isContinuousPlugin || !isContinuousPlugin(a.target)))
                || !validPointsForParameter(a.target.parameterId, a.points)
                || manager.hasClipRegionsForParameter(a.target.trackId, a.target.parameterId))
                return false;

            const LaneCapture* counterpart = nullptr;
            size_t matches = 0;
            for (const auto& b : right.lanes)
                if (b.target == a.target)
                {
                    counterpart = &b;
                    ++matches;
                }
            if (matches != 1 || counterpart == nullptr
                || !validPointsForParameter(
                    counterpart->target.parameterId, counterpart->points))
                return false;

            for (const auto& previous : staged.changes)
                if (previous.target == a.target)
                    return false;

            const auto* actual = manager.findLane(a.target.trackId, a.target.parameterId);
            if (actual == nullptr || !actual->isEnabled())
                return false;

            std::vector<AutomationPoint> blended;
            if (!AutomationCurveMorphCore::blend(
                    a.target.parameterId, a.points, counterpart->points,
                    amount, blended))
                return false;

            AutomationLaneCore replacement = *actual;
            replacement.points = std::move(blended);
            replacement.sortAndResolveDuplicates();
            staged.changes.push_back({
                a.target, actual->getState(), replacement.getState()});
        }

        result = std::move(staged);
        return true;
    }

    static bool apply(AutomationManagerCore& manager, const Plan& plan, bool after)
    {
        if (!plan.isValid() || plan.changes.size() > maxParameters)
            return false;

        std::vector<AutomationLaneCore> staged;
        staged.reserve(plan.changes.size());
        for (size_t i = 0; i < plan.changes.size(); ++i)
        {
            const auto& change = plan.changes[i];
            if (change.target.trackId.isEmpty() || !supported(change.target.parameterId)
                || !AutomationLaneStateValidationCore::valid(
                    change.before, change.target.parameterId,
                    AutomationCurveMorphCore::maxOutputKnots)
                || !AutomationLaneStateValidationCore::valid(
                    change.after, change.target.parameterId,
                    AutomationCurveMorphCore::maxOutputKnots)
                || manager.hasClipRegionsForParameter(
                    change.target.trackId, change.target.parameterId))
                return false;

            for (size_t j = 0; j < i; ++j)
                if (change.target == plan.changes[j].target)
                    return false;

            const auto* live = manager.findLane(
                change.target.trackId, change.target.parameterId);
            if (live == nullptr || !live->isEnabled())
                return false;

            // Refuse to overwrite out-of-history edits performed since the
            // plan was captured. This is especially important for Undo.
            const auto expected = after ? change.before : change.after;
            if (live->getState().toXmlString() != expected.toXmlString())
                return false;

            AutomationLaneCore replacement = *live;
            const auto desired = after ? change.after : change.before;
            replacement.restoreState(desired);
            if (replacement.getParameterId() != change.target.parameterId
                || replacement.getState().toXmlString() != desired.toXmlString())
                return false; // refuse invalid/rounded state rather than lie

            staged.push_back(std::move(replacement));
        }

        for (size_t i = 0; i < plan.changes.size(); ++i)
        {
            const auto& t = plan.changes[i].target;
            manager.getOrCreateLane(t.trackId, t.parameterId) = std::move(staged[i]);
        }
        manager.publishSnapshot(); // one authoritative DSP snapshot per morph/Undo
        return true;
    }
};

} // namespace DAW
