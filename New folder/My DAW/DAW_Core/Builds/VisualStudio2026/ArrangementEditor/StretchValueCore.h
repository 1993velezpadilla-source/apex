// ===========================================================================
// StretchValueCore.h
// Matemáticas puras de time stretch.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Maneja TODAS las conversiones de duración/stretch en un solo lugar.
//   Soporta tres formas de expresar el stretch:
//     1. Porcentaje (100% = original, 200% = doble, 50% = mitad)
//     2. Ratio directo (1.0, 2.0, 0.5)
//     3. BPM (sourceBPM / projectBPM)
//
// EXPLICACIÓN FÍSICA:
//   stretchRatio = newDuration / originalDuration
//
//   El stretch y el pitch son independientes solo cuando hay algoritmo
//   (phase vocoder, PSOLA, etc.).
//
//   En modo Resample NO hay separación:
//     playbackRate = pitchRatio
//     processedDuration = originalDuration / playbackRate
//
//   En modo Stretch:
//     el algoritmo separa los dos parámetros.
//     puedes tener stretchRatio=2.0 con pitchSemitones=0.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include <cmath>
#include <algorithm>
#include <string>

namespace ArrangementEditor
{

class StretchValueCore
{
public:
    // -----------------------------------------------------------------------
    // Conversión porcentaje → ratio
    //   100% → 1.0
    //   200% → 2.0
    //   50%  → 0.5
    // -----------------------------------------------------------------------
    static double percentToRatio(double percent)
    {
        return percent / 100.0;
    }

    // -----------------------------------------------------------------------
    // Conversión ratio → porcentaje
    // -----------------------------------------------------------------------
    static double ratioToPercent(double ratio)
    {
        return ratio * 100.0;
    }

    // -----------------------------------------------------------------------
    // BPM stretch: ajusta clip al tempo del proyecto
    //
    // Ejemplo:
    //   sourceBPM = 140 BPM (el loop es rápido)
    //   projectBPM = 120 BPM (el proyecto es lento)
    //   ratio = 140/120 = 1.1667
    //   El clip se estira 16.67% más para que encaje al tempo del proyecto.
    //
    // Lógica:
    //   Si el loop es más rápido que el proyecto → se estira (ratio > 1)
    //   Si el loop es más lento que el proyecto → se comprime (ratio < 1)
    // -----------------------------------------------------------------------
    static double bpmToRatio(double sourceBPM, double projectBPM)
    {
        if (sourceBPM <= 0.0 || projectBPM <= 0.0)
            return 1.0;
        return sourceBPM / projectBPM;
    }

    // -----------------------------------------------------------------------
    // Calcula duración procesada dado el source y el ratio
    //
    // stretchRatio = 1.0  → misma duración
    // stretchRatio = 2.0  → doble de duración (más lento)
    // stretchRatio = 0.5  → mitad de duración (más rápido)
    // -----------------------------------------------------------------------
    static double processedDuration(double sourceLength, double stretchRatio)
    {
        if (stretchRatio <= 0.0) return sourceLength;
        return sourceLength * stretchRatio;
    }

    // -----------------------------------------------------------------------
    // En modo Resample, el playback rate viene del pitch.
    // La duración cambia según ese rate.
    //
    // playbackRate = pitchRatio (desde PitchValueCore)
    // processedDuration = original / playbackRate
    //
    // Nota: ratio > 1 = más rápido = más corto = pitch más alto
    // -----------------------------------------------------------------------
    static double resampleDuration(double sourceLength, double pitchRatio)
    {
        if (pitchRatio <= 0.0) return sourceLength;
        return sourceLength / pitchRatio;
    }

    // -----------------------------------------------------------------------
    // Clamp al rango válido (25%–400%)
    // -----------------------------------------------------------------------
    static double clampRatio(double ratio)
    {
        return std::max(TimePitchConstants::kStretchMinRatio,
               std::min(TimePitchConstants::kStretchMaxRatio, ratio));
    }

    static double clampPercent(double percent)
    {
        return std::max(TimePitchConstants::kStretchMinPercent,
               std::min(TimePitchConstants::kStretchMaxPercent, percent));
    }

    // -----------------------------------------------------------------------
    // Verifica si el stretch está activo
    // -----------------------------------------------------------------------
    static bool isActive(double ratio)
    {
        return std::abs(ratio - 1.0) > 0.001;
    }

    // -----------------------------------------------------------------------
    // Formatea para display en UI
    //   1.0   → "100%"
    //   2.0   → "200%"
    //   0.5   → "50%"
    //   1.333 → "133.3%"
    // -----------------------------------------------------------------------
    static std::string formatPercent(double ratio)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f%%", ratio * 100.0);
        return std::string(buf);
    }

    static std::string formatBPM(double sourceBPM, double projectBPM)
    {
        char buf[64];
        if (sourceBPM <= 0.0)
            snprintf(buf, sizeof(buf), "BPM: --");
        else
            snprintf(buf, sizeof(buf), "%.1f -> %.1f BPM", sourceBPM, projectBPM);
        return std::string(buf);
    }
};

} // namespace ArrangementEditor
