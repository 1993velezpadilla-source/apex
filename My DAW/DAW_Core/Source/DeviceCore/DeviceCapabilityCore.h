#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace DAW::DeviceCapability {

/**
    Pure device-capability selection logic (2026-07-25 sample-rate /
    32-sample-buffer expansion).

    The device/driver is the authority for selectable sample rates and
    buffer sizes (Brain §3: the result of negotiation is authoritative).
    These functions contain NO device types (juce_audio_devices is not
    required) so they are unit-testable in the console test project;
    DevicePanelModelCore / DeviceSessionCore forward to them.
*/

/** Professional rates APEX may offer, ascending. */
inline juce::Array<double> professionalSampleRates()
{
    return { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
}

/** Buffer sizes APEX may offer, ascending (32 is ultra-low-latency). */
inline juce::Array<int> supportedBufferSizeLadder()
{
    return { 32, 64, 128, 256, 512, 1024, 2048 };
}

/** deviceRates ∩ professional set, ascending by professional-set order, deduped.
    Match tolerance is 0.01 Hz: JUCE enumerations carry the exact probed
    values; a device reporting 48000.5 is NOT offering 48000. */
inline juce::Array<double> intersectProfessionalRates(const juce::Array<double>& deviceRates)
{
    juce::Array<double> out;
    for (const double professional : professionalSampleRates())
        for (const double deviceRate : deviceRates)
            if (std::abs (deviceRate - professional) < 0.01 && ! out.contains (professional))
                out.add (professional);
    return out;
}

/** deviceSizes ∩ supported ladder, ascending by ladder order, deduped. */
inline juce::Array<int> filterSupportedBufferSizes(const juce::Array<int>& deviceSizes)
{
    juce::Array<int> out;
    for (const int ladder : supportedBufferSizeLadder())
        if (deviceSizes.contains (ladder) && ! out.contains (ladder))
            out.add (ladder);
    return out;
}

/** 32 samples is EXPERIMENTAL_ULTRA_LOW_LATENCY until Apollo Solo hardware
    validation passes. It must only ever be offered when the driver reports it. */
inline bool isExperimentalUltraLowLatency(int bufferSize) noexcept
{
    return bufferSize == 32;
}

inline juce::String formatBufferSizeLabel(int bufferSize)
{
    return isExperimentalUltraLowLatency (bufferSize)
        ? juce::String (bufferSize) + " (experimental)"
        : juce::String (bufferSize);
}

struct CapabilityCheckResult
{
    bool ok = true;
    const char* reason = "";
};

/** Pure capability-membership check. Empty lists = advisory mode (no
    enforcement; JUCE/driver nearest-match + read-back govern). */
inline CapabilityCheckResult checkAgainstSupportedConfig(double sampleRate, int bufferSize,
                                                         const juce::Array<double>& rates,
                                                         const juce::Array<int>& sizes)
{
    if (! rates.isEmpty())
    {
        bool found = false;
        for (const double supported : rates)
            if (std::abs (supported - sampleRate) < 0.01)
                found = true;
        if (! found)
            return { false, "Selected sample rate is not supported by this device." };
    }

    if (! sizes.isEmpty() && ! sizes.contains (bufferSize))
        return { false, "Selected buffer size is not supported by this device." };

    return { true, "" };
}

} // namespace DAW::DeviceCapability
