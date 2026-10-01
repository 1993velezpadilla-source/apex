// ===========================================================================
// TimePitchUICore.h
// Modelo de datos para la UI del sistema pitch/time stretch.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Solo datos de UI: nombres de modos, labels, colores de estado.
//   NUNCA toca DSP ni audio thread.
//   El UI thread puede leer esto sin locks.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include <string>
#include <array>

namespace ArrangementEditor
{

class TimePitchUICore
{
public:
    // -----------------------------------------------------------------------
    // Nombres de modos para dropdown
    // -----------------------------------------------------------------------
    static const char* modeName(TimePitchMode mode)
    {
        switch (mode)
        {
            case TimePitchMode::Resample:   return "Resample / Tape";
            case TimePitchMode::Stretch:    return "Time Stretch";
            case TimePitchMode::PitchOnly:  return "Pitch Only";
            case TimePitchMode::Vocal:      return "Vocal / Independent";
            case TimePitchMode::Percussion: return "Percussion";
            case TimePitchMode::Texture:    return "Texture";
            case TimePitchMode::OfflineHQ:  return "Offline HQ";
            default:                        return "Vocal / Independent";
        }
    }

    // -----------------------------------------------------------------------
    // Descripción corta de cada modo (tooltip)
    // -----------------------------------------------------------------------
    static const char* modeDescription(TimePitchMode mode)
    {
        return modeTooltip(mode);
    }

    static const char* modeTooltip(TimePitchMode mode)
    {
        switch (mode)
        {
            case TimePitchMode::Resample:
                return "Tape mode: pitch and time are linked through playback speed.";
            case TimePitchMode::Stretch:
                return "Stretch mode changes duration while preserving pitch.";
            case TimePitchMode::PitchOnly:
                return "Pitch mode changes pitch while preserving duration.";
            case TimePitchMode::Vocal:
                return "Vocal mode allows independent pitch and time control.";
            case TimePitchMode::Percussion:
                return "Time processing optimized for transients and drums.";
            case TimePitchMode::Texture:
                return "Granular/texture time-pitch processing.";
            case TimePitchMode::OfflineHQ:
                return "High quality offline time-pitch processing.";
            default:
                return "Vocal mode allows independent pitch and time control.";
        }
    }

    // -----------------------------------------------------------------------
    // Devuelve todos los nombres de modos como array (para combo box)
    // -----------------------------------------------------------------------
    static std::array<const char*, kTimePitchModeCount> allModeNames()
    {
        return {
            "Resample / Tape",
            "Time Stretch",
            "Pitch Only",
            "Vocal / Independent",
            "Percussion",
            "Texture",
            "Offline HQ"
        };
    }

    // -----------------------------------------------------------------------
    // Indica si el knob Formant debe estar habilitado para este modo
    // -----------------------------------------------------------------------
    static bool formantKnobEnabled(TimePitchMode mode)
    {
        return mode == TimePitchMode::Vocal || mode == TimePitchMode::PitchOnly;
    }

    // -----------------------------------------------------------------------
    // Indica si el knob Stretch debe estar habilitado para este modo
    // En Resample, el stretch lo controla el pitch, no un knob separado.
    // -----------------------------------------------------------------------
    static bool stretchKnobEnabled(TimePitchMode mode)
    {
        switch (mode)
        {
            case TimePitchMode::PitchOnly:
                return false;
            default:
                return true;
        }
    }

    // -----------------------------------------------------------------------
    // Tooltip del pitch knob según el modo actual
    // -----------------------------------------------------------------------
    static std::string pitchKnobTooltip(TimePitchMode mode)
    {
        if (mode == TimePitchMode::Resample)
            return "Tape mode: pitch and time are linked.";
        if (mode == TimePitchMode::Stretch)
            return "Stretch mode changes duration while preserving pitch.";
        if (mode == TimePitchMode::Vocal)
            return "Independent vocal pitch shift with optional formant handling.";
        return "Independent pitch shift.";
    }

    // -----------------------------------------------------------------------
    // Indica si el estado tiene algo activo (para mostrar badge o indicador)
    // -----------------------------------------------------------------------
    static bool hasActiveProcessing(const TimePitchState& state)
    {
        return state.isPitchActive() || state.isStretchActive() || state.isFormantActive();
    }

    // -----------------------------------------------------------------------
    // Texto corto de estado para badge sobre el clip
    // -----------------------------------------------------------------------
    static std::string clipBadgeText(const TimePitchState& state)
    {
        if (state.isIdentity()) return "";

        std::string text;
        if (state.isPitchActive())
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%+.1fst", state.totalPitchSemitones());
            text += buf;
        }
        if (state.isStretchActive())
        {
            if (!text.empty()) text += " ";
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", state.stretchRatio * 100.0);
            text += buf;
        }
        return text;
    }
};

} // namespace ArrangementEditor
