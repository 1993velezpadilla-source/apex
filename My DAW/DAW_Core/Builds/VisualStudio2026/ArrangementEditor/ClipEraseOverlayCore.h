// ===========================================================================
// ClipEraseOverlayCore.h
// Red tint overlay on hovered clip when Eraser tool is active.
// Fades in smoothly on hover.
// ===========================================================================
#pragma once
#include <JuceHeader.h>

namespace ArrangementEditor
{
    class ClipEraseOverlayCore : public juce::Component, private juce::Timer
    {
    public:
        ClipEraseOverlayCore();

        void showOverlay(bool show);

        void paint(juce::Graphics& g) override;
        void resized() override {}

    private:
        void timerCallback() override;

        float m_alpha = 0.f;
        bool  m_visible = false;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
