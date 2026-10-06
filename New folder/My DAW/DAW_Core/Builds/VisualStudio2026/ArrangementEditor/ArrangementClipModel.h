// ===========================================================================
// ArrangementClipModel.h
// Pure data model for a single clip in the arrangement.
// No JUCE dependencies — can be serialized, copied, stored independently.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <string>
#include <cmath>
#include "TimePitchTypesCore.h"
#include "PitchTimelineCompensationCore.h"

namespace ArrangementEditor
{
    struct ArrangementClipModel
    {
        juce::Uuid   id;
        int          trackIndex      = 0;
        double       startTime       = 0.0;      // seconds from project start
        double       length          = 1.0;      // seconds
        double       sourceOffset    = 0.0;      // trim start into source file
        double       referenceBpm    = 120.0;    // BPM at which this clip was created (for tempo-relative playback)
        float        gain            = 1.f;      // linear, 0..4 (shown as dB)

        // --- Legacy pitch/rate fields (kept for backward compat) ---
        // Estos campos se reemplazan por timePitch.pitchSemitones y
        // timePitch.stretchRatio en el nuevo sistema.
        // AudioEngine debe leer de timePitch cuando mode != Resample legacy.
        float        pitch           = 0.f;      // semitones, -24..+24
        float        rate            = 1.f;      // playback rate 0.1..4.0

        float        fadeInLength    = 0.f;      // seconds
        float        fadeOutLength   = 0.f;      // seconds
        // Fade curve shape: -1 = exponential (slow start), 0 = linear,
        // +1 = logarithmic (fast start). Used by the fade renderer and the
        // fade DSP. Drag the small curve handle on the ramp to change.
        float        fadeInCurve     = 0.f;      // -1..+1
        float        fadeOutCurve    = 0.f;      // -1..+1
        int          stretchMode     = 0;        // index into stretch modes
        bool         reversed        = false;
        bool         muted           = false;
        bool         locked          = false;
        std::string  sourcePath;
        std::string  clipName;
        juce::Colour colour          { 0xFFE07B39 }; // default: DAW orange

        // --- Sistema pro de pitch / time stretch ---
        // Contiene todos los parámetros del núcleo TimePitchDSPCore:
        //   pitchSemitones, fineTuneCents, stretchRatio, formantSemitones,
        //   mode (Resample/Stretch/Vocal/etc), preserveFormants, etc.
        //
        // Al hacer split: ambas mitades heredan el mismo timePitch.
        // Al hacer slip: timePitch no se modifica.
        // Al save/load: TimePitchSerializationCore maneja la serialización.
        TimePitchState timePitch;

        // Source bounds en samples (para split/slip correctos)
        int64_t  sourceStartSample = 0;
        int64_t  sourceEndSample   = 0;
        // Total length of the underlying audio file, in samples. Used as the
        // hard upper bound when right-edge trimming so we never push the
        // visible region past the actual decoded audio (which would cause
        // the engine to stretch / repeat to fill). 0 = unknown.
        int64_t  sourceTotalSamples = 0;
        // Sample rate of the underlying source file. Used to convert the
        // sample-based source bounds above into seconds during edge trim so
        // the cap is correct for non-44.1kHz material. 0 = unknown (treat 44100).
        double   sourceSampleRate   = 0.0;

        // ── Waveform visual versioning ────────────────────────────────────
        // Bumped whenever a visual property changes (gain, fade, etc.)
        // ClipRenderCore checks this to decide whether to repaint.
        uint64_t waveformVisualVersion = 0;

        // Bumped whenever the processed audio changes (pitch/stretch mode/ratio)
        // Triggers async processed waveform peak regeneration.
        uint64_t processedWaveformVersion = 0;

        void bumpWaveformVisualVersion()   { ++waveformVisualVersion; }
        void bumpProcessedWaveformVersion() { ++processedWaveformVersion; }

        // Returns true if this clip needs processed (non-source) waveform peaks
        bool needsProcessedWaveform() const
        {
            const int mode = static_cast<int>(timePitch.mode);
            // Mode 0 (Resample): no time stretch, just pitch-speed link → use source
            // Mode 2 (PitchOnly): duration unchanged → use source
            // Modes 1,3,4,5,6: time stretch active → need processed
            return (mode != 0 && mode != 2);
        }

        // ── Fade helpers (for visual waveform rendering) ──────────────────
        // Returns fade-in duration in samples (for waveform fade visual)
        int64_t fadeInSamples(double sampleRate) const
        {
            return (int64_t)std::llround(fadeInLength * sampleRate);
        }

        // Returns fade-out duration in samples
        int64_t fadeOutSamples(double sampleRate) const
        {
            return (int64_t)std::llround(fadeOutLength * sampleRate);
        }

        // Helpers
        double endTime() const { return startTime + length; }
        bool   contains(double time) const { return time >= startTime && time < endTime(); }
        bool   overlaps(double start, double end) const
        {
            return startTime < end && endTime() > start;
        }

        // ── Visual duration — what the clip LOOKS like in the arrangement ──
        //
        // This is what ArrangementViewCore uses to size the clip component.
        // Pitch alone does not change arrangement duration in the unified engine.
        //
        // Rules:
        //   Mode 0 Resample:
        //     playbackRate = pow(2, semitones / 12)
        //     visual = length / playbackRate
        //     +12 st → half duration visible  (clip is narrower)
        //     -12 st → double duration visible (clip is wider)
        //
        //   Mode 1 Stretch:
        //     visual = length * stretchRatio
        //     200% → clip appears twice as wide
        //
        //   Mode 2 PitchOnly:
        //     visual = length  (duration unchanged)
        //
        //   Modes 3-5 (Vocal/Percussion/Texture):
        //     visual = length * stretchRatio
        //
        //   Mode 6 OfflineHQ:
        //     uses the same rules as Mode 1 (stretch governs)
        //
        double visualLength() const
        {
            const auto& tp   = timePitch;
            const int   mode = static_cast<int>(tp.mode);

            if (mode == 2) // PitchOnly — duration unchanged
            {
                return length;
            }
            else // Unified pitch path: stretch governs timeline, pitch does not.
            {
                const double sr = (tp.stretchRatio > 0.001) ? tp.stretchRatio : 1.0;
                return PitchTimelineCompensationCore::getProcessedTimelineLengthForArrangement(length, sr);
            }
        }

        // Generate new ID
        static ArrangementClipModel createNew(int track, double start, double len)
        {
            ArrangementClipModel c;
            c.id         = juce::Uuid();
            c.trackIndex = track;
            c.startTime  = start;
            c.length     = len;
            return c;
        }
    };

} // namespace ArrangementEditor
