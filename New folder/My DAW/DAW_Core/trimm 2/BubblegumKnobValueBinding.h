#pragma once
#include <JuceHeader.h>
#include <functional>

namespace DAW {

/**
 * BubblegumKnobValueBinding
 *
 * Glue between a knob and an external per-track value source. The knob never
 * owns or caches the value — every read goes through getValue, every write
 * through setValue. Each panel constructs a fresh binding pointing at the
 * active track's parameter, which guarantees the knob always reflects/edits
 * a unique value per track without any state bleeding across tracks.
 *
 * Typical use from a panel binding to a track's input trim:
 *
 *   BubblegumKnobValueBinding b;
 *   b.getValue    = [&t]      { return t.getInputTrim().getTargetGainDb(); };
 *   b.setValue    = [&t](float dB){ t.getInputTrim().setTargetGainDb(dB); };
 *   b.formatValue = [](float dB){ return dB <= -120.0f ? juce::String("-inf")
 *                                                       : juce::String(dB, 1) + " dB"; };
 *   b.minValue     = -24.0f;
 *   b.maxValue     =  24.0f;
 *   b.defaultValue =   0.0f;
 *
 * The min/max here are the KNOB visual range, not necessarily the parameter's
 * full range. The trim parameter may allow -120..+24 dB internally; the knob
 * surface only spans -24..+24 with extreme attenuation handled by a separate
 * mute control.
 */
struct BubblegumKnobValueBinding
{
    std::function<float()>             getValue;
    std::function<void(float)>         setValue;
    std::function<juce::String(float)> formatValue;

    float minValue     { 0.0f };
    float maxValue     { 1.0f };
    float defaultValue { 0.0f };

    bool isValid() const noexcept
    {
        return getValue != nullptr && setValue != nullptr;
    }

    float read() const
    {
        return getValue ? juce::jlimit(minValue, maxValue, getValue())
                        : defaultValue;
    }

    void write(float v) const
    {
        if (setValue) setValue(juce::jlimit(minValue, maxValue, v));
    }

    juce::String format(float v) const
    {
        return formatValue ? formatValue(v) : juce::String(v, 2);
    }
};

} // namespace DAW
