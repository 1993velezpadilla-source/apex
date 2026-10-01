#include "BubblegumCableAudioReactiveCore.h"
#include <JuceHeader.h>

namespace bubblegum
{
    void BubblegumCableAudioReactiveCore::reset()
    {
        states.clear();
    }

    BubblegumCableAudioReactiveCore::ReactiveOutput
    BubblegumCableAudioReactiveCore::updateForSend(
        SendId sendId,
        float targetEnergy01,
        float dt,
        float thicknessScaleMax,
        float flowSpeedScaleMax,
        float internalGlowBase,
        float internalGlowMaxAdd,
        float highlightBase,
        float highlightMaxAdd,
        float dropletChanceMaxAdd) noexcept
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, targetEnergy01);

        auto it = states.find(sendId);
        if (it == states.end())
        {
            // First frame: snap directly to target so the cable appears
            // at its correct animated state instantly. Prevents the
            // "knob being adjusted from 0" jitter on first appearance.
            states[sendId].smoothedEnergy01 = clamped;
        }

        auto& state = states[sendId];
        const float alpha = 1.0f - std::exp(-12.0f * dt);
        state.smoothedEnergy01 += (clamped - state.smoothedEnergy01) * alpha;

        ReactiveOutput out;
        out.energy01 = state.smoothedEnergy01;
        out.thicknessScale = 1.0f + state.smoothedEnergy01 * thicknessScaleMax;
        out.flowSpeedScale = 1.0f + state.smoothedEnergy01 * flowSpeedScaleMax;
        out.internalGlowAlpha = internalGlowBase + state.smoothedEnergy01 * internalGlowMaxAdd;
        out.highlightBoost = highlightBase + state.smoothedEnergy01 * highlightMaxAdd;
        out.dropletChanceScale = 1.0f + state.smoothedEnergy01 * dropletChanceMaxAdd;
        return out;
    }
}
