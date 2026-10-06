// ===========================================================================
// PitchValueCore.h
// Matemáticas puras de conversión de pitch.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Todas las fórmulas de pitch viven aquí.
//   Si mañana cambias la escala o el snapping, solo tocas este archivo.
//   Sin dependencias de JUCE ni de UI.
//
// EXPLICACIÓN DE LA FÍSICA:
//   En audio digital, pitch = frecuencia de reproducción.
//   La relación es exponencial (escala logarítmica musical):
//
//     ratio = pow(2, semitones / 12)
//
//   Ejemplos reales:
//     -24 st = ratio 0.25 → frecuencia 1/4 → dos octavas abajo (demonio)
//     -12 st = ratio 0.5  → frecuencia mitad → una octava abajo (voz gruesa)
//       0 st = ratio 1.0  → sin cambio
//     +12 st = ratio 2.0  → frecuencia doble → una octava arriba (ardilla)
//     +24 st = ratio 4.0  → frecuencia 4x → dos octavas arriba (chipmunk)
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "PitchScaleMathCore.h"
#include <cmath>
#include <algorithm>
#include <string>

namespace ArrangementEditor
{

class PitchValueCore
{
public:
    // -----------------------------------------------------------------------
    // Conversión principal: semitonos → ratio de frecuencia
    // Esta es la fórmula base que usan TODOS los DAWs internamente.
    // -----------------------------------------------------------------------
    static double semitonesToRatio(double semitones)
    {
        return PitchScaleMathCore::semitonesToRatio(semitones);
    }

    // -----------------------------------------------------------------------
    // Conversión inversa: ratio → semitonos
    // -----------------------------------------------------------------------
    static double ratioToSemitones(double ratio)
    {
        return PitchScaleMathCore::ratioToSemitones(ratio);
    }

    // -----------------------------------------------------------------------
    // Pitch + fine tune combinados
    // Ejemplo: +7 st + 50 cents = +7.5 st = ratio 1.4983
    // -----------------------------------------------------------------------
    static double pitchToRatio(double semitones, double cents)
    {
        const double total = semitones + PitchScaleMathCore::centsToSemitones(cents);
        return PitchScaleMathCore::semitonesToRatio(total);
    }

    static double clampAudibleSemitones(double semitones)
    {
        return std::max(TimePitchConstants::kPitchAudibleMinSemitones,
               std::min(TimePitchConstants::kPitchAudibleMaxSemitones, semitones));
    }

    static double pitchToAudibleRatio(double semitones, double cents)
    {
        const double total = clampAudibleSemitones(semitones + PitchScaleMathCore::centsToSemitones(cents));
        return PitchScaleMathCore::semitonesToRatio(total);
    }

    // -----------------------------------------------------------------------
    // Desde TimePitchState completo
    // -----------------------------------------------------------------------
    static double ratioFromState(const TimePitchState& state)
    {
        return pitchToRatio(state.pitchSemitones, state.fineTuneCents);
    }

    // -----------------------------------------------------------------------
    // Clamp al rango válido
    // -----------------------------------------------------------------------
    static double clampSemitones(double semitones)
    {
        return PitchScaleMathCore::clampPitch(semitones, true);
    }

    static double clampCents(double cents)
    {
        return std::max(TimePitchConstants::kFineTuneMinCents,
               std::min(TimePitchConstants::kFineTuneMaxCents, cents));
    }

    // -----------------------------------------------------------------------
    // Snap al semitono más cercano (para modo musical)
    // -----------------------------------------------------------------------
    static double snapToSemitone(double semitones)
    {
        return std::round(semitones);
    }

    // -----------------------------------------------------------------------
    // Verifica si el pitch está activo (diferente de 0)
    // -----------------------------------------------------------------------
    static bool isActive(double semitones, double cents = 0.0)
    {
        return std::abs(semitones) > 0.001 || std::abs(cents) > 0.1;
    }

    // -----------------------------------------------------------------------
    // Formatea para display en UI
    //   0      → "0.00 st"
    //   +7.5   → "+7.50 st"
    //   -12.0  → "-12.00 st"
    // -----------------------------------------------------------------------
    static std::string formatSemitones(double semitones)
    {
        char buf[32];
        if (std::abs(semitones) < 0.001)
            snprintf(buf, sizeof(buf), "0.00 st");
        else
            snprintf(buf, sizeof(buf), "%+.2f st", semitones);
        return std::string(buf);
    }

    static std::string formatCents(double cents)
    {
        char buf[32];
        if (std::abs(cents) < 0.1)
            snprintf(buf, sizeof(buf), "0 ct");
        else
            snprintf(buf, sizeof(buf), "%+.0f ct", cents);
        return std::string(buf);
    }

    static std::string formatRatio(double ratio)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "x%.4f", ratio);
        return std::string(buf);
    }
};

} // namespace ArrangementEditor
