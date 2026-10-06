// ===========================================================================
// TimePitchPresetsCore.h
// Presets predefinidos de pitch / time stretch.
//
// PRESETS:
//   Vocal Clean   — PitchOnly, +0 st, preserve formants, no stretch
//   Demon         — Resample, -12 st (voz de demonio, cinta lenta)
//   Chipmunk      — Resample, +12 st (ardilla, cinta rápida)
//   Slowed        — Stretch, -3 st, 130% stretch (slowed+reverb aesthetic)
//   Nightcore     — Resample, +4 st (más rápido y agudo)
//   Tape Slow     — Resample, -7 st (cassette desgastado)
//   Texture Cloud — Texture, 300% stretch, grain jitter max
//
// FLUJO:
//   1. User selecciona preset en dropdown.
//   2. UI llama TimePitchPresetsCore::applyPreset(name, model).
//   3. model.timePitch se actualiza con los parámetros del preset.
//   4. UI crea TimePitchSetStateAction (undo/redo).
//   5. DSPCore recibe el nuevo estado.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "ArrangementClipModel.h"
#include <array>
#include <string>

namespace ArrangementEditor
{

struct TimePitchPreset
{
    const char*    name;
    const char*    description;
    TimePitchState state;
};

class TimePitchPresetsCore
{
public:
    // -----------------------------------------------------------------------
    // All built-in presets
    // -----------------------------------------------------------------------
    static std::array<TimePitchPreset, 7> allPresets()
    {
        std::array<TimePitchPreset, 7> presets;

        // 0 — Vocal Clean
        presets[0].name        = "Vocal Clean";
        presets[0].description = "Pitch shift only, preserve formants. For clean vocal tuning.";
        presets[0].state.mode             = TimePitchMode::Vocal;
        presets[0].state.pitchSemitones   = 0.0;
        presets[0].state.stretchRatio     = 1.0;
        presets[0].state.preserveFormants = true;
        presets[0].state.formantSemitones = 0.0;

        // 1 — Demon
        presets[1].name        = "Demon";
        presets[1].description = "Tape mode -12st. Pitch and duration change together.";
        presets[1].state.mode           = TimePitchMode::Resample;
        presets[1].state.pitchSemitones = -12.0;
        presets[1].state.stretchRatio   = 1.0;

        // 2 — Chipmunk
        presets[2].name        = "Chipmunk";
        presets[2].description = "Tape mode +12st. Faster and higher pitched.";
        presets[2].state.mode           = TimePitchMode::Resample;
        presets[2].state.pitchSemitones = 12.0;
        presets[2].state.stretchRatio   = 1.0;

        // 3 — Slowed
        presets[3].name        = "Slowed";
        presets[3].description = "Slowed+reverb aesthetic: -3st, 130% stretch.";
        presets[3].state.mode           = TimePitchMode::Stretch;
        presets[3].state.pitchSemitones = -3.0;
        presets[3].state.stretchRatio   = 1.30;

        // 4 — Nightcore
        presets[4].name        = "Nightcore";
        presets[4].description = "Tape mode +4st. Faster tempo, higher pitch.";
        presets[4].state.mode           = TimePitchMode::Resample;
        presets[4].state.pitchSemitones = 4.0;
        presets[4].state.stretchRatio   = 1.0;

        // 5 — Tape Slow
        presets[5].name        = "Tape Slow";
        presets[5].description = "Worn cassette: -7st, tape warble feel.";
        presets[5].state.mode           = TimePitchMode::Resample;
        presets[5].state.pitchSemitones = -7.0;
        presets[5].state.stretchRatio   = 1.0;

        // 6 — Texture Cloud
        presets[6].name        = "Texture Cloud";
        presets[6].description = "Granular 300% stretch with maximum jitter. Experimental.";
        presets[6].state.mode         = TimePitchMode::Texture;
        presets[6].state.stretchRatio = 3.0;
        presets[6].state.pitchSemitones = 0.0;

        return presets;
    }

    // -----------------------------------------------------------------------
    // Find preset by name (case-sensitive)
    // Returns nullptr if not found.
    // -----------------------------------------------------------------------
    static const TimePitchPreset* findPreset(const std::string& name)
    {
        static const auto presets = allPresets();
        for (const auto& p : presets)
            if (p.name == name)
                return &p;
        return nullptr;
    }

    // -----------------------------------------------------------------------
    // Apply preset to clip model (UI thread).
    // Returns old state for undo action creation.
    // -----------------------------------------------------------------------
    static TimePitchState applyPreset(const std::string& name,
                                       ArrangementClipModel& model)
    {
        const TimePitchState oldState = model.timePitch;
        if (const auto* p = findPreset(name))
            model.timePitch = p->state;
        return oldState;
    }

    // Preset names only (for combo box)
    static std::array<const char*, 7> allNames()
    {
        return {
            "Vocal Clean", "Demon", "Chipmunk",
            "Slowed", "Nightcore", "Tape Slow", "Texture Cloud"
        };
    }
};

} // namespace ArrangementEditor
