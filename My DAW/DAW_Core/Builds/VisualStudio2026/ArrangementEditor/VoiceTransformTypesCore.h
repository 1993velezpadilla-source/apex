// ===========================================================================
// VoiceTransformTypesCore.h
// One-knob vocal transformation state + preset profiles.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <array>

namespace ArrangementEditor
{

enum class VoiceTransformPreset
{
    Natural = 0,
    Dark,
    Demon,
    Monster,
    Abyss
};

static constexpr int kVoiceTransformPresetCount = 5;

struct VoiceTransformState
{
    VoiceTransformPreset preset = VoiceTransformPreset::Demon;
    float demonAmount = 0.f;       // 0..1, UI label: ULTRA DEMON
    bool reactiveMode = true;

    bool isActive() const noexcept
    {
        return demonAmount > 0.0001f;
    }
};

struct VoiceTransformPresetProfile
{
    const char* name = "DEMON";
    float pitchMaxSemitones = -12.f;
    float formantMax = -5.f;
    float lowBoostMaxDb = 5.f;
    float highCutMaxDb = -6.f;
    float saturationMax = 0.18f;
    float subThreshold = 0.70f;
    float subMax = 0.55f;
    float stereoWidthMax = 0.10f;
    float transientShapeMax = 0.35f;
    float reactiveDepth = 1.f;
};

struct VoiceTransformTargets
{
    float effectiveAmount = 0.f;
    float pitchSemitones = 0.f;
    float formantShift = 0.f;
    float lowBoostDb = 0.f;
    float highCutDb = 0.f;
    float saturation = 0.f;
    float subAmount = 0.f;
    float stereoWidth = 1.f;
    float transientShape = 0.f;
};

class VoiceTransformPresetCore
{
public:
    static const VoiceTransformPresetProfile& profile(VoiceTransformPreset preset) noexcept
    {
        static constexpr std::array<VoiceTransformPresetProfile, kVoiceTransformPresetCount> profiles {{
            { "NATURAL", -3.f,  -1.5f, 1.5f,  -1.5f, 0.05f, 1.01f, 0.00f, 0.00f, 0.05f, 0.25f },
            { "DARK",    -9.f,  -4.5f, 4.5f,  -6.0f, 0.12f, 0.75f, 0.25f, 0.00f, 0.18f, 0.70f },
            { "DEMON",   -18.f, -7.5f, 6.5f,  -9.0f, 0.22f, 0.58f, 0.75f, 0.12f, 0.42f, 1.15f },
            { "MONSTER", -22.f, -10.f,  8.0f, -11.0f, 0.28f, 0.28f, 0.90f, 0.10f, 0.70f, 1.30f },
            { "ABYSS",   -36.f, -18.f, 10.5f, -18.0f, 0.32f, 0.24f, 1.00f, 0.08f, 0.70f, 1.65f }
        }};

        const int index = juce::jlimit(0, kVoiceTransformPresetCount - 1, static_cast<int>(preset));
        return profiles[(size_t) index];
    }

    static const char* presetName(VoiceTransformPreset preset) noexcept
    {
        return profile(preset).name;
    }
};

} // namespace ArrangementEditor
