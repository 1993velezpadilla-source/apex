// ===========================================================================
// ClipEraseOverlayCore.cpp
// ===========================================================================
#include "ClipEraseOverlayCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t eraseRed = 0xFFCC4444;
    }

    ClipEraseOverlayCore::ClipEraseOverlayCore()
    {
        setSize(100, 80);
        setInterceptsMouseClicks(false, false);
    }

    void ClipEraseOverlayCore::showOverlay(bool show)
    {
        m_visible = show;
        if (show)
            startTimerHz(60);
        else
            stopTimer();
    }

    void ClipEraseOverlayCore::timerCallback()
    {
        if (m_visible)
        {
            m_alpha = juce::jmin(0.5f, m_alpha + 0.05f);
            if (m_alpha >= 0.5f)
                stopTimer();
        }
        else
        {
            m_alpha = juce::jmax(0.f, m_alpha - 0.1f);
            if (m_alpha <= 0.f)
                stopTimer();
        }
        repaint();
    }

    void ClipEraseOverlayCore::paint(juce::Graphics& g)
    {
        if (m_alpha < 0.01f)
            return;

        auto b = getLocalBounds().toFloat();
        g.setColour(fromU32(Col::eraseRed).withAlpha(m_alpha));
        g.fillRoundedRectangle(b, 3.f);
    }

} // namespace ArrangementEditor
