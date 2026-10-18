#pragma once
// APEX native plugin identity: static OPEN Evo Eye stamp. No APEX letters,
// splash controls, animation timer, JPEG/PNG assets or global audio callbacks.
// Shapes below follow the same angry upper/lower-lid and mirrored forged-brow
// Beziers as Source/SplashScreen.h (APEX Evo Eye V4).
// Only apply to our own APEX-native editors, never third-party plugin GUIs.
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>

namespace APEX::UI
{
class EvoEyeStamp final : public juce::Component
{
public:
    EvoEyeStamp()
    {
        setName ("APEX Evo Eye - open / static native stamp");
        setTitle ("Evo Eye"); // accessibility; no on-screen text
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
        setAlwaysOnTop (true);
    }

    static constexpr int kWidth = 70;
    static constexpr int kHeight = 27;

    // Stable corner and touch-safe dimensions. The stamp is purely decorative
    // and must never steal clicks from the host or editor controls beneath it.
    void placeInCorner (juce::Component& editor)
    {
        const int width = std::min (kWidth, std::max (28, editor.getWidth() / 6));
        const int height = std::min (kHeight, std::max (16, width * kHeight / kWidth));
        setBounds (std::max (2, editor.getWidth() - width - 9),
                   6, width, height);
        toFront (false);
    }

    void paint (juce::Graphics& g) override
    {
        const float scale = std::min (
            (static_cast<float> (getWidth()) - 2.0f) / 1040.0f,
            (static_cast<float> (getHeight()) - 2.0f) / 345.0f);
        if (scale <= 0.0f)
            return;

        // Design coordinate space: identical eyelid contour geometry as the
        // open-state 1200x750 splash, cropped to its eyes only. No wordmark.
        const float tx = (getWidth() - 1040.0f * scale) * 0.5f - 80.0f * scale;
        const float ty = (getHeight() - 345.0f * scale) * 0.5f - 126.0f * scale;
        g.addTransform (juce::AffineTransform::scale (scale).translated (tx, ty));

        juce::Path aperture;
        aperture.startNewSubPath (213.0f, 304.0f);
        aperture.cubicTo (331.0f, 210.0f, 468.0f, 244.0f, 600.0f, 283.0f);
        aperture.cubicTo (732.0f, 244.0f, 869.0f, 210.0f, 987.0f, 304.0f);
        aperture.cubicTo (868.0f, 459.0f, 734.0f, 446.0f, 600.0f, 442.0f);
        aperture.cubicTo (466.0f, 446.0f, 332.0f, 459.0f, 213.0f, 304.0f);
        aperture.closeSubPath();

        g.saveState();
        g.reduceClipRegion (aperture);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff0c000c), 600.0f, 206.0f,
            juce::Colour (0xff420a32), 600.0f, 470.0f, false));
        g.fillPath (aperture);
        g.setColour (juce::Colour (0xaaae176c));
        g.fillEllipse (375.0f, 194.0f, 454.0f, 233.0f);
        g.setColour (juce::Colour (0x90f21d5e));
        g.fillEllipse (470.0f, 221.0f, 260.0f, 187.0f);
        // Fine static radial plasma fibres: always OPEN, no blink timer.
        for (int i = 0; i < 32; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi
                              * static_cast<float> (i) / 32.0f;
            const float cs = std::cos (angle);
            const float sn = std::sin (angle);
            juce::Path vein;
            vein.startNewSubPath (600.0f + cs * 48.0f,
                                  304.0f + sn * 41.0f);
            vein.cubicTo (600.0f + cs * 75.0f - sn * 9.0f,
                          304.0f + sn * 66.0f + cs * 9.0f,
                          600.0f + cs * 117.0f + sn * 8.0f,
                          304.0f + sn * 100.0f - cs * 8.0f,
                          600.0f + cs * 152.0f,
                          304.0f + sn * 131.0f);
            g.setColour (i % 4 == 0 ? juce::Colour (0xccf8aacd)
                                   : juce::Colour (0x8ae12d83));
            g.strokePath (vein, juce::PathStrokeType (2.4f));
        }
        g.setColour (juce::Colour (0xff09020d));
        g.fillEllipse (554.0f, 249.0f, 92.0f, 119.0f);
        g.setColour (juce::Colour (0xfff32471));
        g.fillRoundedRectangle (587.0f, 258.0f, 24.0f, 100.0f, 11.0f);
        g.setColour (juce::Colour (0xfffee6f2));
        g.fillRoundedRectangle (594.0f, 263.0f, 8.0f, 91.0f, 4.0f);
        g.restoreState();

        // OPEN lower lid steel, without extra McDonald's-M upper arches.
        juce::Path lower;
        lower.startNewSubPath (987.0f, 304.0f);
        lower.cubicTo (868.0f, 459.0f, 734.0f, 446.0f, 600.0f, 442.0f);
        lower.cubicTo (466.0f, 446.0f, 332.0f, 459.0f, 213.0f, 304.0f);
        g.setColour (juce::Colour (0xff090b18));
        g.strokePath (lower, juce::PathStrokeType (9.0f));
        g.setColour (juce::Colour (0xff79819b));
        g.strokePath (lower, juce::PathStrokeType (3.0f));
        g.setColour (juce::Colour (0xeef41c60));
        g.strokePath (lower, juce::PathStrokeType (1.6f));

        // Match the forged scowling brows: same curves, complete on BOTH sides.
        juce::Path brow;
        brow.startNewSubPath (91.0f, 152.0f);
        brow.cubicTo (211.0f, 155.0f, 464.0f, 207.0f, 585.0f, 253.0f);
        brow.lineTo (570.0f, 273.0f);
        brow.cubicTo (439.0f, 241.0f, 278.0f, 206.0f, 172.0f, 226.0f);
        brow.cubicTo (131.0f, 224.0f, 106.0f, 195.0f, 91.0f, 152.0f);
        brow.closeSubPath();
        paintBrow (g, brow);
        juce::Path right (brow);
        right.applyTransform (juce::AffineTransform (
            -1.0f, 0.0f, 1200.0f, 0.0f, 1.0f, 0.0f));
        paintBrow (g, right);
    }

private:
    static void paintBrow (juce::Graphics& g, const juce::Path& path)
    {
        const auto bounds = path.getBounds();
        g.setColour (juce::Colour (0x66f01858));
        g.strokePath (path, juce::PathStrokeType (12.0f));
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xffb1b8d3), bounds.getX(), bounds.getY(),
            juce::Colour (0xff121626), bounds.getX(), bounds.getBottom(), false));
        g.fillPath (path);
        g.setColour (juce::Colour (0xffdbe5fc));
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }
};
} // namespace APEX::UI
