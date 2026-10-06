// ===========================================================================
// RubberBandSelectCore.cpp
// ===========================================================================
#include "RubberBandSelectCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t accent = 0xFFE07B39;
    }

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
        m_currentPos = currentPos;
        setBounds(getSelectionBounds());
        repaint();
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

        const auto b = getLocalBounds().toFloat().reduced(0.5f);
        constexpr float r = 3.5f;

        // ── Subtle fill ───────────────────────────────────────────────────
        // Very faint pink tint — enough to see the selected area, not to
        // block clip visuals underneath.
        g.setColour(juce::Colour(0xFFFFB3C6).withAlpha(0.055f));
        g.fillRoundedRectangle(b, r);

        // ── Outer glow halo ───────────────────────────────────────────────
        // Soft pink outer glow — two expanding passes, no fill.
        g.setColour(juce::Colour(0xFFFF85A1).withAlpha(0.10f));
        g.drawRoundedRectangle(b.expanded(2.5f), r + 2.5f, 1.0f);
        g.setColour(juce::Colour(0xFFFF85A1).withAlpha(0.06f));
        g.drawRoundedRectangle(b.expanded(4.5f), r + 4.5f, 1.0f);

        // ── Outer border — thin soft pink ────────────────────────────────
        g.setColour(juce::Colour(0xFFFF85A1).withAlpha(0.72f));
        g.drawRoundedRectangle(b, r, 0.75f);

        // ── Inner border — near-white hairline ───────────────────────────
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawRoundedRectangle(b.reduced(2.0f), juce::jmax(1.0f, r - 2.0f), 0.5f);

        // ── Corner accent marks ───────────────────────────────────────────
        // Small bright L-shaped ticks at each corner for a precision/luxury feel.
        const float cx = b.getX(), cy = b.getY();
        const float cr = b.getRight(), cb2 = b.getBottom();
        constexpr float kLen = 6.0f;
        const auto cornerCol = juce::Colour(0xFFFFB3C6).withAlpha(0.90f);
        g.setColour(cornerCol);
        // top-left
        g.drawLine(cx,       cy,       cx + kLen, cy,       1.0f);
        g.drawLine(cx,       cy,       cx,        cy + kLen, 1.0f);
        // top-right
        g.drawLine(cr,       cy,       cr - kLen, cy,       1.0f);
        g.drawLine(cr,       cy,       cr,        cy + kLen, 1.0f);
        // bottom-left
        g.drawLine(cx,       cb2,      cx + kLen, cb2,      1.0f);
        g.drawLine(cx,       cb2,      cx,        cb2 - kLen, 1.0f);
        // bottom-right
        g.drawLine(cr,       cb2,      cr - kLen, cb2,      1.0f);
        g.drawLine(cr,       cb2,      cr,        cb2 - kLen, 1.0f);
    }

} // namespace ArrangementEditor
