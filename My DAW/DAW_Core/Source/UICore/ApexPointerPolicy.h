#pragma once
#include <JuceHeader.h>

namespace DAW
{
// One platform-neutral input policy shared by Windows pen/touch and the
// future iPadOS Pencil/iPhone/Android ports. Native JUCE mouse and stylus
// semantics remain precise; only actual fingers start kinetic navigation.
struct ApexPointerPolicy
{
    enum class Kind { Mouse, Finger, Stylus };

    static Kind kind(const juce::MouseEvent& e) noexcept
    {
        switch (e.source.getType())
        {
            case juce::MouseInputSource::InputSourceType::touch: return Kind::Finger;
            case juce::MouseInputSource::InputSourceType::pen:   return Kind::Stylus;
            default:                                            return Kind::Mouse;
        }
    }

    static bool isDirect(const juce::MouseEvent& e) noexcept
    {
        return kind(e) != Kind::Mouse;
    }

    static bool shouldKineticPan(const juce::MouseEvent& e) noexcept
    {
        return kind(e) == Kind::Finger;
    }

    struct StylusSample
    {
        bool available = false;
        bool pressureValid = false;
        bool tiltXValid = false;
        bool tiltYValid = false;
        float pressure = 0.0f, tiltX = 0.0f, tiltY = 0.0f, rotation = 0.0f;
    };

    // Do not invent hardware pressure/tilt if JUCE's backend reports none.
    // This sample is ready for future automation/handwriting/piano-roll tools;
    // it does not reinterpret pressure as an audio-parameter value.
    static StylusSample stylus(const juce::MouseEvent& e) noexcept
    {
        StylusSample result;
        result.available = kind(e) == Kind::Stylus;
        if (!result.available) return result;
        result.pressureValid = e.isPressureValid();
        result.tiltXValid = e.isTiltValid(true);
        result.tiltYValid = e.isTiltValid(false);
        if (result.pressureValid) result.pressure = juce::jlimit(0.0f, 1.0f, e.pressure);
        if (result.tiltXValid) result.tiltX = e.tiltX;
        if (result.tiltYValid) result.tiltY = e.tiltY;
        if (e.isRotationValid()) result.rotation = e.rotation;
        return result;
    }
};
} // namespace DAW
