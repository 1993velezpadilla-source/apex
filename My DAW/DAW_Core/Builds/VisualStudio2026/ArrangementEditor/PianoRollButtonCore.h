// ===========================================================================
// PianoRollButtonCore.h
// Standalone button to open Piano Roll
// Can be placed in: clips, mixer, view menu, toolbar
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <functional>

namespace ArrangementEditor
{
    class PianoRollButtonCore : public juce::Component
    {
    public:
        enum class Style
        {
            ClipOverlay,    // Small button on clip (top-right)
            MixerButton,    // Button in mixer track header
            ToolbarButton,  // Large button in toolbar
            MenuButton      // Text button in View menu
        };

        PianoRollButtonCore(Style style = Style::ToolbarButton);

        void paint(juce::Graphics& g) override;
        void resized() override {}

        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;

        std::function<void()> onClick;

    private:
        Style m_style;
        bool m_hovered = false;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
