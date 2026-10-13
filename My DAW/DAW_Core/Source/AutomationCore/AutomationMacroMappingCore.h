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
