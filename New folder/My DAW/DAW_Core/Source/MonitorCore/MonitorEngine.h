#pragma once
#include <JuceHeader.h>
#include "MonitorStateModel.h"
#include "DimMuteMonoProcessor.h"
#include "MonitorFxChain.h"
#include "SpeakerSetManager.h"

namespace DAW {

/**
 * MonitorEngine — the control-room signal processor.
 *
 * Signal flow (monitor path only — never touches render/export):
 *
 *   [render tap L/R]
 *       → MonitorFxChain  (speaker correction, headphone EQ — future)
 *       → DimMuteMonoProcessor  (dim / mute / mono / gain)
 *       → SpeakerSet trim
 *       → monitor meter tap
 *       → hardware output
 *
 * The render tap is captured BEFORE calling processMonitorPath().
 * This engine modifies the hardware output in-place.
 */
class MonitorEngine
{
public:
    void prepare(double sampleRate, int blockSize) noexcept
    {
        dimMuteMono_.prepare(sampleRate, blockSize);
        fxChain_.prepare(sampleRate, blockSize);
    }

    void releaseResources() noexcept
    {
        fxChain_.releaseResources();
    }

    void processMonitorPath(float* L, float* R, int numSamples,
                            const MonitorStateModel& state,
                            const SpeakerSetManager& speakers) noexcept
    {
        // 1. Monitor FX (speaker correction etc.) — bypassed in v1
        const bool fxBypassed = state.monitorFxBypassed.load(std::memory_order_relaxed);
        fxChain_.process(L, R, numSamples, fxBypassed);

        // 2. Dim / mute / mono / monitor gain
        dimMuteMono_.process(L, R, numSamples, state);

        // 3. Speaker set trim
        const int activeSet = state.activeSpeakerSet.load(std::memory_order_relaxed);
        const float trimGain = speakers.getTrimGainLinear(activeSet);
        if (std::abs(trimGain - 1.0f) > 0.0001f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] *= trimGain;
                R[i] *= trimGain;
            }
        }

        // 4. Monitor meter peaks (UI reads these on its timer)
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float al = std::abs(L[i]);
            float ar = std::abs(R[i]);
            if (al > peakL) peakL = al;
            if (ar > peakR) peakR = ar;
        }
        state.meterPeakL.store(peakL, std::memory_order_relaxed);
        state.meterPeakR.store(peakR, std::memory_order_relaxed);
    }

private:
    DimMuteMonoProcessor dimMuteMono_;
    MonitorFxChain       fxChain_;
};

} // namespace DAW
