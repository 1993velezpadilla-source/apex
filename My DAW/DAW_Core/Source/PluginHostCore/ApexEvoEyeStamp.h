#pragma once
// One canonical APEX eye stamp for *all* native plugin editors.
// On Windows this calls the exact open-eye geometry already used by the
// boot animation in SplashScreen.h. No duplicate image asset or animation.
#include <JuceHeader.h>
#if JUCE_WINDOWS
#include "../SplashScreen.h"
#endif

namespace APEX::Branding
{
class EvoEyeStamp final
{
public:
    static void paint(juce::Graphics& g, juce::Rectangle<float> target)
    {
        if (target.isEmpty())
            return;
#if JUCE_WINDOWS
        // Local static: first plugin editor initializes the shared stamp on
        // JUCE's message thread; later plugins reuse the same in-memory art.
        static const juce::Image openEye = DAWSplashScreen::renderOpenEyeStamp();
        if (openEye.isValid())
        {
            g.drawImageWithin(openEye, juce::roundToInt(target.getX()),
                juce::roundToInt(target.getY()),
                juce::jmax(1, juce::roundToInt(target.getWidth())),
                juce::jmax(1, juce::roundToInt(target.getHeight())),
                juce::RectanglePlacement::centred, false);
        }
#else
        // The canonical splash is Windows GDI+. Don't substitute a
        // different logo or pretend a newly invented graphic is identical.
        juce::ignoreUnused(g);
#endif
    }
};
} // namespace APEX::Branding
