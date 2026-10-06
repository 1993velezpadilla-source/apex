// ===========================================================================
// WSOLAStretchCore.h
// WSOLA (Waveform Similarity Overlap-Add) — motor real de time stretch.
//
// POR QUÉ WSOLA Y NO PHASE VOCODER:
//   WSOLA es el algoritmo usado por SoundTouch (Reaper, muchos DAWs open).
//   Ventajas sobre phase vocoder básico:
//     ✓ Menos "metallic" en monofónico (voz, bajo, leads)
//     ✓ Sin phase smear en transients
//     ✓ Más natural para voces habladas/cantadas
//     ✓ Implementable sin FFT pesada
//     ✓ CPU predecible en realtime
//
//   Cómo funciona WSOLA:
//     1. El output se construye frame a frame con hopSyn fijos.
//     2. Para cada frame output, se busca en el source el segmento
//        que más se parece al frame anterior (cross-correlation).
//     3. El segmento seleccionado se windowa (Hann) y se suma (OLA).
//     4. La posición de lectura avanza hopAna = hopSyn / stretchRatio.
//        → Si stretchRatio=2.0, hopAna=hopSyn/2: leemos más lento → más lento.
//        → Si stretchRatio=0.5, hopAna=hopSyn*2: leemos más rápido → más rápido.
//
//   Pitch shifting ON TOP:
//     Para PitchOnly o Vocal, se aplica resampling del output:
//     1. WSOLA estira a stretchRatio=1/pitchRatio (más largo).
//     2. Output se resamples a pitchRatio (más rápido).
//     Resultado: pitch cambia, duración queda igual.
//
// AUDIO THREAD SAFETY:
//   process() pre-alocado. Sin allocations por bloque.
//   processBuffer_ y inputBuffer_ pre-alocados en prepare().
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "TimePitchQualityCore.h"
#include "TransientDetectorCore.h"
#include "PitchValueCore.h"
#include "PitchScaleMathCore.h"
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>

namespace ArrangementEditor
{

class WSOLAStretchCore
{
public:
    WSOLAStretchCore() = default;

    // -----------------------------------------------------------------------
    // prepare — llama antes del primer bloque. Pre-aloca todo.
    // -----------------------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize,
                 TimePitchQuality quality = TimePitchQuality::Realtime)
    {
        sampleRate_   = sampleRate;
        maxBlockSize_ = maxBlockSize;

        auto qp = TimePitchQualityParams::forQuality(quality);
        windowSize_   = qp.windowSize;
        hopSyn_       = qp.hopSyn;
        searchRadius_ = qp.searchRadius;

        buildHannWindow(windowSize_);
        transientDetector_.prepare(sampleRate, maxBlockSize);
        simdScratch_.assign((size_t)juce::jmax(windowSize_, maxBlockSize_ * 2), 0.0f);

        // Output overlap-add accumulator — 2x window to handle overlap
        const int accumCap = windowSize_ * 2 + maxBlockSize * 8;
        for (int ch = 0; ch < 2; ++ch)
        {
            outputAccum_[ch].assign(accumCap, 0.f);
            outputAccumNorm_[ch].assign(accumCap, 0.f);
            prevFrame_[ch].assign(windowSize_, 0.f);
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            accumReadPos_[ch]  = 0;
            accumWritePos_[ch] = 0;
            accumFilled_[ch]   = 0;
            inputReadPos_[ch]  = 0.0;
            outputWritten_[ch] = 0;
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            inputReadPos_[ch]  = 0.0;
            outputWritten_[ch] = 0;
            accumReadPos_[ch]  = 0;
            accumWritePos_[ch] = 0;
            accumFilled_[ch]   = 0;
            std::fill(outputAccum_[ch].begin(), outputAccum_[ch].end(), 0.f);
            std::fill(outputAccumNorm_[ch].begin(), outputAccumNorm_[ch].end(), 0.f);
            std::fill(prevFrame_[ch].begin(), prevFrame_[ch].end(), 0.f);
        }
        transientDetector_.reset();
    }

