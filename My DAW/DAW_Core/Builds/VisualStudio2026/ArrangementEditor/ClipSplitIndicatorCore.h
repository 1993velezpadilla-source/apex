// ===========================================================================
// ClipSplitIndicatorCore.h
// Thin vertical line showing where split will occur (Split tool active).
// Follows mouse X position, snaps to grid if snap enabled.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <functional>

namespace ArrangementEditor
{
    class ClipSplitIndicatorCore : public juce::Component
    {
    public:
        ClipSplitIndicatorCore();

        void setPosition(double timePos);
        // Note: use Component::setVisible() instead of custom method

        void paint(juce::Graphics& g) override;
        void resized() override {}

    private:
        double m_timePos = 0.0;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
