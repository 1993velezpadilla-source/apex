// ===========================================================================
// ClipPitchCore.h
// Pure logic for clip pitch shifting.
// Pitch is stored as semitone offset (-36..+36 UI, -48..+48 legacy headroom).
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include <cmath>
#include <string>

namespace ArrangementEditor
{
    class ClipPitchCore
    {
    public:
        // Convert semitone offset to frequency ratio
        static float semitoneToRatio(float semitones)
        {
            return (float)PitchScaleMathCore::semitonesToRatio(semitones);
        }

        // Convert frequency ratio to semitones
        static float ratioToSemitone(float ratio)
        {
            return (float)PitchScaleMathCore::ratioToSemitones(ratio);
        }

        // Format pitch for display
        static std::string formatPitch(float semitones)
        {
            char buf[32];
            if (std::fabsf(semitones) < 0.01f)
                snprintf(buf, sizeof(buf), "0 (root)");
            else
                snprintf(buf, sizeof(buf), "%+.1f st", semitones);
            return std::string(buf);
        }

        // Check if pitch shift is active
        static bool isPitchShifted(float semitones)
        {
            return std::fabsf(semitones) > 0.01f;
        }

        // Range constants
        static constexpr float kMinSemitones = -36.f;
        static constexpr float kMaxSemitones = +36.f;
        static constexpr float kDefault = 0.f;
    };

} // namespace ArrangementEditor
