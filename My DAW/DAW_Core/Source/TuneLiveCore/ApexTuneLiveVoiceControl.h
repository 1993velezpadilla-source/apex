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
    float referenceHz = 440.0f; // concert pitch, 430..450 Hz; independent from note key
    float humanize = 0.35f;     // [0,1]
    float flexTune = 0.0f;       // [0,1], leave small pitch drift untouched
    float noteHysteresis = 0.15f; // semitones
    float hopMilliseconds = 5.33f;  // real pitch-detector analysis hop
    float vibratoPreserve = 0.0f;   // additional preservation of sustained periodic modulation
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
        previousReferenceHz_ = 0.0f;
        centreMidi_ = 0.0f;
        centreValid_ = false;
        resetVibrato();
    }
    VoiceDecision update(PitchTracker::Result detected, VoiceSettings settings) noexcept {
        if (!detected.voiced || !std::isfinite(detected.hz) || detected.hz < 55.0f
            || detected.hz > 1200.0f || detected.confidence < 0.7f) {
            selected_ = -1;
            centreValid_ = false;
            resetVibrato();
            return {};
        }
        if (settings.root != previousRoot_ || settings.scale != previousScale_
            || settings.referenceHz != previousReferenceHz_) {
            selected_ = -1; // Changing key must invalidate the old note immediately.
            centreValid_ = false;
            resetVibrato();
            previousRoot_ = settings.root;
            previousScale_ = settings.scale;
            previousReferenceHz_ = settings.referenceHz;
        }
        const float reference = std::isfinite(settings.referenceHz)
                                ? std::clamp(settings.referenceHz, 430.0f, 450.0f) : 440.0f;
        const float midi = 69.0f + 12.0f * std::log2(detected.hz / reference);
        const float preserve = std::clamp(settings.vibratoPreserve, 0.0f, 1.0f);
        const bool vibrating = preserve > 0.0f && vibratoConfidence_ > 0.5f &&
                               std::abs(midi - centreMidi_) <= 0.65f;
        const int nearest = nearestScaleMidi(vibrating ? centreMidi_ : midi, settings.root, settings.scale);
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
        const float hopMs = std::clamp(settings.hopMilliseconds, 0.5f, 30.0f);
        if (newNote || !centreValid_) {
            centreMidi_ = midi;
            centreValid_ = true;
            resetVibrato();
        } else {
            constexpr float kCentreTimeMs = 135.0f;
            const float centreAlpha = 1.0f - std::exp(-hopMs / kCentreTimeMs);
            centreMidi_ += centreAlpha * (midi - centreMidi_);
        }
        const float direct = static_cast<float>(selected_) - midi;
        const float expressive = static_cast<float>(selected_) - centreMidi_;
        const float amount = std::clamp(settings.correction, 0.0f, 1.0f);
        if (preserve > 0.0f)
            followVibrato(midi - centreMidi_, hopMs);
        else
            resetVibrato();
        const float humanize = std::max(std::clamp(settings.humanize, 0.0f, 1.0f),
                                        preserve * vibratoConfidence_);
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
    void resetVibrato() noexcept {
        vibratoSide_ = vibratoCrossings_ = 0;
        crossingAgeMs_ = previousHalfPeriodMs_ = vibratoConfidence_ = 0.0f;
    }

    void followVibrato(float deviation, float hopMs) noexcept {
        // A Schmitt threshold ignores small detector jitter. Require repeated
        // alternating excursions at a plausible singing vibrato cadence.
        constexpr float threshold = 0.065f;
        constexpr float minimumHalfPeriodMs = 45.0f;
        constexpr float maximumHalfPeriodMs = 160.0f;
        crossingAgeMs_ = std::min(crossingAgeMs_ + hopMs, 1000.0f);
        const int side = deviation > threshold ? 1 : deviation < -threshold ? -1 : 0;
        if (side != 0 && side != vibratoSide_) {
            const float interval = crossingAgeMs_;
            const bool plausible = vibratoSide_ != 0 &&
                interval >= minimumHalfPeriodMs && interval <= maximumHalfPeriodMs;
            const bool consistent = previousHalfPeriodMs_ == 0.0f ||
                (interval > 0.60f * previousHalfPeriodMs_ && interval < 1.65f * previousHalfPeriodMs_);
            vibratoCrossings_ = plausible && consistent ? std::min(3, vibratoCrossings_ + 1) : 0;
            previousHalfPeriodMs_ = plausible ? interval : 0.0f;
            crossingAgeMs_ = 0.0f;
            vibratoSide_ = side;
        }
        const float target = vibratoCrossings_ >= 2 && crossingAgeMs_ < maximumHalfPeriodMs ? 1.0f : 0.0f;
        vibratoConfidence_ += (target - vibratoConfidence_) * hopMs / (35.0f + hopMs);
    }

    int selected_ = -1, previousRoot_ = -1, previousScale_ = -1;
    float previousReferenceHz_ = 0.0f;
    float centreMidi_ = 0.0f;
    bool centreValid_ = false;
    int vibratoSide_ = 0, vibratoCrossings_ = 0;
    float crossingAgeMs_ = 0.0f, previousHalfPeriodMs_ = 0.0f, vibratoConfidence_ = 0.0f;
};
}} // namespace APEX::TuneLive
