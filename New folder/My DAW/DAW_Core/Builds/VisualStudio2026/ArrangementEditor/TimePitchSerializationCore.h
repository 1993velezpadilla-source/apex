// ===========================================================================
// TimePitchSerializationCore.h
// Serialización y deserialización del TimePitchState.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   El TimePitchState debe sobrevivir a:
//     - Save/load del proyecto
//     - Split de clips (cada mitad hereda el estado)
//     - Slip editing (no destruye el estado)
//     - Undo/redo futuro
//
//   Usar JUCE ValueTree para guardar en XML/binary igual que el resto del DAW.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "UnifiedPitchStateCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{

class TimePitchSerializationCore
{
public:
    // -----------------------------------------------------------------------
    // Identificadores de propiedades en el ValueTree
    // -----------------------------------------------------------------------
    static inline const juce::Identifier kTreeType         { "TimePitchState" };
    static inline const juce::Identifier kPropPitch        { "pitchSemitones" };
    static inline const juce::Identifier kPropFineTune     { "fineTuneCents" };
    static inline const juce::Identifier kPropStretch      { "stretchRatio" };
    static inline const juce::Identifier kPropFormant      { "formantSemitones" };
    static inline const juce::Identifier kPropMode         { "mode" };
    static inline const juce::Identifier kPropPreserve     { "preserveFormants" };
    static inline const juce::Identifier kPropTransient    { "transientPreserve" };
    static inline const juce::Identifier kPropHighQuality  { "highQuality" };
    static inline const juce::Identifier kPropSampleRate   { "sourceSampleRate" };
    static inline const juce::Identifier kPropSourceBPM    { "sourceBPM" };
    static inline const juce::Identifier kPropProjectBPM   { "projectBPM" };
    static inline const juce::Identifier kPropVoicePreset  { "voiceTransformPreset" };
    static inline const juce::Identifier kPropVoiceAmount  { "voiceTransformAmount" };
    static inline const juce::Identifier kPropVoiceReactive { "voiceTransformReactive" };
    static inline const juce::Identifier kPropPitchEngineVersion { "pitchEngineVersion" };

    // -----------------------------------------------------------------------
    // Guarda el estado en un ValueTree
    // -----------------------------------------------------------------------
    static juce::ValueTree save(const TimePitchState& state)
    {
        juce::ValueTree tree(kTreeType);
        tree.setProperty(kPropPitchEngineVersion, 1, nullptr);
        tree.setProperty(kPropPitch,       state.pitchSemitones,  nullptr);
        tree.setProperty(kPropFineTune,    state.fineTuneCents,   nullptr);
        tree.setProperty(kPropStretch,     state.stretchRatio,    nullptr);
        tree.setProperty(kPropFormant,     state.formantSemitones,nullptr);
        tree.setProperty(kPropMode,        static_cast<int>(state.mode), nullptr);
        tree.setProperty(kPropPreserve,    state.preserveFormants,nullptr);
        tree.setProperty(kPropTransient,   state.transientPreserve, nullptr);
        tree.setProperty(kPropHighQuality, state.highQuality,     nullptr);
        tree.setProperty(kPropSampleRate,  state.sourceSampleRate,nullptr);
        tree.setProperty(kPropSourceBPM,   state.sourceBPM,       nullptr);
        tree.setProperty(kPropProjectBPM,  state.projectBPM,      nullptr);
        tree.setProperty(kPropVoicePreset, static_cast<int>(state.voiceTransform.preset), nullptr);
        tree.setProperty(kPropVoiceAmount, state.voiceTransform.demonAmount, nullptr);
        tree.setProperty(kPropVoiceReactive, state.voiceTransform.reactiveMode, nullptr);
        return tree;
    }

    // -----------------------------------------------------------------------
    // Carga el estado desde un ValueTree
    // Valores faltantes usan defaults seguros (sin cambio).
    // -----------------------------------------------------------------------
    static TimePitchState load(const juce::ValueTree& tree)
    {
        TimePitchState state;
        if (!tree.isValid() || tree.getType() != kTreeType)
            return state; // devuelve defaults seguros

        const int pitchVersion = tree.hasProperty(kPropPitchEngineVersion)
            ? static_cast<int>(tree.getProperty(kPropPitchEngineVersion, 1)) : 0;
        state.pitchEngineVersion = 1;
        state.pitchSemitones   = UnifiedPitchStateCore::migrateLegacyPitchValue(
            static_cast<double>(tree.getProperty(kPropPitch, 0.0)), pitchVersion);
        state.fineTuneCents    = static_cast<double>(tree.getProperty(kPropFineTune,   0.0));
        state.stretchRatio     = static_cast<double>(tree.getProperty(kPropStretch,    1.0));
        state.formantSemitones = static_cast<double>(tree.getProperty(kPropFormant,    0.0));
        state.preserveFormants = static_cast<bool>(tree.getProperty(kPropPreserve,     false));
        state.transientPreserve= static_cast<bool>(tree.getProperty(kPropTransient,    false));
        state.highQuality      = static_cast<bool>(tree.getProperty(kPropHighQuality,  false));
        state.sourceSampleRate = static_cast<int>(tree.getProperty(kPropSampleRate,    44100));
        state.sourceBPM        = static_cast<double>(tree.getProperty(kPropSourceBPM,  0.0));
        state.projectBPM       = static_cast<double>(tree.getProperty(kPropProjectBPM, 120.0));
        state.voiceTransform.demonAmount = juce::jlimit(0.f, 1.f,
            static_cast<float>(tree.getProperty(kPropVoiceAmount, 0.0f)));
        state.voiceTransform.reactiveMode = static_cast<bool>(tree.getProperty(kPropVoiceReactive, true));

        const int modeInt = static_cast<int>(tree.getProperty(kPropMode, static_cast<int>(TimePitchMode::Vocal)));
        state.mode = static_cast<TimePitchMode>(
            juce::jlimit(0, kTimePitchModeCount - 1, modeInt));

        const int voicePreset = static_cast<int>(tree.getProperty(kPropVoicePreset, static_cast<int>(VoiceTransformPreset::Demon)));
        state.voiceTransform.preset = static_cast<VoiceTransformPreset>(
            juce::jlimit(0, kVoiceTransformPresetCount - 1, voicePreset));

        return state;
    }

    // -----------------------------------------------------------------------
    // Herencia de estado al hacer split de un clip.
    // El estado se copia completo a ambas mitades.
    // Solo la información de source start/end cambia — eso está en el clip.
    // -----------------------------------------------------------------------
    static TimePitchState inheritForSplit(const TimePitchState& original)
    {
        // Copia directa: el estado es el mismo para ambas mitades.
        // Las mitades tienen diferente sourceStartSample/sourceEndSample
        // pero el pitch/stretch/formant/mode es idéntico.
        return original;
    }
};

} // namespace ArrangementEditor
