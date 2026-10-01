// ===========================================================================
// ClipWaveformColourCore.h
// APEX waveform colour authority — the waveform visually BELONGS to its
// TRACK. The colour is derived from the canonical track colour (mirrored
// into ArrangementClipModel::trackColour by the engine bridge), never from
// the clip's own colour and never from a hard-coded default.
//
// Contrast/readability: a small canonical brighten lifts the colour off the
// dark clip body; muted reduces presence via alpha; selected lifts further.
// All adjustments stay in the APEX visual family — no hue shifts.
// ===========================================================================
#pragma once
#include <JuceHeader.h>

namespace ArrangementEditor
{

struct ClipWaveformColourCore
{
    /** Resolve the paint colour for a waveform on a track.
     *  @param trackColour  the canonical Track colour (authority).
     *  @param muted        clip/track muted state — reduced presence.
     *  @param selected     clip selection — canonical lift.
     *  Returns a colour that clearly reads as the track's colour. */
    static juce::Colour resolveWaveformColour(juce::Colour trackColour,
                                              bool muted,
                                              bool selected)
    {
        // Graceful fallback only when no valid track colour exists.
        if (!trackColour.isOpaque())
            trackColour = juce::Colour(0xFF087BFF); // APEX signal blue

        auto c = trackColour.brighter(muted ? 0.22f : 0.32f);
        c = c.withAlpha(muted ? 0.45f : 0.85f);
        if (selected)
            c = c.brighter(0.25f).withAlpha(0.95f);
        return c;
    }

    /** True when both colours share the same hue family (the APEX visual
     *  identity check used by regressions). */
    static bool sameHueFamily(const juce::Colour& a, const juce::Colour& b)
    {
        const float ha = a.getHue();
        const float hb = b.getHue();
        const float d = std::fabs(ha - hb);
        return d < 0.03f || (1.0f - d) < 0.03f;
    }
};

} // namespace ArrangementEditor
