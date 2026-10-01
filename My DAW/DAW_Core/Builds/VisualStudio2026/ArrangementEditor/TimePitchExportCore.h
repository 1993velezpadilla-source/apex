// ===========================================================================
// TimePitchExportCore.h
// Export / Bounce quality management.
//
// POR QUÉ EXISTE:
//   Durante playback normal: usar Realtime o Balanced (CPU friendly).
//   Durante export/bounce:   usar OfflineBest (máxima calidad).
//   Este núcleo centraliza esa decisión para que AudioEngine no deba
//   saber si está en modo export o playback.
//
// FLUJO:
//   1. User inicia export/bounce.
//   2. ExportManager llama TimePitchExportCore::beginExport().
//   3. Todos los DSP cores se re-preparan con OfflineBest.
//   4. Export renderiza.
//   5. ExportManager llama endExport().
//   6. DSP cores vuelven a calidad anterior.
//
// ALIGNMENT:
//   Export y playback deben producir el mismo audio en el mismo timeline.
//   Con WSOLA, la latencia (windowSize) crea un offset. Este núcleo
//   reporta la latencia en samples para que el exporter pueda
//   compensar el offset al inicio del render.
//
// AUDIO THREAD SAFETY:
//   isExporting() es atomic — seguro leer desde audio thread.
//   beginExport/endExport son mensaje thread.
// ===========================================================================
#pragma once
#include "TimePitchQualityCore.h"
#include "TimePitchDSPCore.h"
#include <JuceHeader.h>
#include <functional>
#include <atomic>
#include <vector>

namespace ArrangementEditor
{

class TimePitchExportCore
{
public:
    TimePitchExportCore() = default;

    // -----------------------------------------------------------------------
    // beginExport — message thread.
    // Switches all registered DSP cores to OfflineBest quality.
    // Stores previous quality for restore on endExport().
    // -----------------------------------------------------------------------
    void beginExport(double sampleRate, int maxBlockSize)
    {
        exportSampleRate_ = sampleRate;
        exportBlockSize_  = maxBlockSize;
        isExporting_.store(true);

        // Notify all registered DSP cores
        for (auto* dsp : registeredDSPs_)
        {
            if (dsp)
                dsp->setQuality(TimePitchQuality::OfflineBest);
        }

        if (onExportBegin) onExportBegin();
    }

    void endExport(TimePitchQuality restoreQuality = TimePitchQuality::Realtime)
    {
        isExporting_.store(false);

        for (auto* dsp : registeredDSPs_)
        {
            if (dsp)
                dsp->setQuality(restoreQuality);
        }

        if (onExportEnd) onExportEnd();
    }

    // -----------------------------------------------------------------------
    // Register / unregister DSP cores
    // -----------------------------------------------------------------------
    void registerDSP(TimePitchDSPCore* dsp)
    {
        if (dsp && std::find(registeredDSPs_.begin(), registeredDSPs_.end(), dsp)
                    == registeredDSPs_.end())
            registeredDSPs_.push_back(dsp);
    }

    void unregisterDSP(TimePitchDSPCore* dsp)
    {
        registeredDSPs_.erase(
            std::remove(registeredDSPs_.begin(), registeredDSPs_.end(), dsp),
            registeredDSPs_.end());
    }

    // -----------------------------------------------------------------------
    // exportLatencyCompensationSamples
    //
    // Returns the number of samples to skip at the start of the export
    // to compensate for WSOLA pre-fill latency.
    //
    // WSOLA needs windowSize/2 samples to "warm up" before producing
    // aligned output. The exporter should discard this many samples
    // from the start of each clip's render.
    //
    // For OfflineBest quality: windowSize = 8192, latency = 4096 samples.
    // At 44100 Hz: ~93ms offset that would otherwise appear at clip start.
    // -----------------------------------------------------------------------
    int exportLatencyCompensationSamples() const
    {
        if (!isExporting_.load()) return 0;
        const auto params = TimePitchQualityParams::forQuality(TimePitchQuality::OfflineBest);
        return params.windowSize / 2;
    }

    bool isExporting() const { return isExporting_.load(); }

    // Quality to use for playback (non-export)
    void setPlaybackQuality(TimePitchQuality q) { playbackQuality_ = q; }
    TimePitchQuality playbackQuality() const    { return playbackQuality_; }

    // Callbacks
    std::function<void()> onExportBegin;
    std::function<void()> onExportEnd;

private:
    std::atomic<bool>  isExporting_      { false };
    TimePitchQuality   playbackQuality_  = TimePitchQuality::Realtime;
    double             exportSampleRate_ = 44100.0;
    int                exportBlockSize_  = 512;

    std::vector<TimePitchDSPCore*> registeredDSPs_;
};

} // namespace ArrangementEditor
