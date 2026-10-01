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

            registry.forEachRT ([this, playheadPPQ] (AutomationParameter& p)
            {
                evaluateOne (p, playheadPPQ);
            });
        }

        void onTransportStop()
        {
            modeState.clearAllLatches();
        }

    private:
        void evaluateOne (AutomationParameter& p, double playheadPPQ) noexcept
        {
            const auto id = p.getID();

            if (! modeState.isReadingRT (id))
                return;

            // User is holding this control -- their value wins.
            if (p.isGestureActive())
                return;

            auto lane = lanes.findLaneRT (id);
            if (lane == nullptr) return;

            auto snap = lane->getSnapshot();
            if (snap == nullptr || snap->empty()) return;

            const float v = AutomationLane::evaluateAt (*snap, playheadPPQ);

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
