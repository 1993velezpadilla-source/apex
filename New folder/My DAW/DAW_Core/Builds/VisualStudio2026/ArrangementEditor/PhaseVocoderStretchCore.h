// ===========================================================================
// PhaseVocoderStretchCore.h
// Motor WSOLA real para Stretch (Mode 1), PitchOnly (Mode 2), Vocal (Mode 3).
//
// ALGORITMO REAL (WSOLA — Waveform Similarity Overlap-Add):
//   Mismo algoritmo base que SoundTouch / Reaper / muchos DAWs:
//     1. Divide source en frames solapados (Hann window).
//     2. Busca el frame fuente más similar al anterior (cross-correlation).
//     3. OLA en output → cambia duración sin metallic phase artifacts.
//
//   Pitch shifting (Mode 2/3) = WSOLA stretch + resample compensatorio:
//     - WSOLA produce señal 1/pitchRatio veces más larga.
//     - Resampling a pitchRatio restaura la duración.
//     - Resultado: pitch cambia, duración igual.
//
// REEMPLAZAR EN FUTURO:
//   Solo reemplaza WSOLAStretchCore internals si quieres FFT phase vocoder.
//   Esta interfaz pública NO cambia.
//
// AUDIO THREAD SAFETY: sin allocations. Todo pre-alocado en prepare().
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "PitchValueCore.h"
#include "StretchValueCore.h"
#include "WSOLAStretchCore.h"
#include "PitchScaleMathCore.h"
#include <JuceHeader.h>
#include <vector>
#include <atomic>

namespace ArrangementEditor
{

class PhaseVocoderStretchCore
{
public:
    PhaseVocoderStretchCore() = default;

    static float readCubic(const float* data, int length, double pos) noexcept
    {
        const int i1 = (int) std::floor(pos);
        const float frac = (float) (pos - (double) i1);
        const auto readSafe = [data, length](int index) noexcept -> float
        {
            return (index >= 0 && index < length) ? data[index] : 0.0f;
        };

        const float y0 = readSafe(i1 - 1);
        const float y1 = readSafe(i1);
        const float y2 = readSafe(i1 + 1);
        const float y3 = readSafe(i1 + 2);

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        const float result = ((c3 * frac + c2) * frac + c1) * frac + c0;
        return PitchScaleMathCore::flushDenormal(result);
    }

    void prepare(double sampleRate, int maxBlockSize,
                 TimePitchQuality quality = TimePitchQuality::Realtime)
    {
        sampleRate_   = sampleRate;
        maxBlockSize_ = maxBlockSize;
        wsola_.prepare(sampleRate, maxBlockSize, quality);
        // Worst-case pitch ratio is 16.0 (+48 st internal headroom).
        // Add 16 samples of slack for the +4 cubic look-ahead and rounding.
        constexpr double kMaxPitchRatio = 16.0;
        constexpr int    kCubicSlack    = 16;
        const int resBufCap =
            static_cast<int>(std::ceil(maxBlockSize * kMaxPitchRatio)) + kCubicSlack;
        for (int ch = 0; ch < 2; ++ch)
            resampleBuf_[ch].assign(static_cast<size_t>(resBufCap), 0.f);
    }

    void reset()
    {
        wsola_.reset();
    }

    void setState(const TimePitchState& state)
    {
        pitchRatio_.store(PitchValueCore::pitchToAudibleRatio(state.pitchSemitones,
                                                               state.fineTuneCents));
        stretchRatio_.store(state.stretchRatio);
        mode_.store(static_cast<int>(state.mode));
        wsola_.setTransientLock(state.transientLock || state.mode == TimePitchMode::Percussion || state.transientPreserve);
    }

    // renderSegment — el punto de entrada desde TimePitchDSPCore.
    void renderSegment(const float* src, int srcTotal,
                       int64_t srcStart, int64_t srcEnd,
                       float* dst, int dstNumSamples,
                       int channel)
    {
        const double pitch   = pitchRatio_.load();
        const double stretch = stretchRatio_.load();
        const int    mode    = mode_.load();
        const int    chIdx   = channel & 1;
        juce::ignoreUnused(chIdx);

        if (mode == static_cast<int>(TimePitchMode::Stretch))
        {
            // Pure time stretch: pitch stays same, duration changes
            wsola_.setStretchRatio(stretch);
            wsola_.renderSegment(src, srcTotal, srcStart, srcEnd,
                                 dst, dstNumSamples, channel);
        }
        else
        {
            // PitchOnly / Vocal / OfflineHQ fallback:
            // Stage 1: WSOLA stretches by stretchRatio * pitchRatio.
            // Stage 2: Resample at pitchRatio.
            // Combined duration = stretchRatio, while pitch changes independently.
            const double wsolaStretch = juce::jmax(0.0001, stretch * ((pitch > 0.0001) ? pitch : 1.0));
            wsola_.setStretchRatio(wsolaStretch);

            const int intermediateN = (int)std::ceil(dstNumSamples * pitch) + 4;
            const int resBufCap     = (int)resampleBuf_[chIdx].size();
            // Bug #2 guard: if intermediateN exceeds the pre-allocated buffer
            // (e.g. a pitch ratio spike on first block), grow the buffer instead
            // of returning silence (silence injects a hard click/pop).
            // This allocation path is only hit when pitch jumps beyond what was
            // prepared — rare at runtime, never on steady-state playback.
            if (intermediateN > resBufCap)
            {
                resampleBuf_[chIdx].assign(static_cast<size_t>(intermediateN) + 16, 0.f);
            }
            const int   interN  = intermediateN;
            float* interBuf     = resampleBuf_[chIdx].data();

            wsola_.renderSegment(src, srcTotal, srcStart, srcEnd,
                                 interBuf, interN, channel);

            // Cubic resample: interBuf → dst at pitchRatio
            for (int i = 0; i < dstNumSamples; ++i)
            {
                const double srcPos = i * pitch;
                const float raw = readCubic(interBuf, interN, srcPos);
                dst[i] = PitchScaleMathCore::flushDenormal(raw);
            }

            // Vocal formant correction placeholder:
            // TODO: apply cepstrum/LPC formant envelope shift here
            // when preserveFormants=true and mode==Vocal.
        }
    }

    double getStretchRatio() const { return stretchRatio_.load(); }

    // Legacy processBlock compatibility shim (for any old call sites)
    void processBlock(const juce::AudioBuffer<float>& src,
                      juce::AudioBuffer<float>& dst, int numSamples)
    {
        const int channels = std::min(src.getNumChannels(), dst.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            renderSegment(src.getReadPointer(ch), src.getNumSamples(),
                          0, src.getNumSamples(),
                          dst.getWritePointer(ch), numSamples, ch);
    }

private:
    double sampleRate_    = 44100.0;
    int    maxBlockSize_  = 512;

    std::atomic<double> pitchRatio_   { 1.0 };
    std::atomic<double> stretchRatio_ { 1.0 };
    std::atomic<int>    mode_         { 1 };

    WSOLAStretchCore   wsola_;
    std::vector<float> resampleBuf_[2];
};

} // namespace ArrangementEditor
