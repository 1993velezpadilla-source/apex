// ===========================================================================
// ClipGainCore.h
// Pure logic for clip gain calculations.
// Clip gain is separate from track fader volume — it scales audio before the track.
// ===========================================================================
#pragma once
#include <cmath>
#include <string>

namespace ArrangementEditor
{
    class ClipGainCore
    {
    public:
        // Convert linear gain (0..4) to dB
        static float linearToDb(float linear)
        {
            if (linear <= 0.0001f) return -150.f;
            return 20.f * std::log10f(linear);
        }

        // Convert dB to linear gain
        static float dbToLinear(float db)
        {
            if (db <= -150.f) return 0.f;
            return std::powf(10.f, db / 20.f);
        }

        // Apply gain to a single sample
        static float applyGain(float sample, float gainLinear)
        {
            return sample * gainLinear;
        }

        // Format gain for display
        static std::string formatGain(float linear)
        {
            float db = linearToDb(linear);
            char buf[32];
            if (db <= -60.f)
                snprintf(buf, sizeof(buf), "-inf dB");
            else
                snprintf(buf, sizeof(buf), "%+.1f dB", db);
            return std::string(buf);
        }

        // Range constants
        static constexpr float kMinLinear = 0.0f;
        static constexpr float kMaxLinear = 4.0f;     // +12 dB
        static constexpr float kDefaultLinear = 1.0f; // 0 dB
    };

} // namespace ArrangementEditor
