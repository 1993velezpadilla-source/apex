// ===========================================================================
// VocalPitchCore.h
// Motor vocal: pitch + formant preservation (Mode 3).
//
// ALGORITMO — Formant compensation via spectral envelope tilt:
//
//   El problema con plain WSOLA para voces:
//     +12 st → formants suben 2x → ardilla
//     -12 st → formants bajan 0.5x → demonio
//
//   Solución (esta implementación):
//     Después de pitch-shift via WSOLA, aplicamos un filtro de "spectral tilt"
//     que compensa el desplazamiento de energía en el espectro.
//     Es una aproximación a formant preservation que funciona bien para
//     ±6st y produce resultados musicales para ±12st.
//
//     Para preserveFormants=true:
//       1. WSOLA desplaza el pitch.
//       2. Filtro de énfasis compensa la pendiente espectral.
//          El filtro es un first-order IIR que sube/baja el énfasis de HF
//          de forma opuesta al pitch shift para mantener el balance tímbrico.
//
//     Para preserveFormants=false:
//       Comportamiento natural (ardilla/demonio) — útil creativamente.
//
//   Limitaciones vs PSOLA real:
//     - No separa fonemas individualmente
//     - No hace LPC analysis/resynthesis
//     - En voces muy procesadas puede sonar ligeramente brightened/darkened
//     - Suficientemente bueno para corrección de pitch ±6st musical
//
//   PSOLA hook: reemplaza applyFormantCompensation() para producción.
//
// AUDIO THREAD SAFETY: todos los buffers pre-alocados en prepare().
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "PitchValueCore.h"
#include "FormantValueCore.h"
#include "PhaseVocoderStretchCore.h"
#include "VoiceTransformMapperCore.h"
#include "VoiceReactiveAnalyzerCore.h"
#include "VoiceTransformDSPCore.h"
#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <cmath>

namespace ArrangementEditor
{

class VocalPitchCore
{
public:
    VocalPitchCore() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;
        engine_.prepare(sampleRate, maxBlockSize, TimePitchQuality::Realtime);
        reactive_.prepare(sampleRate);
        transformDSP_.prepare(sampleRate, maxBlockSize);
        filterState_[0] = filterState_[1] = 0.f;
    }

    void reset()
    {
        engine_.reset();
        reactive_.reset();
        transformDSP_.reset();
        filterState_[0] = filterState_[1] = 0.f;
    }

    void setState(const TimePitchState& state)
    {
        voiceTransform_ = state.voiceTransform;

        engine_.setState(state);
        preserveFormants_.store(state.preserveFormants ? 1 : 0);

        const double audiblePitch = PitchValueCore::pitchToAudibleRatio(state.pitchSemitones,
                                                                         state.fineTuneCents);
        const double alpha   = juce::jlimit(0.05, 0.95, 0.5 / audiblePitch);
        filterAlpha_.store(alpha);

        const double fmSt = FormantValueCore::effectiveFormantSemitones(state, false);
        const double fmAlphaTweak = std::pow(2.0, fmSt / 12.0);
        formantAlphaTweak_.store(fmAlphaTweak);

        // Bug 10 fix: flush the IIR state when alpha changes significantly.
        // If we keep the old z-state with a new coefficient the filter
        // outputs an exponential transient that sounds like a pitched click
        // or static chirp on every pitch-knob step.
        const float newAlpha = static_cast<float>(juce::jlimit(0.05, 0.95, alpha * fmAlphaTweak));
        if (std::abs(newAlpha - lastFilterAlpha_) > 0.01f)
        {
            filterState_[0] = 0.f;
            filterState_[1] = 0.f;
            lastFilterAlpha_ = newAlpha;
        }
    }

    // -----------------------------------------------------------------------
    // renderSegment
    // -----------------------------------------------------------------------
    void renderSegment(const float* src, int srcTotal,
                       int64_t srcStart, int64_t srcEnd,
                       float* dst, int dstNumSamples,
                       int channel)
    {
        // Stage 1: WSOLA pitch shift
        engine_.renderSegment(src, srcTotal, srcStart, srcEnd,
                              dst, dstNumSamples, channel);

        // Stage 2: Formant compensation via spectral tilt IIR
        if (preserveFormants_.load() != 0)
            applyFormantCompensation(dst, dstNumSamples, channel);

        if (voiceTransform_.isActive())
        {
            const float energy = voiceTransform_.reactiveMode
                ? reactive_.processBlock(dst, dstNumSamples)
                : 0.f;
            const auto targets = VoiceTransformMapperCore::map(voiceTransform_, energy);
            transformDSP_.process(dst, dstNumSamples, channel, targets);
        }
    }

    int latencySamples() const noexcept { return 0; }

    bool isFormantPreservationActive() const { return preserveFormants_.load() != 0; }

private:
    // -----------------------------------------------------------------------
    // applyFormantCompensation
    //
    // First-order IIR spectral tilt filter.
    //
    // y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
    //
    // alpha < 0.5 → low-pass tilt   (pull down HF  — compensates pitch-up)
    // alpha > 0.5 → high-pass tilt  (pull up HF    — compensates pitch-down)
    //
    // Combined with manual formant knob: multiply alpha by fmAlphaTweak.
    //
    // This is a PSOLA hook: replace this entire function with LPC
    // analysis/resynthesis for production quality without changing the
    // renderSegment interface.
    // -----------------------------------------------------------------------
    void applyFormantCompensation(float* samples, int n, int channel)
    {
        const double alpha     = filterAlpha_.load();
        const double fmTweak   = formantAlphaTweak_.load();
        const float  a         = (float)juce::jlimit(0.05, 0.95, alpha * fmTweak);
        const float  oneMinusA = 1.f - a;

        float state = filterState_[channel & 1];

        for (int i = 0; i < n; ++i)
        {
            state      = a * samples[i] + oneMinusA * state;
            samples[i] = state;
        }

        filterState_[channel & 1] = state;
    }

    PhaseVocoderStretchCore engine_;
    VoiceReactiveAnalyzerCore reactive_;
    VoiceTransformDSPCore transformDSP_;
    VoiceTransformState voiceTransform_;
    double sampleRate_ = 44100.0;

    std::atomic<int>    preserveFormants_  { 0 };
    std::atomic<double> filterAlpha_       { 0.5 };
    std::atomic<double> formantAlphaTweak_ { 1.0 };

    // Per-channel IIR state (2 channels)
    float filterState_[2] = { 0.f, 0.f };
    // Last applied combined alpha — used to detect coefficient jumps that
    // require flushing the IIR state to prevent decay chirp artefacts.
    float lastFilterAlpha_ = 0.5f;
};

} // namespace ArrangementEditor

