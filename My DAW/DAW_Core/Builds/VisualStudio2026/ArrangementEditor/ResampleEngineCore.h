// ===========================================================================
// ResampleEngineCore.h
// Motor de resample: modo tape/DJ/varispeed.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Resample es el modo más básico y más importante.
//   Funciona igual que una cinta analógica o un vinilo.
//
//   pitch y tiempo están unidos:
//     playbackRate = pow(2, semitones / 12)
//     duración = original / playbackRate
//
//   +12 st = 2x speed = 1 octava arriba = dura la mitad → ardilla/chipmunk
//   -12 st = 0.5x speed = 1 octava abajo = dura el doble → voz de demonio
//
//   Ventajas vs algoritmos avanzados:
//     ✓ Sin artefactos de phase vocoder (no hay "metallic" sound)
//     ✓ CPU mínimo
//     ✓ Sonido natural/analógico
//     ✓ Ideal para efectos creativos
//     ✓ Así funciona una DDJ/Serato al cambiar pitch
//
//   Desventajas:
//     ✗ No puedes separar pitch de tiempo
//     ✗ No es adecuado para corrección de pitch musical
//
// AUDIO THREAD SAFETY:
//   processBlock() es lock-free, sin allocations.
//   El snapshot se lee atomicamente al inicio de cada bloque.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "PitchValueCore.h"
#include "StretchValueCore.h"
#include <JuceHeader.h>
#include <vector>
#include <atomic>

namespace ArrangementEditor
{

class ResampleEngineCore
{
public:
    ResampleEngineCore() = default;

    // -----------------------------------------------------------------------
    // Prepara el motor para el sample rate y block size actuales.
    // Llamar desde el audio thread antes del primer bloque.
    // -----------------------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_   = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Pre-alloca el buffer interno para evitar allocations en audio thread
        resampledBuffer_.setSize(2, maxBlockSize * 8, false, true, true);
        readPosition_ = 0.0;
    }

    // -----------------------------------------------------------------------
    // Reset de posición de lectura
    // -----------------------------------------------------------------------
    void reset()
    {
        readPosition_ = 0.0;
    }

    // Zero algorithmic latency — pure sample rate conversion.
    int latencySamples() const noexcept { return 0; }

    // -----------------------------------------------------------------------
    // Actualiza el estado (desde UI thread, publicado a audio thread).
    // Esta función es segura — solo escribe un double atomic.
    // -----------------------------------------------------------------------
    void setState(const TimePitchState& state)
    {
        // Solo usamos pitchSemitones + fineTuneCents para calcular el rate
        const double ratio = PitchValueCore::pitchToAudibleRatio(state.pitchSemitones,
                                                                 state.fineTuneCents);
        playbackRate_.store(ratio);
    }

    // -----------------------------------------------------------------------
    // Calcula la duración procesada dado el estado actual.
    // Para que el clip muestre su longitud correcta en el arrangement.
    // -----------------------------------------------------------------------
    static double processedDuration(double sourceLength, const TimePitchState& state)
    {
        const double ratio = PitchValueCore::pitchToAudibleRatio(state.pitchSemitones,
                                                                 state.fineTuneCents);
        return StretchValueCore::resampleDuration(sourceLength, ratio);
    }

    // -----------------------------------------------------------------------
    // processBlock — versión directa con interpolación lineal.
    //
    // Lee 'sourceBuffer' a playbackRate_ y escribe en 'destBuffer'.
    //
    // Parametros:
    //   sourceBuffer  — audio fuente sin procesar
    //   destBuffer    — buffer de salida (ya tiene el tamaño correcto)
    //   numSamples    — samples a escribir en destBuffer
    //
    // NOTA: Esta es una implementación placeholder funcional.
    //   Para producción se reemplaza con un interpolador de alta calidad
    //   (Sinc/Lanczos) dentro del mismo núcleo sin tocar otros archivos.
    // -----------------------------------------------------------------------
    void processBlock(const juce::AudioBuffer<float>& sourceBuffer,
                      juce::AudioBuffer<float>& destBuffer,
                      int numSamples)
    {
        const double rate    = playbackRate_.load();
        const int    srcSamples = sourceBuffer.getNumSamples();
        const int    channels   = std::min(sourceBuffer.getNumChannels(),
                                           destBuffer.getNumChannels());

        if (srcSamples == 0 || rate <= 0.0)
        {
            destBuffer.clear();
            return;
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = sourceBuffer.getReadPointer(ch);
            float*       dst = destBuffer.getWritePointer(ch);

            double readPos = readPosition_;

            for (int i = 0; i < numSamples; ++i)
            {
                // Cubic (Hermite) interpolation — eliminates the aliasing/imaging
                // hiss that linear interpolation produces at pitch ratios > ~1.5x.
                const int   i1   = static_cast<int>(readPos);
                const float frac = static_cast<float>(readPos - i1);
                const auto safe  = [src, srcSamples](int idx) noexcept -> float
                {
                    return (idx >= 0 && idx < srcSamples) ? src[idx] : 0.f;
                };
                const float y0 = safe(i1 - 1);
                const float y1 = safe(i1);
                const float y2 = safe(i1 + 1);
                const float y3 = safe(i1 + 2);
                const float c0 = y1;
                const float c1 = 0.5f * (y2 - y0);
                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                dst[i] = ((c3 * frac + c2) * frac + c1) * frac + c0;

                readPos += rate;
            }

            // Solo actualiza readPosition_ desde el primer canal para coherencia
            if (ch == 0)
                readPosition_ = readPos;
        }
    }

    double getPlaybackRate() const { return playbackRate_.load(); }

private:
    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 512;
    double readPosition_ = 0.0;

    std::atomic<double> playbackRate_ { 1.0 };

    juce::AudioBuffer<float> resampledBuffer_;
};

} // namespace ArrangementEditor
