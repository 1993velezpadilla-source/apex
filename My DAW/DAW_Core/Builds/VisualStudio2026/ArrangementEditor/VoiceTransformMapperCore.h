// ===========================================================================
// VoiceTransformMapperCore.h
// Converts preset + ULTRA DEMON amount + audio energy into hidden DSP targets.
// ===========================================================================
#pragma once
#include "VoiceTransformTypesCore.h"
#include <cmath>

namespace ArrangementEditor
{

class VoiceTransformMapperCore
{
public:
    static VoiceTransformTargets map(const VoiceTransformState& state, float energy) noexcept
    {
        const auto& p = VoiceTransformPresetCore::profile(state.preset);
        const float amount = juce::jlimit(0.f, 1.f, state.demonAmount);
        const float e = juce::jlimit(0.f, 1.f, energy);
        const float curved = smoothstep(amount);
        const float effective = state.reactiveMode ? curved * (0.6f + 0.4f * e) : curved;
        const float reactivePush = state.reactiveMode ? smoothstep(0.65f, 0.85f, e) : 0.f;

        VoiceTransformTargets t;
        t.effectiveAmount = effective;
        t.pitchSemitones = p.pitchMaxSemitones * effective - 2.f * reactivePush * p.reactiveDepth;
        t.formantShift = p.formantMax * effective - reactivePush * p.reactiveDepth;
        t.lowBoostDb = p.lowBoostMaxDb * std::pow(effective, 0.75f);
        t.highCutDb = p.highCutMaxDb * std::pow(effective, 1.15f);
        t.saturation = juce::jlimit(0.f, 0.35f, p.saturationMax * std::pow(effective, 1.4f) + 0.05f * reactivePush);

        const float subFade = smoothstep(p.subThreshold, 1.f, effective);
        t.subAmount = juce::jlimit(0.f, 1.f, p.subMax * subFade + reactivePush * 0.15f);
        t.lowBoostDb *= 1.f - 0.25f * t.subAmount;

        t.stereoWidth = juce::jlimit(1.f, 1.35f, 1.f + p.stereoWidthMax * smoothstep(0.65f, 1.f, effective));
        t.transientShape = p.transientShapeMax * effective;
        return t;
    }

private:
    static float smoothstep(float x) noexcept
    {
        x = juce::jlimit(0.f, 1.f, x);
        return x * x * (3.f - 2.f * x);
    }

    static float smoothstep(float edge0, float edge1, float x) noexcept
    {
        if (edge1 <= edge0)
            return x >= edge1 ? 1.f : 0.f;

        x = juce::jlimit(0.f, 1.f, (x - edge0) / (edge1 - edge0));
        return x * x * (3.f - 2.f * x);
    }
};

} // namespace ArrangementEditor
