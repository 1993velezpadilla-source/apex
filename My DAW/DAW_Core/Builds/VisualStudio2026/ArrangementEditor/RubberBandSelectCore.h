// ===========================================================================
// RubberBandSelectCore.h
// Selection rectangle drawn during drag (Select tool).
// Selects all clips intersecting the rectangle on mouse up.
// Bubblegum Blob v3 — clear inside, crisp pink-glow outline (static, like a
// default selection box, no animation).
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

        /** Fires only when the marquee bounds actually change. */
        std::function<void(juce::Rectangle<int>)> onSelectionChanged;
        std::function<void(juce::Rectangle<int>)> onSelectionComplete;

    private:
        juce::Point<int> m_startPos;
        juce::Point<int> m_currentPos;
        bool m_active = false;
    };

} // namespace ArrangementEditor
