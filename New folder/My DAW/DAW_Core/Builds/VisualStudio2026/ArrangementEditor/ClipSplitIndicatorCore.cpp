// ===========================================================================
// ClipSplitIndicatorCore.cpp
// ===========================================================================
#include "ClipSplitIndicatorCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t accent = 0xFFE07B39;
    }

    ClipSplitIndicatorCore::ClipSplitIndicatorCore()
    {
        setSize(2, 80);
        setVisible(false);
        setInterceptsMouseClicks(false, false);
    }

    void ClipSplitIndicatorCore::setPosition(double timePos)
    {
        m_timePos = timePos;
        repaint();
    }

    void ClipSplitIndicatorCore::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(fromU32(Col::accent));
        g.fillRect(b);

        // Top/bottom markers
        g.fillRect(b.getX() - 2.f, b.getY(), 6.f, 3.f);
        g.fillRect(b.getX() - 2.f, b.getBottom() - 3.f, 6.f, 3.f);
    }

} // namespace ArrangementEditor
