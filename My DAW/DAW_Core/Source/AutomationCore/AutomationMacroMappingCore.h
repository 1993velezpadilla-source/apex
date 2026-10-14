#pragma once
#include "AutomationMultiParameterSnapshotCore.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace DAW {

/**
 * Offline control-thread macro envelope compiler for APEX Automation V2.
 *
 * One normalized macro curve (0..1) can drive explicit native volume/PAN and
 * individual continuous VST3 parameter instances, with a different value
 * range and inversion per destination. This compiles ordinary lane curves:
 * there is NO extra per-sample macro object in the audio callback.
 *
 * IMPORTANT: VST3 targets are allowed only when the host explicitly reports
 * that their parameter has stepCount==0 (continuous). This core does not
 * infer that from the name or write to a live AudioProcessor.
 *
 * No side effects while planning; the shared multiparameter transaction
 * validates every target and publishes a single manager DSP snapshot.
 * Host PPQ lane mirroring/GUI integration is a separate integration gate.
 */
struct AutomationMacroMappingCore
{
    using Target = AutomationMultiParameterSnapshotCore::Target;
    using Plan = AutomationMultiParameterSnapshotCore::Plan;

    struct Binding
    {
        Target target;
        float minimum = 0.0f;
        float maximum = 1.0f;
        bool inverted = false;
        // Only relevant to VST3; caller must populate from the actual
        // matching plugin instance's AudioProcessorParameter metadata.
        bool pluginIsVerifiedContinuous = false;
    };

    static constexpr size_t maxBindings = 64;
    static constexpr size_t maxMacroPoints = 2048;

    struct Configuration
    {
        juce::String name;
        std::vector<AutomationPoint> controlPoints;
        std::vector<Binding> bindings;
    };

    // Save macro shape and destinations in one project ValueTree, but NEVER
    // serialize pluginIsVerifiedContinuous: that evidence is valid only for
    // the exact currently loaded processor instance, and must be re-queried
    // by the host after opening a project or replacing a plugin.
    static juce::ValueTree serializeConfiguration(const Configuration& definition)
    {
        juce::ValueTree root("AutomationMacro");
        if (definition.controlPoints.empty() || definition.bindings.empty()
            || definition.controlPoints.size() > maxMacroPoints
            || definition.bindings.size() > maxBindings
            || !AutomationMultiParameterSnapshotCore::validPoints(
                definition.controlPoints))
            return root;

        root.setProperty("version", 1, nullptr);
        root.setProperty("name", definition.name, nullptr);
        juce::ValueTree envelope("Envelope");
        for (const auto& p : definition.controlPoints)
        {
            if (p.value < 0.0f || p.value > 1.0f
                || p.tensionToNext < -1.0f || p.tensionToNext > 1.0f)
                return juce::ValueTree("AutomationMacro");
            juce::ValueTree point("Point");
            point.setProperty("timeSamples", (juce::int64)p.timeSamples, nullptr);
            point.setProperty("value", p.value, nullptr);
            point.setProperty("curve", automationCurveTypeToString(p.curveToNext), nullptr);
            point.setProperty("tension", p.tensionToNext, nullptr);
            envelope.addChild(point, -1, nullptr);
        }
        root.addChild(envelope, -1, nullptr);

        for (const auto& b : definition.bindings)
        {
            if (b.target.trackId.isEmpty()
                || !AutomationMultiParameterSnapshotCore::supported(
                    b.target.parameterId)
                || !std::isfinite(b.minimum) || !std::isfinite(b.maximum)
                || b.minimum >= b.maximum)
                return juce::ValueTree("AutomationMacro");

            const float lowDomain =
                b.target.parameterId == AutomationLaneCore::trackPanParameterId
                    ? -1.0f : 0.0f;
            const float highDomain =
                b.target.parameterId == AutomationLaneCore::trackVolumeParameterId
                    ? 2.0f : 1.0f;
            if (b.minimum < lowDomain || b.maximum > highDomain)
                return juce::ValueTree("AutomationMacro");

            for (int i = 1; i < root.getNumChildren(); ++i)
            {
                const auto previous = root.getChild(i);
                if (previous.getProperty("trackId").toString() == b.target.trackId
                    && previous.getProperty("parameterId").toString()
                        == b.target.parameterId)
                    return juce::ValueTree("AutomationMacro");
            }

            juce::ValueTree target("Target");
            target.setProperty("trackId", b.target.trackId, nullptr);
            target.setProperty("parameterId", b.target.parameterId, nullptr);
            target.setProperty("minimum", b.minimum, nullptr);
            target.setProperty("maximum", b.maximum, nullptr);
            target.setProperty("inverted", b.inverted, nullptr);
            root.addChild(target, -1, nullptr);
        }
        return root;
    }

