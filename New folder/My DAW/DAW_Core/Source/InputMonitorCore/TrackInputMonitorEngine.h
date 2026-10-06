#pragma once
#include <JuceHeader.h>
#include "TrackMonitoringStateModel.h"

namespace DAW {

/**
 * TrackInputMonitorEngine — per-track live input monitoring DSP.
 *
 * When monitoring is ON, mixes the hardware input into the track's
 * playback buffer so the performer hears themselves in real time.
 *
 * Signal flow:
 *   Hardware Input → [InputFxChain (future)] → Monitor Mix → Track Output
 *
 * Future support for:
 *   - Monitor wet, record dry (InputFxChain on monitor path only)
 *   - Monitor wet, record wet (InputFxChain on both paths)
 *   - Monitor dry, record dry (bypass InputFxChain)
 */
class TrackInputMonitorEngine
{
public:
    void prepare(double sampleRate, int blockSize) noexcept
    {
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
    }

    /**
     * Mixes live input into the track output buffer when monitoring is ON.
     *
     * @param trackOutput    The track's current playback buffer (modified in place)
     * @param hwInput        Hardware input buffer (read-only)
     * @param inputChannel   Which hardware input channel pair to read
     * @param numSamples     Block size
     * @param state          Per-track monitoring state
     * @param trackGainL     Track's left gain (volume × pan law)
     * @param trackGainR     Track's right gain
     */
    void processMonitorMix(float* outL, float* outR,
                           const float* inL, const float* inR,
                           int numSamples,
                           const TrackMonitoringStateModel& state,
                           float trackGainL, float trackGainR) noexcept
    {
        if (!state.isMonitoring()) return;
        if (!inL || !inR) return;

        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] += inL[i] * trackGainL;
            outR[i] += inR[i] * trackGainR;
        }
    }

private:
    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;
};

} // namespace DAW
