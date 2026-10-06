// ===========================================================================
// TimePitchQualityCore.h
// Calidad de procesamiento: window sizes, hop sizes, calidad de interpolación.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Cada modo de stretch tiene un trade-off entre CPU y calidad.
//   Draft = rápido para edición en tiempo real.
//   Realtime = balance CPU/calidad para playback.
//   High / OfflineBest = máxima calidad para render final.
//
//   Los DAWs pro usan calidades distintas según el contexto:
//   - Arrastre de clip → Draft
//   - Playback normal → Realtime
//   - Bounce/Export → OfflineBest
// ===========================================================================
#pragma once

namespace ArrangementEditor
{

enum class TimePitchQuality
{
    Draft       = 0,
    Realtime    = 1,
    Balanced    = 2,
    High        = 3,
    OfflineBest = 4
};

struct TimePitchQualityParams
{
    int windowSize;  // Hann window size in samples
    int hopSyn;      // Synthesis hop in output samples (= windowSize / 4)
    int searchRadius;// WSOLA search radius in source samples (for best-match)

    static TimePitchQualityParams forQuality(TimePitchQuality q)
    {
        switch (q)
        {
            case TimePitchQuality::Draft:
                return { 512,  128,   32  };
            case TimePitchQuality::Realtime:
                return { 1024, 256,  128  };
            case TimePitchQuality::Balanced:
                return { 1024, 256,  128  };
            case TimePitchQuality::High:
                return { 2048, 512,  256  };
            case TimePitchQuality::OfflineBest:
                return { 4096, 1024, 512  };
            default:
                return { 1024, 256,  128  };
        }
    }
};

} // namespace ArrangementEditor
