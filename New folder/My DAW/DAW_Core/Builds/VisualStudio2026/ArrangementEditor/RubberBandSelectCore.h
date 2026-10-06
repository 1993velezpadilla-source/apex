// ===========================================================================
// RubberBandSelectCore.h
// Selection rectangle drawn during drag (Select tool).
// Selects all clips intersecting the rectangle on mouse up.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <functional>

namespace ArrangementEditor
{
    class RubberBandSelectCore : public juce::Component
    {
    public:
        RubberBandSelectCore();

        void startDrag(juce::Point<int> startPos);
        void updateDrag(juce::Point<int> currentPos);
        void endDrag();

        juce::Rectangle<int> getSelectionBounds() const;

        void paint(juce::Graphics& g) override;
        void resized() override {}

        std::function<void(juce::Rectangle<int>)> onSelectionComplete;

    private:
        juce::Point<int> m_startPos;
        juce::Point<int> m_currentPos;
        bool m_active = false;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
