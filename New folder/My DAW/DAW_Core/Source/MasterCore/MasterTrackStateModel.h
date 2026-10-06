#pragma once
#include <atomic>
#include <JuceHeader.h>

namespace DAW {

/**
 * MasterTrackStateModel — state for the visible Master Track / Master Channel.
 *
 * This represents the USER-FACING master channel in the mixer UI.
 * It is related to, but architecturally separate from, the internal
 * MasterBusEngine backend processing path.
 *
 * The Master Track must NOT have:
 *   - record-arm
 *   - live input monitoring
 *   - input selection
 *
 * The Master Track DOES have:
 *   - master fader (volume)
 *   - mute
 *   - pan (for stereo balance, typically centred)
 *   - insert slots
 *   - automation support
 *   - clip indicator
 *   - output assignment display
 */
struct MasterTrackStateModel
{
    std::atomic<float> masterGain   { 1.0f };   // linear gain (1.0 = 0 dB)
    std::atomic<float> masterPan    { 0.0f };   // −1..+1
    std::atomic<bool>  masterMute   { false };
    std::atomic<int>   insertCount  { 0 };      // number of active inserts

    // Meter peaks (written by audio thread)
    mutable std::atomic<float> peakL { 0.0f };
    mutable std::atomic<float> peakR { 0.0f };
    mutable std::atomic<bool>  clipped { false };

    // Output assignment display
    juce::String outputName = "Main Out";

    float getGainDb() const noexcept
    {
        float g = masterGain.load(std::memory_order_relaxed);
        return g <= 0.000001f ? -120.0f : 20.0f * std::log10(g);
    }

    void setGainDb(float db) noexcept
    {
        masterGain.store(std::pow(10.0f, db / 20.0f), std::memory_order_relaxed);
    }
};

} // namespace DAW
