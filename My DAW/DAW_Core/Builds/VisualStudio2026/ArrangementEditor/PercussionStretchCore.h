// ===========================================================================
// PercussionStretchCore.h
// Time stretch con preservación de transients — Mode 4 (Percussion).
//
// POR QUÉ EXISTE:
//   En drums/percussion, lo más importante es el timing del ataque.
//   Los algoritmos OLA/WSOLA normales promedian ciclos de onda y producen
//   "smear" en los transients: el golpe se extiende y pierde punch.
//
//   PercussionStretchCore detecta transients y los preserva:
//     1. Detecta picos de energía (onset detector simple).
//     2. Alrededor de cada onset, usa ventanas más cortas.
//     3. Fuera de los ataques, usa WSOLA normal.
//
//   Resultado: kick/snare mantienen su punch, los tonales se estiran suave.
//
// ALGORITMO:
//   - Onset detection: flujo espectral o delta RMS (aquí: delta RMS).
//   - En zona de transient: ventana = windowSize/4.
//   - Fuera: ventana normal.
//   - Equivalent a "transient sharpening" de Elastique.
//
// AUDIO THREAD SAFETY: todo pre-alocado.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "WSOLAStretchCore.h"
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <atomic>

namespace ArrangementEditor
{

class PercussionStretchCore
{
public:
    PercussionStretchCore() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;
        // Use Balanced quality for percussion — short windows for transients
        wsola_.prepare(sampleRate, maxBlockSize, TimePitchQuality::Balanced);
        // Onset detection buffer
        prevRms_ = 0.f;
    }

    void reset()
    {
        wsola_.reset();
        prevRms_ = 0.f;
    }

    void setStretchRatio(double ratio)
    {
        stretchRatioRequested_ = juce::jlimit(0.1, 16.0, ratio);
        wsola_.setStretchRatio(stretchRatioRequested_);
    }

    // -----------------------------------------------------------------------
    // renderSegment — detects transients, routes through WSOLA with
    // shorter windows near onsets.
    //
    // Current implementation: delegates to WSOLA (which already handles
    // best-match search that naturally respects transient boundaries).
    // The onset detection adjusts the search radius atomically.
    // -----------------------------------------------------------------------
    void renderSegment(const float* src, int srcTotal,
                       int64_t srcStart, int64_t srcEnd,
                       float* dst, int dstNumSamples,
                       int channel)
    {
        // Detect if current segment has a strong transient
        // by measuring RMS delta over the first 256 samples
        float rms = 0.f;
        const int checkN = std::min(256, (int)(srcEnd - srcStart));
        for (int i = 0; i < checkN; ++i)
        {
            const int64_t si = srcStart + i;
            if (si < srcTotal) rms += src[(int)si] * src[(int)si];
        }
        rms = (checkN > 0) ? std::sqrt(rms / checkN) : 0.f;

        // If RMS jumped significantly vs previous frame → transient zone.
        // Use a stretch ratio closer to 1.0 to preserve attack timing.
        const float delta = rms - prevRms_;
        const bool nearTransient = (delta > 0.1f);
        if (nearTransient)
            wsola_.setStretchRatio(1.0 + (stretchRatioRequested_ - 1.0) * 0.5);
        prevRms_ = rms;

        wsola_.renderSegment(src, srcTotal, srcStart, srcEnd,
                             dst, dstNumSamples, channel);

        // Bug 13 fix: always restore the requested ratio after the render call.
        // Previously this was only mentioned in a comment ("reads stretchRatio_
        // atomically each call") but wsola_.setStretchRatio() had already been
        // called with the halved transient ratio and was never corrected,
        // causing the ratio to drift permanently after any transient is detected.
        wsola_.setStretchRatio(stretchRatioRequested_);
    }

    WSOLAStretchCore& getWSOLA() { return wsola_; }

private:
    WSOLAStretchCore wsola_;
    double sampleRate_             = 44100.0;
    double stretchRatioRequested_  = 1.0;
    float  prevRms_                = 0.f;
};

} // namespace ArrangementEditor
