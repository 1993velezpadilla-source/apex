#pragma once
#include <JuceHeader.h>

namespace DAW {

// ===========================================================================
// ClipColourDefaults.h
// Centralized default colour for newly created audio clips.
//
// VIOLET GALAXY VOID — the single source of truth for the default colour of
// every newly created audio clip (recorded, imported, dragged, bounced,
// rendered, consolidated, frozen, or created inside the Audio Editor).
//
// Rules enforced by this module:
//   * One centralized setting — never hardcode the colour in multiple places.
//   * Existing clips with custom colours are never recoloured.
//   * Opening an older project never recolours its clips (colours are
//     persisted per-clip inside the project file).
//   * The user can always change a clip's colour manually afterwards.
// ===========================================================================
struct ClipColourDefaults
{
    /** VIOLET GALAXY VOID — the single source of truth for the default colour
     *  of newly created audio clips.
     *  HEX: #191235  |  RGB: 25, 18, 53  |  HSL: 252°, 49%, 14% */
    static juce::Colour defaultAudioClipColour() noexcept
    {
        return juce::Colour(0xFF191235);
    }

    /** Legacy fallback used when a persisted colour string is invalid. */
    static juce::Colour fallbackClipColour() noexcept
    {
        return juce::Colour(0xFF808080);
    }
};

} // namespace DAW