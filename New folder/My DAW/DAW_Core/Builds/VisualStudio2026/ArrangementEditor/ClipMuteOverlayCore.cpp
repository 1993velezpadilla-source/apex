// ===========================================================================
// ClipMuteOverlayCore.cpp
// ===========================================================================
#include "ClipMuteOverlayCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t muteGrey = 0x99666666;
    }

    ClipMuteOverlayCore::ClipMuteOverlayCore()
    {
        setSize(100, 80);
        setInterceptsMouseClicks(false, false);
    }

    void ClipMuteOverlayCore::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(fromU32(Col::muteGrey));
        g.fillRoundedRectangle(b, 3.f);
    }

} // namespace ArrangementEditor
