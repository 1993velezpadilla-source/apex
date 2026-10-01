#pragma once
#include <JuceHeader.h>
#include "FaderRangeCore.h"

namespace DAW {

/**
 * MixerScaleColorModel — color stops for the MIXER FADER GAIN SCALE.
 *
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║  THIS IS FOR THE FADER GAIN SCALE — NOT the signal peak meter. ║
 * ║  The signal/peak meter uses its own color gradient in           ║
 * ║  LevelMeter.h (green→yellow→red based on signal dB).           ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * Color thresholds are constant in dB. Y positions come from FaderRangeCore
 * so they automatically reposition when format changes.
 */
namespace MixerScaleColorModel
{
    // ── Canonical colors ─────────────────────────────────────────────
    inline juce::Colour green()   { return juce::Colour(0xFF22C55E); }
    inline juce::Colour yellow()  { return juce::Colour(0xFFEAB308); }
    inline juce::Colour orange()  { return juce::Colour(0xFFF97316); }
    inline juce::Colour red()     { return juce::Colour(0xFFEF4444); }
    inline juce::Colour deepRed() { return juce::Colour(0xFFDC2626); }

    // ── dB thresholds (constant in dB — positions re-derive from core) ──
    static constexpr float kGreenEndDb  = -12.0f;
    static constexpr float kYellowEndDb =  -2.0f;
    static constexpr float kOrangeEndDb =   0.0f;
    static constexpr float kRedEndDb    =  +2.0f;

    // ── Normalized positions via FaderRangeCore ──────────────────────
    inline float greenEndPos (const FaderRangeCore& c) { return c.dbToNorm(kGreenEndDb);  }
    inline float yellowEndPos(const FaderRangeCore& c) { return c.dbToNorm(kYellowEndDb); }
    inline float orangeEndPos(const FaderRangeCore& c) { return c.dbToNorm(kOrangeEndDb); }
    inline float redEndPos   (const FaderRangeCore& c) { return c.dbToNorm(kRedEndDb);    }

    // Global-instance convenience overloads
    inline float greenEndPos()  { if (auto* c = FaderRangeCore::getGlobalInstance()) return greenEndPos(*c);  return 0.5f; }
    inline float yellowEndPos() { if (auto* c = FaderRangeCore::getGlobalInstance()) return yellowEndPos(*c); return 0.7f; }
    inline float orangeEndPos() { if (auto* c = FaderRangeCore::getGlobalInstance()) return orangeEndPos(*c); return 0.8f; }
    inline float redEndPos()    { if (auto* c = FaderRangeCore::getGlobalInstance()) return redEndPos(*c);    return 0.9f; }

    inline juce::Colour colorAtPosition(float pos)
    {
        if (pos <= greenEndPos())  return green();
        if (pos <= yellowEndPos()) return yellow();
        if (pos <= orangeEndPos()) return orange();
        if (pos <= redEndPos())    return red();
        return deepRed();
    }

    inline juce::Colour colorAtDb(float db)
    {
        if (auto* c = FaderRangeCore::getGlobalInstance())
            return colorAtPosition(c->dbToNorm(db));
        return green();
    }

    /**
     * Builds a JUCE ColourGradient for the fader track fill.
     * Color stop positions come from FaderRangeCore so they adapt to format.
     */
    inline juce::ColourGradient buildFaderGradient(float topY, float bottomY,
                                                   const FaderRangeCore& core)
    {
        juce::ColourGradient grad(green(), 0, bottomY, deepRed(), 0, topY, false);
        grad.addColour((double)greenEndPos(core),  green());
        grad.addColour((double)greenEndPos(core)  + 0.001, yellow());
        grad.addColour((double)yellowEndPos(core), yellow());
        grad.addColour((double)yellowEndPos(core) + 0.001, orange());
        grad.addColour((double)orangeEndPos(core), orange());
        grad.addColour((double)orangeEndPos(core) + 0.001, red());
        grad.addColour((double)redEndPos(core),    red());
        grad.addColour((double)redEndPos(core)    + 0.001, deepRed());
        return grad;
    }

    // Global-instance convenience overload
    inline juce::ColourGradient buildFaderGradient(float topY, float bottomY)
    {
        if (auto* c = FaderRangeCore::getGlobalInstance())
            return buildFaderGradient(topY, bottomY, *c);
        return juce::ColourGradient(green(), 0, bottomY, deepRed(), 0, topY, false);
    }
}

} // namespace DAW