    static bool deserializeConfiguration(
        const juce::ValueTree& root, Configuration& result)
    {
        result = {};
        if (!root.hasType("AutomationMacro")
            || (int)root.getProperty("version", 0) != 1
            || root.getNumChildren() < 2
            || root.getNumChildren() > (int)maxBindings + 1)
            return false;

        const auto envelope = root.getChild(0);
        if (!envelope.hasType("Envelope") || envelope.getNumChildren() <= 0
            || envelope.getNumChildren() > (int)maxMacroPoints)
            return false;

        Configuration staged;
        staged.name = root.getProperty("name", "").toString();
        staged.controlPoints.reserve((size_t)envelope.getNumChildren());
        for (int i = 0; i < envelope.getNumChildren(); ++i)
        {
            const auto pointTree = envelope.getChild(i);
            if (!pointTree.hasType("Point")
                || !pointTree.hasProperty("timeSamples")
                || !pointTree.hasProperty("value")
                || !pointTree.hasProperty("curve")
                || !pointTree.hasProperty("tension"))
                return false;

            const auto curve = pointTree.getProperty("curve").toString();
            AutomationPoint p;
            p.timeSamples = (int64_t)(juce::int64)pointTree.getProperty("timeSamples");
            p.value = (float)pointTree.getProperty("value");
            p.curveToNext = automationCurveTypeFromString(curve);
            if (juce::String(automationCurveTypeToString(p.curveToNext)) != curve)
                return false;
            p.tensionToNext = (float)pointTree.getProperty("tension");
            staged.controlPoints.push_back(p);
        }
        if (!AutomationMultiParameterSnapshotCore::validPoints(
                staged.controlPoints))
            return false;
        for (const auto& p : staged.controlPoints)
            if (p.value < 0.0f || p.value > 1.0f
                || p.tensionToNext < -1.0f || p.tensionToNext > 1.0f)
                return false;

        staged.bindings.reserve((size_t)root.getNumChildren() - 1);
        for (int i = 1; i < root.getNumChildren(); ++i)
        {
            const auto node = root.getChild(i);
            if (!node.hasType("Target")
                || !node.hasProperty("minimum") || !node.hasProperty("maximum")
                || !node.hasProperty("inverted"))
                return false;

            Binding b;
            b.target.trackId = node.getProperty("trackId", {}).toString();
            b.target.parameterId = node.getProperty("parameterId", {}).toString();
            b.minimum = (float)node.getProperty("minimum");
            b.maximum = (float)node.getProperty("maximum");
            b.inverted = (bool)node.getProperty("inverted");
            b.pluginIsVerifiedContinuous = false; // ALWAYS revalidate host metadata
            if (b.target.trackId.isEmpty()
                || !AutomationMultiParameterSnapshotCore::supported(
                    b.target.parameterId)
                || !std::isfinite(b.minimum) || !std::isfinite(b.maximum)
                || b.minimum >= b.maximum)
                return false;

            const float lowDomain =
                b.target.parameterId == AutomationLaneCore::trackPanParameterId
                    ? -1.0f : 0.0f;
            const float highDomain =
                b.target.parameterId == AutomationLaneCore::trackVolumeParameterId
                    ? 2.0f : 1.0f;
            if (b.minimum < lowDomain || b.maximum > highDomain)
                return false;

            for (const auto& other : staged.bindings)
                if (other.target == b.target)
                    return false;
            staged.bindings.push_back(std::move(b));
        }
        result = std::move(staged);
        return true;
    }

    static bool compile(const AutomationManagerCore& manager,
                        const std::vector<AutomationPoint>& macro,
                        const std::vector<Binding>& bindings,
                        Plan& result)
    {
        result.changes.clear();
        if (bindings.empty() || bindings.size() > maxBindings
            || macro.empty() || macro.size() > maxMacroPoints
            || !AutomationMultiParameterSnapshotCore::validPoints(macro))
            return false;

        // Reject non-normalized macro control values; clipping could turn
        // a delicate fade or PAN sweep into sudden changes without warning.
        for (const auto& point : macro)
            if (point.value < 0.0f || point.value > 1.0f
                || point.tensionToNext < -1.0f || point.tensionToNext > 1.0f)
                return false;

        Plan staged;
        staged.changes.reserve(bindings.size());
        for (const auto& binding : bindings)
        {
            const auto& target = binding.target;
            if (target.trackId.isEmpty()
                || !AutomationMultiParameterSnapshotCore::supported(target.parameterId)
                || !std::isfinite(binding.minimum)
                || !std::isfinite(binding.maximum)
                || binding.minimum >= binding.maximum)
                return false;

            const bool isPlugin = target.parameterId.startsWith("plugin.");
            if (isPlugin && !binding.pluginIsVerifiedContinuous)
                return false; // VST3 discrete/enum parameters require step mapping.

            const float lowDomain = target.parameterId == AutomationLaneCore::trackPanParameterId
                ? -1.0f : 0.0f;
            const float highDomain = target.parameterId == AutomationLaneCore::trackVolumeParameterId
                ? 2.0f : 1.0f;
            if (binding.minimum < lowDomain || binding.maximum > highDomain)
                return false;

            for (const auto& other : staged.changes)
                if (other.target == target)
                    return false;

            if (manager.hasClipRegionsForParameter(
                    target.trackId, target.parameterId))
                return false;
            const auto* current = manager.findLane(
                target.trackId, target.parameterId);
            if (current == nullptr || !current->isEnabled())
                return false;

            AutomationLaneCore changed = *current;
            changed.points.clear();
            changed.points.reserve(macro.size());
            for (auto point : macro)
            {
                const float x = binding.inverted ? 1.0f - point.value : point.value;
                point.value = binding.minimum
                            + (binding.maximum - binding.minimum) * x;
                if (!std::isfinite(point.value)
                    || point.value != AutomationLaneCore::clampValueForParameter(
                        target.parameterId, point.value))
                    return false;
                changed.points.push_back(point);
            }
            changed.sortAndResolveDuplicates();
            staged.changes.push_back(
                {target, current->getState(), changed.getState()});
        }
        result = std::move(staged);
        return true;
    }

    static bool apply(AutomationManagerCore& manager, const Plan& plan, bool after)
    {
        return AutomationMultiParameterSnapshotCore::apply(manager, plan, after);
    }
};

} // namespace DAW
