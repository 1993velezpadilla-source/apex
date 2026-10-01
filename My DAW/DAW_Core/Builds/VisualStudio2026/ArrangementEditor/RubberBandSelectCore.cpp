// ===========================================================================
// RubberBandSelectCore.cpp
// Bubblegum selection box — clean, COMPLETE APEX-flow frame.
// Transparent interior, solid two-tone pink outline with a soft glow.
// Drawn as one continuous rounded-rectangle frame (no dripping/segmented
// outline) so the marquee always reads as a single solid selection box —
// never "line space line space" segments.
// Selection behaviour is identical to a normal marquee.
// ===========================================================================
#include "RubberBandSelectCore.h"
#include "../Source/ThemeCore/Theme.h"

namespace ArrangementEditor
{
    RubberBandSelectCore::RubberBandSelectCore()
    {
        setSize(100, 100);
        setVisible(false);
        setInterceptsMouseClicks(false, false);
    }

    void RubberBandSelectCore::startDrag(juce::Point<int> startPos)
    {
        m_startPos = startPos;
        m_currentPos = startPos;
        m_active = true;
        setBounds(getSelectionBounds());
        setVisible(true);
    }

    void RubberBandSelectCore::updateDrag(juce::Point<int> currentPos)
    {
        const auto oldBounds = getBounds();
        m_currentPos = currentPos;
        const auto newBounds = getSelectionBounds();
        if (newBounds == oldBounds)
            return;

        setBounds(newBounds);
        if (auto* parent = getParentComponent())
            parent->repaint(oldBounds.getUnion(newBounds).expanded(4));
        else
            repaint();

        if (onSelectionChanged)
            onSelectionChanged(newBounds);
    }

    void RubberBandSelectCore::endDrag()
    {
        m_active = false;
        setVisible(false);

        if (onSelectionComplete)
            onSelectionComplete(getSelectionBounds());
    }

    juce::Rectangle<int> RubberBandSelectCore::getSelectionBounds() const
    {
        return juce::Rectangle<int>(m_startPos, m_currentPos).getSmallestIntegerContainer();
    }

    void RubberBandSelectCore::paint(juce::Graphics& g)
    {
        if (!m_active)
            return;

        auto& a = DAW::Theme::getInstance().apex;
        const auto b = getLocalBounds().toFloat();
        if (b.isEmpty())
            return;

        constexpr float kCorner = 4.f;

        // Soft pink glow halo — drawn outside the frame so the outline itself
        // stays one complete, unbroken shape.
        for (int i = 3; i >= 1; --i)
        {
            g.setColour(a.color.pink.withAlpha(0.05f + 0.05f * (float)(3 - i)));
            g.drawRoundedRectangle(b.expanded((float)i), kCorner + (float)i,
                                   1.5f + (float)(3 - i) * 2.0f);
        }

        // Complete two-tone frame — light bubblegum core, hot pink edge.
        g.setColour(a.color.pink.withAlpha(0.98f));
        g.drawRoundedRectangle(b, kCorner, 2.6f);
        g.setColour(a.color.magentaBright.withAlpha(1.0f));
        g.drawRoundedRectangle(b, kCorner, 1.5f);

        // Crisp white inner hairline — clean, like a native selection box.
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawRoundedRectangle(b.reduced(1.0f), juce::jmax(0.f, kCorner - 1.f), 0.6f);
    }

} // namespace ArrangementEditor
