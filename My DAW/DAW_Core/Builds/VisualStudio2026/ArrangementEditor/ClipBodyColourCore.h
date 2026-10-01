// ===========================================================================
// ClipBodyColourCore.h
// APEX clip-body/header tint authority.
//
// The clip body visually BELONGS to its TRACK. The tint derives from the
// canonical ArrangementClipModel::trackColour — the SAME single authority
// feeding ClipWaveformColourCore — never from the engine clip's own colour
// (which defaults to violet and breaks track coherence).
//
// Design rules (APEX dark aesthetic):
//   * hue preserved from the source colour (Track colour, or an explicit
//     per-clip override);
//   * saturation substantially reduced for the background;
//   * brightness very low — the body provides COLOR CONTEXT, the waveform
//     (significantly brighter) provides CONTENT VISIBILITY;
//   * selection/hover only lift brightness within the same family;
//   * muted drops alpha and saturation further, still in the family.
//
// Explicit per-clip colour override (ClipColourPickerCore →
// ArrangementClipModel::explicitClipColourOverride): when present, the
// override colour replaces the tint SOURCE hue for body and header, while
// keeping the same dark design language. The waveform authority
// (trackColour) is never replaced.
// ===========================================================================
#pragma once
#include <JuceHeader.h>

namespace ArrangementEditor
{

struct ClipBodyColourCore
{
    /** Resolve the deep desaturated track-family tint for the clip body.
     *  @param trackColour       canonical Track colour (default authority).
     *  @param selected          clip selection — small canonical brightness lift.
     *  @param muted             clip/track muted — lower alpha and saturation.
     *  @param hovered           hover state — small canonical brightness lift.
     *  @param explicitOverride  non-null when the user explicitly tinted this
     *                           clip; its hue replaces the track hue as the
     *                           tint SOURCE (same dark design language). */
    static juce::Colour resolveClipBodyColour(juce::Colour trackColour,
                                              bool selected,
                                              bool muted,
                                              bool hovered,
                                              const juce::Colour* explicitOverride)
    {
        juce::Colour source = explicitOverride != nullptr ? *explicitOverride : trackColour;
        if (!source.isOpaque())
            source = juce::Colour(0xFF087BFF); // APEX signal blue fallback

        const float hue  = source.getHue();
        // Saturation floor keeps the hue numerically stable and readable at
        // very low brightness (a near-black pixel with ~0 saturation has an
        // unstable hue channel and would read grey, not track-family).
        float saturation = juce::jmax(source.getSaturation() * 0.22f, 0.12f); // substantially reduced
        float brightness = 0.08f;                          // very low
        float alpha      = 0.92f;

        if (hovered)
            brightness += 0.03f;
        if (selected)
        {
            brightness += 0.05f;   // readability lift, still dark
            alpha       = 0.95f;
        }
        if (muted)
        {
            saturation = juce::jmax(saturation * 0.5f, 0.13f); // lower but still family-readable
            brightness = juce::jmax(brightness, 0.10f);
            alpha      = 0.55f;
        }

        return juce::Colour::fromHSV(hue, saturation, brightness, alpha);
    }

    /** Resolve the clip header (name strip) colour: a slightly lifted variant
     *  of the same track-family tint, keeping the name readable. An explicit
     *  per-clip override keeps its full identity (existing picker semantics:
     *  the header is the clip's identity field). */
    static juce::Colour resolveClipHeaderColour(juce::Colour trackColour,
                                                const juce::Colour* explicitOverride)
    {
        if (explicitOverride != nullptr)
            return *explicitOverride;

        if (!trackColour.isOpaque())
            trackColour = juce::Colour(0xFF087BFF); // APEX signal blue fallback

        return juce::Colour::fromHSV(trackColour.getHue(),
                                     trackColour.getSaturation() * 0.30f,
                                     0.16f,
                                     0.95f);
    }
};

} // namespace ArrangementEditor
