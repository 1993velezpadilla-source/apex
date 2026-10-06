// ===========================================================================
// VoiceTransformUICore.h
// UI names and visual helpers for VOICE TRANSFORM / ULTRA DEMON.
// ===========================================================================
#pragma once
#include "VoiceTransformTypesCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{

class VoiceTransformUICore
{
public:
    static const char* presetName(VoiceTransformPreset preset) noexcept
    {
        return VoiceTransformPresetCore::presetName(preset);
    }

    static juce::Colour presetAccent(VoiceTransformPreset preset) noexcept
    {
        switch (preset)
        {
            case VoiceTransformPreset::Natural: return juce::Colour(0xFFBFC2C8);
            case VoiceTransformPreset::Dark:    return juce::Colour(0xFF2D3E74);
            case VoiceTransformPreset::Demon:   return juce::Colour(0xFFE3314F);
            case VoiceTransformPreset::Monster: return juce::Colour(0xFFFF6A1C);
            case VoiceTransformPreset::Abyss:   return juce::Colour(0xFF7B35FF);
            default:                            return juce::Colour(0xFFE3314F);
        }
    }

    static float visualIntensity(float demonAmount, float energy, bool reactiveMode) noexcept
    {
        const float pulse = reactiveMode ? juce::jlimit(0.f, 1.f, energy) : 0.f;
        return juce::jlimit(0.f, 1.f, demonAmount * 0.75f + pulse * 0.25f);
    }
};

} // namespace ArrangementEditor
