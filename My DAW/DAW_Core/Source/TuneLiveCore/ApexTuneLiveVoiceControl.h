#pragma once
// Original APEX Tune Live music/control policy. Portable C++17, zero allocations.
// Monophonic note hysteresis prevents adjacent semitone hunting near boundaries.
// Humanize preserves small expressive intonation deviations (not artificial vibrato).
#include "ApexTuneLivePitchTracker.h"
#include <algorithm>
#include <cmath>

namespace APEX { namespace TuneLive {
struct VoiceSettings {
    int root = 0;
    int scale = 0;
    float correction = 0.85f;   // [0,1]
    float humanize = 0.35f;     // [0,1]
    float noteHysteresis = 0.15f; // semitones
};
struct VoiceDecision {
    float shiftSemitones = 0.0f;
    float sourceMidi = 0.0f;
    int targetMidi = -1;
    bool voiced = false;
};
class VoiceControl final {
public:
    void reset() noexcept { selected_ = -1; previousRoot_ = -1; previousScale_ = -1; }
    VoiceDecision update(PitchTracker::Result detected, VoiceSettings settings) noexcept {
        if (!detected.voiced || !std::isfinite(detected.hz) || detected.hz < 55.0f
            || detected.hz > 1200.0f || detected.confidence < 0.7f) {
            selected_ = -1;
            return {};
        }
        if (settings.root != previousRoot_ || settings.scale != previousScale_) {
            selected_ = -1; // Changing key must invalidate the old note immediately.
            previousRoot_ = settings.root;
            previousScale_ = settings.scale;
        }
        const float midi = 69.0f + 12.0f * std::log2(detected.hz / 440.0f);
        const int nearest = nearestScaleMidi(midi, settings.root, settings.scale);
        const float hysteresis = std::clamp(settings.noteHysteresis, 0.0f, 0.40f);
        // Only keep an old target if it is still in the selected scale.
        // On fast transitions, choosing the new note immediately avoids excessive lag.
        if (selected_ < 0) selected_ = nearest;
        else if (nearest != selected_) {
            const float oldDistance = std::abs(midi - static_cast<float>(selected_));
            const float newDistance = std::abs(midi - static_cast<float>(nearest));
            if (oldDistance > newDistance + hysteresis || oldDistance > 1.5f)
                selected_ = nearest;
        }
        const float desired = std::clamp(static_cast<float>(selected_) - midi, -2.0f, 2.0f);
        const float amount = std::clamp(settings.correction, 0.0f, 1.0f);
        const float humanize = std::clamp(settings.humanize, 0.0f, 1.0f);
        // Keep fine natural drift without allowing huge pitch errors through.
        // This is intentionally smooth across the whole distance domain.
        const float attenuation = humanize * 0.70f * std::exp(-0.5f * std::pow(desired / 0.30f, 2.0f));
        return { desired * amount * (1.0f - attenuation), midi, selected_, true };
    }
private:
    int selected_ = -1, previousRoot_ = -1, previousScale_ = -1;
};
}} // namespace APEX::TuneLive
