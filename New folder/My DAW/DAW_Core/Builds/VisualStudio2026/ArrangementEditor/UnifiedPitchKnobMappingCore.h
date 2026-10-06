// ===========================================================================
// UnifiedPitchKnobMappingCore.h
// One pitch knob nonlinear taper: fine near zero, accelerated near extremes.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include <algorithm>
#include <cmath>

namespace ArrangementEditor
{

class UnifiedPitchKnobMappingCore
{
public:
    static double knobPositionToSemitones(double position) noexcept
    {
        position = std::clamp(PitchScaleMathCore::sanitizeNaN(position, 0.5), 0.0, 1.0);
        const double signedNorm = (position - 0.5) * 2.0;
        const double shaped = std::copysign(std::pow(std::abs(signedNorm), 1.55), signedNorm);
        return shaped * PitchScaleMathCore::kUiMaxSemitones;
    }

    static double semitonesToKnobPosition(double semitones) noexcept
    {
        semitones = std::clamp(PitchScaleMathCore::sanitizeNaN(semitones),
                               PitchScaleMathCore::kUiMinSemitones,
                               PitchScaleMathCore::kUiMaxSemitones);
        const double signedNorm = semitones / PitchScaleMathCore::kUiMaxSemitones;
        const double unshaped = std::copysign(std::pow(std::abs(signedNorm), 1.0 / 1.55), signedNorm);
        return std::clamp(0.5 + unshaped * 0.5, 0.0, 1.0);
    }
};

} // namespace ArrangementEditor
