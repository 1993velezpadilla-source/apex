#pragma once

#include <JuceHeader.h>
#include <vector>

#include "BubblegumCableStyleSettingsCore.h"

namespace DAW::BubblegumAppearanceSettings
{
    enum class PaletteFamily
    {
        Neon = 0,
        Regular
    };

    struct PaletteChoice
    {
        PaletteFamily family = PaletteFamily::Neon;
        int index = 0;
    };

    struct ColourPreset
    {
        const char* name;
        juce::Colour colour;
    };

    inline juce::Colour getDefaultSelectedTrackAccent() noexcept
    {
        return juce::Colour(0xFFFF2D78);
    }

    inline juce::Colour getClassicCablePink() noexcept
    {
        return juce::Colour::fromFloatRGBA(0.95f, 0.44f, 0.66f, 1.00f);
    }

    inline juce::Colour getDefaultCableAccent() noexcept
    {
        return getDefaultSelectedTrackAccent();
    }

    inline PaletteChoice getDefaultSelectedTrackChoice() noexcept
    {
        return {};
    }

    inline PaletteChoice getDefaultCableChoice() noexcept
    {
        return {};
    }

    inline const std::vector<ColourPreset>& getSelectedTrackPresets(PaletteFamily family)
    {
        static const std::vector<ColourPreset> neon
        {
            { "Neon Pink",   getDefaultSelectedTrackAccent() },
            { "Neon Violet", juce::Colour(0xFFA259FF) },
            { "Neon Blue",   juce::Colour(0xFF3FA9FF) },
            { "Neon Mint",   juce::Colour(0xFF29F0B4) },
            { "Neon Amber",  juce::Colour(0xFFFFB347) }
        };

        static const std::vector<ColourPreset> regular
        {
            { "Royal Purple", juce::Colour(0xFF7C3AED) },
            { "Crimson",      juce::Colour(0xFFB94A68) },
            { "Steel Blue",   juce::Colour(0xFF5E81AC) },
            { "Forest",       juce::Colour(0xFF5E8C61) },
            { "Gold",         juce::Colour(0xFFD4AF7F) }
        };

        return family == PaletteFamily::Regular ? regular : neon;
    }

    inline const std::vector<ColourPreset>& getCablePresets(PaletteFamily family)
    {
        static const std::vector<ColourPreset> neon
        {
            { "Neon Pink",      getDefaultCableAccent() },
            { "Bubblegum Pink", getClassicCablePink() },
            { "Neon Violet",    juce::Colour(0xFFA259FF) },
            { "Neon Cyan",      juce::Colour(0xFF41D9FF) },
            { "Neon Lime",      juce::Colour(0xFF7DFF7A) }
        };

        static const std::vector<ColourPreset> regular
        {
            { "Rose",       juce::Colour(0xFFC76C8D) },
            { "Lavender",   juce::Colour(0xFF8C7AE6) },
            { "Slate Blue", juce::Colour(0xFF6382C7) },
            { "Copper",     juce::Colour(0xFFB87951) },
            { "Silver",     juce::Colour(0xFFB8C0CC) }
        };

        return family == PaletteFamily::Regular ? regular : neon;
    }

    inline int clampPresetIndex(bool forCable, PaletteFamily family, int index) noexcept
    {
        const auto& presets = forCable ? getCablePresets(family)
                                       : getSelectedTrackPresets(family);
        if (presets.empty())
            return 0;

        return juce::jlimit(0, (int) presets.size() - 1, index);
    }

    inline juce::Colour resolveColour(bool forCable, PaletteFamily family, int index) noexcept
    {
        const auto& presets = forCable ? getCablePresets(family)
                                       : getSelectedTrackPresets(family);
        if (presets.empty())
            return forCable ? getDefaultCableAccent() : getDefaultSelectedTrackAccent();

        return presets[(size_t) clampPresetIndex(forCable, family, index)].colour;
    }

    inline juce::String resolvePresetName(bool forCable, PaletteFamily family, int index)
    {
        const auto& presets = forCable ? getCablePresets(family)
                                       : getSelectedTrackPresets(family);
        if (presets.empty())
            return {};

        return presets[(size_t) clampPresetIndex(forCable, family, index)].name;
    }

    inline bubblegum::BubblegumCableStyleSettingsCore::Style makeCableStyleForAccent(juce::Colour accent) noexcept
    {
        bubblegum::BubblegumCableStyleSettingsCore::Style style;

        const auto pearlTop      = accent.interpolatedWith(juce::Colours::white, 0.87f)
                                       .interpolatedWith(juce::Colour(0xFFFFEEE5), 0.14f);
        const auto pearlCore     = accent.interpolatedWith(juce::Colours::white, 0.93f)
                                       .interpolatedWith(juce::Colour(0xFFFFF8F2), 0.16f);
        const auto smokedBody    = accent.interpolatedWith(juce::Colour(0xFF1B0A12), 0.42f);
        const auto deepShadow    = accent.darker(2.4f).interpolatedWith(juce::Colours::black, 0.50f);
        const auto softOutline   = accent.interpolatedWith(juce::Colour(0xFFFFE0CF), 0.34f);
        const auto softHighlight = accent.interpolatedWith(juce::Colours::white, 0.95f)
                                       .interpolatedWith(juce::Colour(0xFFFFEEDB), 0.12f);

        style.bodyTop        = pearlTop.withAlpha(0.86f);
        style.bodyBottom     = smokedBody.withAlpha(0.96f);
        style.coreTop        = pearlCore.withAlpha(0.64f);
        style.coreBottom     = accent.interpolatedWith(juce::Colour(0xFFFFD6E1), 0.18f).withAlpha(0.18f);
        style.mistTop        = pearlTop.withAlpha(0.022f);
        style.mistBottom     = accent.interpolatedWith(deepShadow, 0.26f).withAlpha(0.09f);
        style.shadow         = deepShadow.withAlpha(0.20f);
        style.highlightSoft  = softHighlight.withAlpha(0.16f);
        style.highlightSharp = softHighlight.withAlpha(0.44f);
        style.outline        = softOutline.withAlpha(0.12f);
        style.splash         = pearlTop.withAlpha(0.30f);
        style.droplet        = softHighlight.withAlpha(0.36f);

        return style;
    }
}
