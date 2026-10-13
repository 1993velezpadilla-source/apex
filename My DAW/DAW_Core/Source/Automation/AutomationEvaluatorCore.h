#pragma once

#include "AutomationParameterCore.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationLaneStoreCore.h"
#include "AutomationModeStateCore.h"
#include <JuceHeader.h>

namespace apex::automation
{
    /**
        Walks every parameter that has a lane, evaluates the curve at the
        current PPQ playhead, and writes the value into the parameter.

        Call evaluateBlock() once per audio block from the audio thread,
        BEFORE the rest of the audio processing reads parameter values.

        Phase 1A: block-rate evaluation (one update per block at block start).
        Phase 1B will add sample-accurate IParameterChanges for VST3 plugins.
    */
    class AutomationEvaluator
    {
    public:
        AutomationEvaluator (AutomationParameterRegistry& reg,
                             AutomationLaneStore&         laneStore,
                             AutomationModeState&         modes)
            : registry  (reg)
            , lanes     (laneStore)
            , modeState (modes)
        {
        }

        // Call once per processBlock, BEFORE reading any parameter values.
        // Runs on the audio thread: every lookup below is lock-free and
        // allocation-free (published snapshots + shared atomics only).
        void evaluateBlock (double playheadPPQ, bool transportRolling) noexcept
        {
            if (! transportRolling)
                return;

            // Native parameter state must resolve from the SAME PPQ map
            // generation pinned by the complete device/offline callback.
            // Otherwise native faders could reflect new curves while VST3
            // instances still render the old macro bank for this block.
            const auto ppqLaneMap = lanes.getEffectiveLaneSnapshotRT();
            registry.forEachRT ([this, playheadPPQ, &ppqLaneMap] (AutomationParameter& p)
            {
                evaluateOne (p, playheadPPQ, ppqLaneMap);
            });
        }

        void onTransportStop()
        {
            modeState.clearAllLatches();
        }

    private:
        void evaluateOne (
            AutomationParameter& p, double playheadPPQ,
            const AutomationLaneStore::RtLaneSnapshot& ppqLaneMap) noexcept
        {
            const auto id = p.getID();

            if (! modeState.isReadingRT (id))
                return;

            // User gesture wins. Latch and Write keep the last user value
            // after release until transport Stop. Re-evaluating the old lane
            // during this hold would fight the value on every audio block.
            const auto mode = modeState.getModeRT (id);
            if (p.isGestureActive()
                || ((mode == AutomationMode::Latch || mode == AutomationMode::Write)
                    && modeState.latchHeldRT (id)))
                return;

            auto lane = AutomationLaneStore::findLaneInSnapshotRT (ppqLaneMap, id);
            if (lane == nullptr) return;

            auto snap = lane->getSnapshot();
            if (snap == nullptr || snap->empty()) return;

            // A stepped (enum/switch/boolean) control must retain its
            // preceding value until the next breakpoint, even if a saved
            // or imported lane incorrectly marks the segment Linear/Smooth.
            // Interpolation here made the UI/plugin-facing atomic switch
            // EARLIER than the direct mute/solo DSP path at the same PPQ.
            float v;
            if (p.getRange().isStepped)
            {
                const auto upper = std::upper_bound(
                    snap->begin(), snap->end(), playheadPPQ,
                    [] (double t, const Breakpoint& bp)
                    {
                        return t < bp.timePPQ;
                    });
                v = (upper == snap->begin()
                         ? snap->front() : *(upper - 1)).normalizedValue;
            }
            else
                v = AutomationLane::evaluateAt (*snap, playheadPPQ);

            // Avoid churning version counters on float-precision noise.
            const float current = p.getNormalizedValue();
            if (std::abs (v - current) < 1.0e-6f)
                return;

            // Write to our atomic state (for UI dispatch, version tracking, etc.)
            // but do NOT forward to the bound plugin parameter here —
            // PluginInstanceCore::applyAutomationAtSample handles that with
            // proper per-sample smoothing in Phase 2.
            p.writeValue(v, ChangeSource::Automation);
        }

        AutomationParameterRegistry& registry;
        AutomationLaneStore&         lanes;
        AutomationModeState&         modeState;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationEvaluator)
    };
}
