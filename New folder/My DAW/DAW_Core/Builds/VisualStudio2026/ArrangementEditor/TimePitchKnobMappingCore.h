// ===========================================================================
// TimePitchKnobMappingCore.h
// Conversión de valores de knob UI → valores DSP.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Los knobs de UI no siempre tienen escala lineal.
//   El time stretch, por ejemplo, necesita escala logarítmica/exponencial
//   para que 50%/100%/200% se sientan naturales al girar el knob.
//
//   Este núcleo convierte:
//     knobPosition (0.0..1.0) → valor DSP real
//     valor DSP real → knobPosition (para mostrar en UI)
//
//   Pitch knob: escala lineal (semitonos directos)
//   Stretch knob: escala logarítmica (25%..400%)
//   Formant knob: escala lineal
//   Fine tune: lineal (cents directos)
//
//   NUNCA dibuja UI — solo convierte números.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "UnifiedPitchKnobMappingCore.h"
#include <cmath>
#include <algorithm>

namespace ArrangementEditor
{

class TimePitchKnobMappingCore
{
public:
    // -----------------------------------------------------------------------
    // PITCH KNOB — escala lineal
    // knobPos 0.0 → min st
    // knobPos 0.5 → 0 st
    // knobPos 1.0 → max st
    // -----------------------------------------------------------------------
    static double pitchKnobToSemitones(double knobPos)
    {
        return UnifiedPitchKnobMappingCore::knobPositionToSemitones(knobPos);
    }

    static double semitonesToPitchKnob(double semitones)
    {
        return UnifiedPitchKnobMappingCore::semitonesToKnobPosition(semitones);
    }

    // -----------------------------------------------------------------------
    // STRETCH KNOB — escala logarítmica
    //
    // Por qué log: si usamos escala lineal entre 25% y 400%,
    // el centro del knob sería 212% que no es útil.
    // Con log, el centro del knob = 100% (original), lo que es natural.
    //
    // Rango: 25% → 400% (en ratio: 0.25 → 4.0)
    // knobPos 0.0 → ratio 0.25 (25%)
    // knobPos 0.5 → ratio 1.0  (100%)
    // knobPos 1.0 → ratio 4.0  (400%)
    // -----------------------------------------------------------------------
    static double stretchKnobToRatio(double knobPos)
    {
        const double logMin = std::log2(TimePitchConstants::kStretchMinRatio); // -2
        const double logMax = std::log2(TimePitchConstants::kStretchMaxRatio); // +2
        const double logVal = logMin + knobPos * (logMax - logMin);
        return std::pow(2.0, logVal);
    }

    static double stretchRatioToKnob(double ratio)
    {
        if (ratio <= 0.0) return 0.5;
        const double logMin = std::log2(TimePitchConstants::kStretchMinRatio);
        const double logMax = std::log2(TimePitchConstants::kStretchMaxRatio);
        const double logVal = std::log2(ratio);
        const double pos = (logVal - logMin) / (logMax - logMin);
        return std::max(0.0, std::min(1.0, pos));
    }

    // -----------------------------------------------------------------------
    // FORMANT KNOB — escala lineal
    // knobPos 0.0 → -12 st
    // knobPos 0.5 → 0 st
    // knobPos 1.0 → +12 st
    // -----------------------------------------------------------------------
    static double formantKnobToSemitones(double knobPos)
    {
        const double range = TimePitchConstants::kFormantMaxSemitones
                           - TimePitchConstants::kFormantMinSemitones;
        return TimePitchConstants::kFormantMinSemitones + knobPos * range;
    }

    static double semitonesToFormantKnob(double semitones)
    {
        const double range = TimePitchConstants::kFormantMaxSemitones
                           - TimePitchConstants::kFormantMinSemitones;
        if (range <= 0.0) return 0.5;
        const double pos = (semitones - TimePitchConstants::kFormantMinSemitones) / range;
        return std::max(0.0, std::min(1.0, pos));
    }

    // -----------------------------------------------------------------------
    // FINE TUNE KNOB — lineal, cents
    // knobPos 0.0 → -100 ct
    // knobPos 0.5 → 0 ct
    // knobPos 1.0 → +100 ct
    // -----------------------------------------------------------------------
    static double fineTuneKnobToCents(double knobPos)
    {
        return TimePitchConstants::kFineTuneMinCents
               + knobPos * (TimePitchConstants::kFineTuneMaxCents
                            - TimePitchConstants::kFineTuneMinCents);
    }

    static double centsToFineTuneKnob(double cents)
    {
        const double range = TimePitchConstants::kFineTuneMaxCents
                           - TimePitchConstants::kFineTuneMinCents;
        if (range <= 0.0) return 0.5;
        const double pos = (cents - TimePitchConstants::kFineTuneMinCents) / range;
        return std::max(0.0, std::min(1.0, pos));
    }
};

} // namespace ArrangementEditor
