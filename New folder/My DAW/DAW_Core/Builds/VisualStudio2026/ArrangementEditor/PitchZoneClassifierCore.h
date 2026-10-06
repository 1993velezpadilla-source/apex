// ===========================================================================
// PitchZoneClassifierCore.h
// Smooth one-knob pitch zone classification.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include <algorithm>
#include <cmath>

namespace ArrangementEditor
{

enum class UnifiedPitchZone
{
    Clean = 0,
    MusicalLowHigh,
    DarkExtreme,
    ChipmunkExtreme
};

struct PitchZoneSnapshot
{
    UnifiedPitchZone zone = UnifiedPitchZone::Clean;
    double darkIntensity = 0.0;
    double chipIntensity = 0.0;
    double extremeIntensity = 0.0;
    double musicalIntensity = 1.0;
};

class PitchZoneClassifierCore
{
public:
    static PitchZoneSnapshot classify(double pitchSemitones) noexcept
    {
        pitchSemitones = PitchScaleMathCore::sanitizeNaN(pitchSemitones);

        PitchZoneSnapshot z;
        z.darkIntensity = pitchSemitones < 0.0
            ? PitchScaleMathCore::smoothstep(10.0, 36.0, -pitchSemitones) : 0.0;
        z.chipIntensity = pitchSemitones > 0.0
            ? PitchScaleMathCore::smoothstep(10.0, 36.0,  pitchSemitones) : 0.0;
        z.extremeIntensity = std::max(z.darkIntensity, z.chipIntensity);
        z.musicalIntensity = 1.0 - z.extremeIntensity;

        const double absSt = std::abs(pitchSemitones);
        if (absSt <= 2.0)
            z.zone = UnifiedPitchZone::Clean;
        else if (pitchSemitones < -12.0)
            z.zone = UnifiedPitchZone::DarkExtreme;
        else if (pitchSemitones > 12.0)
            z.zone = UnifiedPitchZone::ChipmunkExtreme;
        else
            z.zone = UnifiedPitchZone::MusicalLowHigh;

        return z;
    }

    static const char* zoneName(UnifiedPitchZone zone) noexcept
    {
        switch (zone)
        {
            case UnifiedPitchZone::Clean: return "Clean";
            case UnifiedPitchZone::MusicalLowHigh: return "MusicalLowHigh";
            case UnifiedPitchZone::DarkExtreme: return "DarkExtreme";
            case UnifiedPitchZone::ChipmunkExtreme: return "ChipmunkExtreme";
            default: return "Unknown";
        }
    }
};

} // namespace ArrangementEditor
