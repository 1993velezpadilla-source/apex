#pragma once
// Original APEX Tune Live music/control policy. Portable C++17, zero allocations.
// Monophonic note hysteresis prevents adjacent semitone hunting near boundaries.
// Expressive Humanize follows the sung note's slow centre while retaining vibrato.
// No synthesized vibrato, no time-domain allocation and no proprietary tuning model.
#include "ApexTuneLivePitchTracker.h"
#include <algorithm>
#include <cmath>

namespace APEX { namespace TuneLive {
struct VoiceSettings {
    int root = 0;
    int scale = 0;
    float correction = 0.85f;   // [0,1]
    float humanize = 0.35f;     // [0,1]
    float flexTune = 0.0f;       // [0,1], leave small pitch drift untouched
    float noteHysteresis = 0.15f; // semitones
    float hopMilliseconds = 5.33f;  // real pitch-detector analysis hop
};
struct VoiceDecision {
    float shiftSemitones = 0.0f;
    float sourceMidi = 0.0f;
    int targetMidi = -1;
    bool voiced = false;
};
class VoiceControl final {
public:
    void reset() noexcept {
        selected_ = -1;
        previousRoot_ = -1;
        previousScale_ = -1;
        centreMidi_ = 0.0f;
        centreValid_ = false;
    }
    VoiceDecision update(PitchTracker::Result detected, VoiceSettings settings) noexcept {
        if (!detected.voiced || !std::isfinite(detected.hz) || detected.hz < 55.0f
            || detected.hz > 1200.0f || detected.confidence < 0.7f) {
            selected_ = -1;
            centreValid_ = false;
            return {};
        }
        if (settings.root != previousRoot_ || settings.scale != previousScale_) {
            selected_ = -1; // Changing key must invalidate the old note immediately.
            centreValid_ = false;
            previousRoot_ = settings.root;
            previousScale_ = settings.scale;
        }
        const float midi = 69.0f + 12.0f * std::log2(detected.hz / 440.0f);
        const int nearest = nearestScaleMidi(midi, settings.root, settings.scale);
        const float hysteresis = std::clamp(settings.noteHysteresis, 0.0f, 0.40f);
        // Only keep an old target if it is still in the selected scale.
        // On fast transitions, choosing the new note immediately avoids excessive lag.
        bool newNote = false;
        if (selected_ < 0) { selected_ = nearest; newNote = true; }
        else if (nearest != selected_) {
            const float oldDistance = std::abs(midi - static_cast<float>(selected_));
            const float newDistance = std::abs(midi - static_cast<float>(nearest));
            if (oldDistance > newDistance + hysteresis || oldDistance > 1.5f) {
                selected_ = nearest;
                newNote = true;
            }
        }
        // Pitch-tracking happens in short hops. Treat the moving note centre
        // differently from the instantaneous sung pitch: the slow part is
        // detuning; the fast residual contains vibrato and small inflections.
        // If we snap EVERY instantaneous pitch directly to the target, we
        // erase the singer's natural vibrato. Humanize mixes the two tuning
        // decisions rather than randomly adding vibrato to the signal.
        if (newNote || !centreValid_) {
            centreMidi_ = midi;
            centreValid_ = true;
        } else {
            const float hopMs = std::clamp(settings.hopMilliseconds, 1.0f, 30.0f);
            constexpr float kCentreTimeMs = 135.0f;
            const float centreAlpha = 1.0f - std::exp(-hopMs / kCentreTimeMs);
            centreMidi_ += centreAlpha * (midi - centreMidi_);
        }
        const float direct = static_cast<float>(selected_) - midi;
        const float expressive = static_cast<float>(selected_) - centreMidi_;
        const float amount = std::clamp(settings.correction, 0.0f, 1.0f);
        const float humanize = std::clamp(settings.humanize, 0.0f, 1.0f);
        const float desired = std::clamp(direct * (1.0f - humanize)
                                        + expressive * humanize, -2.0f, 2.0f);
        // Flex Tune: soft musical dead-zone in semitones, not a hidden
        // dynamics effect. Natural singers retain subtle deviations; large
        // out-of-tune notes still pull toward the selected scale.
        const float flexCents = 18.0f * std::clamp(settings.flexTune, 0.0f, 1.0f);
        const float flexSemitones = flexCents * 0.01f;
        const float flexible = std::copysign(
            std::max(0.0f, std::abs(desired)-flexSemitones), desired);
        return { flexible * amount, midi, selected_, true };
    }
private:
    int selected_ = -1, previousRoot_ = -1, previousScale_ = -1;
    float centreMidi_ = 0.0f;
    bool centreValid_ = false;
};
}} // namespace APEX::TuneLive