    void setTransientLock(bool enabled) noexcept { transientLockEnabled_ = enabled; }

    // WSOLA latency = half analysis window (standard OLA pre-fill latency)
    int latencySamples() const noexcept { return windowSize_ / 2; }

    // -----------------------------------------------------------------------
    // setState — desde UI thread (o prepare del DSP), sin lock.
    // -----------------------------------------------------------------------
    void setStretchRatio(double ratio)
    {
        stretchRatio_.store(juce::jlimit(0.1, 8.0, ratio));
    }

    double getStretchRatio() const { return stretchRatio_.load(); }

    // -----------------------------------------------------------------------
    // renderSegment — versión directa sobre un source buffer completo.
    //
    // Esto es lo que llama TimePitchDSPCore::renderClipSegment().
    //
    // Parámetros:
    //   src           — buffer fuente completo del clip
    //   srcStartSample— primer sample del source a usar
    //   srcEndSample  — último sample del source a usar (exclusive)
    //   dst           — buffer de salida ya dimensionado
    //   dstNumSamples — samples a escribir en dst
    //   channel       — canal a procesar (para multi-channel, llamar por canal)
    //
    // Algoritmo WSOLA:
    //   hopAna = hopSyn / stretchRatio
    //   Para cada frame output:
    //     1. Lee windowSize desde sourcePos + searchRadius (best-match)
    //     2. Windowa con Hann
    //     3. OLA en el acumulador
    //     4. sourcePos avanza hopAna
    //   Extrae dstNumSamples desde el acumulador.
    // -----------------------------------------------------------------------
    void renderSegment(const float* src, int srcTotal,
                       int64_t srcStartSample, int64_t srcEndSample,
                       float* dst, int dstNumSamples,
                       int channel)
    {
        const double stretchRatio = stretchRatio_.load();
        const double hopAna       = (double)hopSyn_ / stretchRatio;
        const int chIdx           = channel & 1;

        // srcRange activo
        const int64_t activeStart = juce::jlimit((int64_t)0,
                                                 (int64_t)srcTotal,
                                                 srcStartSample);
        const int64_t activeEnd   = juce::jlimit(activeStart,
                                                 (int64_t)srcTotal,
                                                 srcEndSample);
        const int srcAvail = (int)(activeEnd - activeStart);
        if (srcAvail <= 0 || dstNumSamples <= 0)
        {
            std::fill(dst, dst + dstNumSamples, 0.f);
            return;
        }

        const int accumCap = (int)outputAccum_[chIdx].size();

        // Generate only enough frames to satisfy this callback plus a small
        // lookahead reserve. Do not regenerate a full window every callback:
        // that over-consumes source audio, drains the stretch core after a few
        // seconds, and leaves silence in the output accumulator.
        const int targetFilled = std::min(accumCap - hopSyn_, dstNumSamples + windowSize_ * 2);
        while (accumFilled_[chIdx] < targetFilled)
        {
            // Posición en source para este frame
            const int64_t nominalSrcPos = activeStart + (int64_t)inputReadPos_[chIdx];

            // Búsqueda WSOLA: encuentra el offset dentro de searchRadius_
            // que maximiza la similitud con el frame anterior.
            int64_t bestSrcPos = nominalSrcPos;
            if (transientLockEnabled_)
            {
                const int64_t winStart = juce::jlimit(activeStart, activeEnd - 1, nominalSrcPos - hopSyn_);
                const int winLen = (int)juce::jmin<int64_t>((int64_t)hopSyn_ * 2, activeEnd - winStart);
                const int transient = transientDetector_.findTransientInWindow(src + (int)winStart, winLen);
                if (transient >= 0 && std::abs((int)((winStart + transient) - nominalSrcPos)) <= hopSyn_)
                    bestSrcPos = winStart + transient;
                else
                    bestSrcPos = findBestMatch(src, srcTotal, activeStart, activeEnd, nominalSrcPos, searchRadius_, chIdx);
            }
            else
            {
                bestSrcPos = findBestMatch(src, srcTotal, activeStart, activeEnd, nominalSrcPos, searchRadius_, chIdx);
            }

            // Windowa y OLA
            for (int i = 0; i < windowSize_; ++i)
            {
                const int64_t si = bestSrcPos + i;
                const float s = (si >= activeStart && si < activeEnd)
                    ? src[(int)si] : 0.f;
                const float w = hannWindow_[i];
                const int ai  = (accumWritePos_[chIdx] + i) % accumCap;
                outputAccum_[chIdx][ai]     += s * w;
                outputAccumNorm_[chIdx][ai] += w;
            }

            accumWritePos_[chIdx] = (accumWritePos_[chIdx] + hopSyn_) % accumCap;
            accumFilled_[chIdx]   = std::min(accumFilled_[chIdx] + hopSyn_, accumCap);

            inputReadPos_[chIdx] += hopAna;

            // If we overrun the source, stop generating. Never wrap in clip
            // playback: wrapping turns a long stretch into repeated/corrupt
            // material and can mask the real end of the source.
            if (inputReadPos_[chIdx] >= (double)srcAvail)
            {
                inputReadPos_[chIdx] = (double)srcAvail;
                break;
            }
        }

        // Extraer samples del acumulador normalizado
        // Guard: only read as many samples as were actually filled. If the
        // accumulator ran short (e.g. source exhausted mid-block), zero-fill
        // the remainder explicitly instead of reading stale cleared cells,
        // which avoids crackle at buffer boundaries.
        const int safeRead = std::min(dstNumSamples, accumFilled_[chIdx]);
        for (int i = 0; i < safeRead; ++i)
        {
            const int ai   = (accumReadPos_[chIdx] + i) % accumCap;
            const float n  = outputAccumNorm_[chIdx][ai];
            const float raw = (n > 0.001f) ? outputAccum_[chIdx][ai] / n : 0.f;
            dst[i] = PitchScaleMathCore::flushDenormal(raw);
        }
        for (int i = safeRead; i < dstNumSamples; ++i)
            dst[i] = 0.f;

        // Avanza read pos y limpia lo que se consumió
        // Bug 21 fix: only clear and advance by safeRead, not dstNumSamples.
        // Clearing beyond safeRead would erase positions that were never written
        // (they are already zero), but — more importantly — subtracting
        // dstNumSamples from accumFilled_ when safeRead < dstNumSamples drives
        // the counter negative (clamped to 0), discarding the fact that some
        // filled samples may remain in the accumulator for the next call.
        for (int i = 0; i < safeRead; ++i)
        {
            const int ai = (accumReadPos_[chIdx] + i) % accumCap;
            outputAccum_[chIdx][ai]     = 0.f;
            outputAccumNorm_[chIdx][ai] = 0.f;
        }
        accumReadPos_[chIdx] = (accumReadPos_[chIdx] + dstNumSamples) % accumCap;
        accumFilled_[chIdx]  = std::max(0, accumFilled_[chIdx] - safeRead);

        // Mark that output has been produced so future frames can use
        // real WSOLA best-match correlation instead of staying forever in
        // the nominal/no-match startup path.
        outputWritten_[chIdx] += dstNumSamples;
    }

private:
    // -----------------------------------------------------------------------
    // Búsqueda WSOLA: encuentra el offset [-radius, +radius] alrededor de
    // nominalSrcPos que minimiza el error RMS vs el frame anterior.
    //
    // Si no hay frame anterior (inicio) devuelve nominal.
    // Complejidad: O(radius * windowSize) — limitado por searchRadius_.
    // -----------------------------------------------------------------------
    int64_t findBestMatch(const float* src, int srcTotal,
                          int64_t activeStart, int64_t activeEnd,
                          int64_t nominalSrcPos, int radius, int channel) const
    {
        const int chIdx = channel & 1;
        const int64_t clampedNominal = juce::jlimit(activeStart,
                                                    juce::jmax(activeStart, activeEnd - 1),
                                                    nominalSrcPos);

        if (outputWritten_[chIdx] == 0 || radius == 0)
        {
            updatePrevFrame(src, srcTotal, activeStart, activeEnd, clampedNominal, chIdx);
            return clampedNominal;
        }

        int64_t bestPos = clampedNominal;
        double bestScore = -1.0;
        const int checkLen = juce::jmin(windowSize_, juce::jmax(64, windowSize_ / 2));

        auto scoreCandidate = [&](int64_t candidate) -> double
        {
            if (candidate < activeStart || candidate >= activeEnd) return -1.0;
            if ((int)simdScratch_.size() < checkLen)
                simdScratch_.resize((size_t)checkLen, 0.0f);
            for (int i = 0; i < checkLen; ++i)
            {
                const int64_t si = candidate + i;
                simdScratch_[(size_t)i] = (si >= activeStart && si < activeEnd) ? src[(int)si] : 0.0f;
            }
            const float* a = simdScratch_.data();
            const float* b = prevFrame_[chIdx].data();
            double dot = 0.0, normA = 0.0, normB = 0.0;
            for (int i = 0; i < checkLen; ++i)
            {
                dot += (double)a[i] * (double)b[i];
                normA += (double)a[i] * (double)a[i];
                normB += (double)b[i] * (double)b[i];
            }
            const double denom = std::sqrt(normA * normB);
            return (denom > 1.0e-8) ? dot / denom : 0.0;
        };

        for (int offset = -radius; offset <= radius; offset += 4) // stride 4 = faster
        {
            const int64_t candidate = clampedNominal + offset;
            const double score = scoreCandidate(candidate);

            if (score > bestScore)
            {
                bestScore = score;
                bestPos   = candidate;
            }
        }

        const int64_t coarseBestPos = bestPos;
        for (int fineOff = -3; fineOff <= 3; ++fineOff)
        {
            if (fineOff == 0) continue;
            const int64_t candidate = coarseBestPos + fineOff;
            const double score = scoreCandidate(candidate);
            if (score > bestScore)
            {
                bestScore = score;
                bestPos = candidate;
            }
        }

        updatePrevFrame(src, srcTotal, activeStart, activeEnd, bestPos, chIdx);

        return bestPos;
    }

