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
 * MaxLR  : juce::jmax(peakL, peakR) — default for stereo source visualisation
 * LeftOnly  : peakL — useful for inspecting one channel of a stereo pair
 * RightOnly : peakR
 * Average   : (peakL + peakR) * 0.5f — closer to perceived loudness
 * Sum       : peakL + peakR — exposes phase relationships when summing to mono
 *
 * The L+R toggle in InputTrimFloatingPanel cycles between the first three
 * modes (Max, L, R) on click. Average and Sum are reserved for a future
 * right-click context menu on the VU meter itself.
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
        case VuChannelMode::MaxLR:     return "L+R";
        case VuChannelMode::LeftOnly:  return "L";
        case VuChannelMode::RightOnly: return "R";
        case VuChannelMode::Average:   return "AVG";
        case VuChannelMode::Sum:       return "SUM";
    }
    return "L+R";
}

} // namespace DAW
