#pragma once
#include <JuceHeader.h>
#include <set>

namespace DAW {

/**
 * SoloSafeCore — manages solo-safe state per track/bus.
 *
 * Solo-safe tracks/buses are NEVER muted by solo logic.
 * Typical solo-safe targets: FX return buses, reverb buses,
 * delay buses, parallel compression buses.
 *
 * Architectural rule: solo-safe state is independent of solo state.
 * A track can be both soloed AND solo-safe (solo-safe takes priority).
 */
class SoloSafeCore
{
public:
    void setSoloSafe(const juce::String& trackOrBusId, bool safe)
    {
        if (safe)
            soloSafeIds_.insert(trackOrBusId);
        else
            soloSafeIds_.erase(trackOrBusId);
    }

    bool isSoloSafe(const juce::String& trackOrBusId) const
    {
        return soloSafeIds_.count(trackOrBusId) > 0;
    }

    /** Returns true if the target should remain audible during solo.
     *  A solo-safe target is never muted by solo logic. */
    bool shouldRemainAudible(const juce::String& trackOrBusId, bool anySoloed) const
    {
        if (!anySoloed) return true;
        return isSoloSafe(trackOrBusId);
    }

    void clearAll() { soloSafeIds_.clear(); }

    juce::ValueTree getState() const
    {
        juce::ValueTree state("SoloSafe");
        int idx = 0;
        for (const auto& id : soloSafeIds_)
            state.setProperty("id_" + juce::String(idx++), id, nullptr);
        return state;
    }

private:
    std::set<juce::String> soloSafeIds_;
};

} // namespace DAW