    void updatePrevFrame(const float* src, int srcTotal,
                         int64_t activeStart, int64_t activeEnd,
                         int64_t frameStart, int chIdx) const
    {
        for (int i = 0; i < windowSize_ && i < (int)prevFrame_[chIdx].size(); ++i)
        {
            const int64_t si = frameStart + i;
            prevFrame_[chIdx][i] = (si >= activeStart && si < activeEnd && si < srcTotal)
                ? src[(int)si] : 0.f;
        }
    }

    void buildHannWindow(int size)
    {
        hannWindow_.resize(size);
        for (int i = 0; i < size; ++i)
            hannWindow_[i] = 0.5f * (1.f - std::cos(
                2.f * juce::MathConstants<float>::pi * i / (float)(size - 1)));
    }

    double sampleRate_    = 44100.0;
    int    maxBlockSize_  = 512;
    int    windowSize_    = 1024;
    int    hopSyn_        = 256;
    int    searchRadius_  = 128;
    bool   transientLockEnabled_ = false;

    std::atomic<double> stretchRatio_ { 1.0 };

    std::vector<float>  hannWindow_;
    mutable std::vector<float> prevFrame_[2];
    mutable std::vector<float> simdScratch_;
    TransientDetectorCore transientDetector_;

    // Acumulador OLA — 2 canales pre-alocados
    std::vector<float>  outputAccum_[2];
    std::vector<float>  outputAccumNorm_[2];
    int accumReadPos_[2]   = { 0, 0 };
    int accumWritePos_[2]  = { 0, 0 };
    int accumFilled_[2]    = { 0, 0 };

    double inputReadPos_[2]  = { 0.0, 0.0 };
    int    outputWritten_[2] = { 0, 0 };
};

} // namespace ArrangementEditor
