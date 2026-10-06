// ===========================================================================
// TimePitchQualitySelectorCore.h
// Quality selector UI model — Draft / Realtime / Balanced / High / OfflineBest.
//
// POR QUÉ EXISTE:
//   La calidad del stretch engine (windowSize, hopSyn, searchRadius)
//   debe ser configurable por el usuario y por contexto:
//     - Durante arrastre de clip → Draft (responsivo)
//     - Durante playback normal → Realtime
//     - Durante export/bounce   → OfflineBest
//
// FLUJO:
//   TimePitchQualitySelectorCore::setQuality(q)
//     → TimePitchDSPCore::setQuality(q)
//     → WSOLAStretchCore::prepare(sr, bs, q) // re-allocs window if needed
//
// AUDIO THREAD SAFETY:
//   setQuality() es mensaje thread. DSP re-prepara en audio thread
//   solo cuando el bloque está vacío (entre transport stops).
// ===========================================================================
#pragma once
#include "TimePitchQualityCore.h"
#include "TimePitchDSPCore.h"
#include <JuceHeader.h>
#include <functional>

namespace ArrangementEditor
{

class TimePitchQualitySelectorCore
{
public:
    TimePitchQualitySelectorCore() = default;

    // -----------------------------------------------------------------------
    // Names for combo boxes
    // -----------------------------------------------------------------------
    static const char* qualityName(TimePitchQuality q)
    {
        switch (q)
        {
            case TimePitchQuality::Draft:       return "Draft";
            case TimePitchQuality::Realtime:    return "Realtime";
            case TimePitchQuality::Balanced:    return "Balanced";
            case TimePitchQuality::High:        return "High";
            case TimePitchQuality::OfflineBest: return "Offline Best";
            default:                            return "Realtime";
        }
    }

    static const char* qualityDescription(TimePitchQuality q)
    {
        switch (q)
        {
            case TimePitchQuality::Draft:
                return "Fastest. Use while scrubbing or moving clips.";
            case TimePitchQuality::Realtime:
                return "Good quality for live playback. Default.";
            case TimePitchQuality::Balanced:
                return "Better quality, slightly more CPU.";
            case TimePitchQuality::High:
                return "High quality. Suitable for mixing.";
            case TimePitchQuality::OfflineBest:
                return "Maximum quality. Render to cache first.";
            default:
                return "";
        }
    }

    TimePitchQuality getQuality() const { return current_; }

    // -----------------------------------------------------------------------
    // setQuality — message thread.
    // Calls onQualityChanged so DSP cores can re-prepare.
    // -----------------------------------------------------------------------
    void setQuality(TimePitchQuality q)
    {
        if (q == current_) return;
        current_ = q;
        if (onQualityChanged) onQualityChanged(q);
    }

    // Set quality by index (for combo box callback, 0-based)
    void setQualityByIndex(int index)
    {
        const TimePitchQuality qs[] = {
            TimePitchQuality::Draft,
            TimePitchQuality::Realtime,
            TimePitchQuality::Balanced,
            TimePitchQuality::High,
            TimePitchQuality::OfflineBest
        };
        if (index >= 0 && index < 5)
            setQuality(qs[index]);
    }

    int getQualityIndex() const { return static_cast<int>(current_); }

    // Returns window size in samples for the current quality (for UI display)
    int currentWindowSizeSamples() const
    {
        return TimePitchQualityParams::forQuality(current_).windowSize;
    }

    // Returns latency in ms at a given sample rate
    float currentLatencyMs(double sampleRate) const
    {
        const int ws = currentWindowSizeSamples();
        return (sampleRate > 0) ? (float)(ws * 1000.0 / sampleRate) : 0.f;
    }

    // Callback — connect to TimePitchDSPCore::setQuality or re-prepare
    std::function<void(TimePitchQuality)> onQualityChanged;

private:
    TimePitchQuality current_ = TimePitchQuality::Realtime;
};

} // namespace ArrangementEditor
