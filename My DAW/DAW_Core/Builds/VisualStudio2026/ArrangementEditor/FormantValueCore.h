// ===========================================================================
// FormantValueCore.h
// Lógica de formant shifting para modo vocal.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Los formants son las resonancias del tracto vocal humano.
//   Cuando haces pitch shift sin preservar formants:
//     - Pitch alto → ardilla (todos los formants suben con el pitch)
//     - Pitch bajo → demonio (todos los formants bajan con el pitch)
//
//   Con preservación de formants:
//     - Solo la nota musical cambia
//     - El timbre y la identidad de la voz se mantienen
//     - Esto es lo que hace AutoTune, Melodyne, y el motor vocal de FL Studio
//
//   Este núcleo permite:
//     1. Separar el formant del pitch
//     2. Mover el formant independientemente
//     3. Link/unlink formant con el pitch (para efecto ardilla intencional)
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include <cmath>
#include <string>

namespace ArrangementEditor
{

class FormantValueCore
{
public:
    // -----------------------------------------------------------------------
    // Formant shift en semitonos → ratio de frecuencia
    // Misma fórmula que el pitch pero para los formants
    // -----------------------------------------------------------------------
    static double semitonesToRatio(double semitones)
    {
        return std::pow(2.0, semitones / 12.0);
    }

    // -----------------------------------------------------------------------
    // Calcula el formant shift efectivo.
    //
    // Modos:
    //   preserveFormants = true  → formantShift compensa el pitch shift
    //                              La identidad vocal se mantiene.
    //
    //   preserveFormants = false → formantShift sigue al pitch
    //                              Efecto ardilla/demonio natural.
    //
    //   linked = true (override) → formant offset = 0, sigue pitch puro
    //   linked = false           → formant offset = formantSemitones manual
    // -----------------------------------------------------------------------
    static double effectiveFormantSemitones(const TimePitchState& state,
                                             bool linkedToPitch = false)
    {
        if (linkedToPitch)
        {
            // Formant sigue al pitch: ardilla/demonio intencional
            return state.pitchSemitones;
        }

        if (state.preserveFormants)
        {
            // Preservar: el formant manual + compensación inversa del pitch
            // Resultado: la voz se mantiene en su timbre original
            return state.formantSemitones - state.pitchSemitones;
        }

        // Sin preservación: solo el valor manual del knob formant
        return state.formantSemitones;
    }

    // -----------------------------------------------------------------------
    // Clamp al rango válido
    // -----------------------------------------------------------------------
    static double clamp(double semitones)
    {
        return std::max(TimePitchConstants::kFormantMinSemitones,
               std::min(TimePitchConstants::kFormantMaxSemitones, semitones));
    }

    // -----------------------------------------------------------------------
    // Verifica si el formant shift está activo
    // -----------------------------------------------------------------------
    static bool isActive(double semitones)
    {
        return std::abs(semitones) > 0.001;
    }

    // -----------------------------------------------------------------------
    // Formatea para display en UI
    // -----------------------------------------------------------------------
    static std::string format(double semitones)
    {
        char buf[32];
        if (std::abs(semitones) < 0.001)
            snprintf(buf, sizeof(buf), "0.0 fm");
        else
            snprintf(buf, sizeof(buf), "%+.1f fm", semitones);
        return std::string(buf);
    }

    // -----------------------------------------------------------------------
    // Describe el efecto vocal en palabras (útil para tooltip de UI)
    // -----------------------------------------------------------------------
    static std::string describeEffect(const TimePitchState& state)
    {
        const double pitch = state.pitchSemitones;

        if (state.mode != TimePitchMode::Vocal && state.mode != TimePitchMode::PitchOnly)
            return "";

        if (!state.preserveFormants)
        {
            if (pitch > 4.0)  return "Chipmunk / Ardilla";
            if (pitch > 0.0)  return "Voz aguda";
            if (pitch < -4.0) return "Demonio / Voz gruesa";
            if (pitch < 0.0)  return "Voz grave";
            return "Original";
        }
        else
        {
            if (pitch > 0.0)  return "Pitch arriba (formants preservados)";
            if (pitch < 0.0)  return "Pitch abajo (formants preservados)";
            return "Original";
        }
    }
};

} // namespace ArrangementEditor
