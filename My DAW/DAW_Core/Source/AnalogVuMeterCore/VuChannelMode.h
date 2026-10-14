#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * VuChannelMode
 *
 * What signal the VU meter displays from a stereo source. The InputMeterCore
 * provides separate L and R atomics; this enum tells the meter Component
 * how to combine them into a single needle reading.
 *
 * MaxLR  : max of the two detected levels
 * LeftOnly  : peakL — useful for inspecting one channel of a stereo pair
 * RightOnly : peakR
 * Average   : mean of independently detected L/R levels (default stereo VU)
 * Sum       : sum of detected levels, not a phase-aware mono waveform sum
 *
 * The panel cycles Stereo (Average), Left, Right, Max. Sum is retained for
 * existing programmatic consumers; it is not a mid/side detector.
 */
enum class VuChannelMode
{
    MaxLR,
    LeftOnly,
    RightOnly,
    Average,
    Sum
};

/** Combine two channel peak values according to the mode. */
inline float combineChannelPeaks(float peakL, float peakR, VuChannelMode mode) noexcept
{
    switch (mode)
    {
        case VuChannelMode::MaxLR:     return juce::jmax(peakL, peakR);
        case VuChannelMode::LeftOnly:  return peakL;
        case VuChannelMode::RightOnly: return peakR;
        case VuChannelMode::Average:   return (peakL + peakR) * 0.5f;
        case VuChannelMode::Sum:       return peakL + peakR;
    }
    return juce::jmax(peakL, peakR);
}

/** Short label string for displaying the current mode in UI (e.g. on the toggle). */
inline juce::String channelModeLabel(VuChannelMode mode) noexcept
{
    switch (mode)
    {
        case VuChannelMode::MaxLR:     return "MAX";
        case VuChannelMode::LeftOnly:  return "L";
        case VuChannelMode::RightOnly: return "R";
        case VuChannelMode::Average:   return "L+R";
        case VuChannelMode::Sum:       return "SUM";
    }
    return "L+R";
}

} // namespace DAW
