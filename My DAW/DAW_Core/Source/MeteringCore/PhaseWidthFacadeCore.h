#pragma once
#include <JuceHeader.h>
#include "CorrelationMeterCore.h"
#include "StereoWidthCore.h"
#include "MonoCheckProcessor.h"

namespace DAW {

/**
 * PhaseWidthFacadeCore — single-owner facade for the three phase/width/mono cores.
 *
 * Aggregates CorrelationMeterCore, StereoWidthCore, and MonoCheckProcessor into
 * one object that can be embedded by value in ControlRoomEngine.
 *
 * Processing order inside processBlock():
 *   1. Correlation meter   — reads original signal, no write
 *   2. Stereo width meter  — reads original signal, no write
 *   3. MonoCheckProcessor  — may write (L+R)/2 into both channels if enabled
 *
 * Metering always measures the pre-mono signal; mono check writes last.
 * This matches standard control-room behaviour: meters show the true stereo
 * picture, audible mono check is an optional override.
 */
class PhaseWidthFacadeCore
{
public:
    PhaseWidthFacadeCore() = default;

    void prepare(double sampleRate, int /*blockSize*/) noexcept
    {
        correlation_.prepare(sampleRate);
        width_.prepare(sampleRate);
        monoCheck_.prepare(sampleRate);
    }

    void releaseResources() noexcept
    {
        correlation_.reset();
        width_.reset();
        monoCheck_.setEnabled(false);
    }

    /** Audio thread. Feed one block of stereo data (in-place — may modify if mono active). */
    void processBlock(float* L, float* R, int numSamples) noexcept
    {
        if (L == nullptr || R == nullptr || numSamples <= 0) return;
        correlation_.processBlock(L, R, numSamples);
        width_.processBlock(L, R, numSamples);
        monoCheck_.processBlock(L, R, numSamples);
    }

    // ── Accessors (UI thread) ──────────────────────────────────────────
    CorrelationMeterCore&       getCorrelation()  noexcept { return correlation_; }
    const CorrelationMeterCore& getCorrelation()  const noexcept { return correlation_; }
    StereoWidthCore&            getWidth()        noexcept { return width_; }
    const StereoWidthCore&      getWidth()        const noexcept { return width_; }
    MonoCheckProcessor&         getMonoCheck()    noexcept { return monoCheck_; }
    const MonoCheckProcessor&   getMonoCheck()    const noexcept { return monoCheck_; }

private:
    CorrelationMeterCore correlation_;
    StereoWidthCore      width_;
    MonoCheckProcessor   monoCheck_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseWidthFacadeCore)
};

} // namespace DAW
