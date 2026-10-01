#include "BubblegumCableVisibilityCore.h"

namespace bubblegum
{
    bool BubblegumCableVisibilityCore::cablesVisible(const Inputs& in) const noexcept
    {
        return in.bubblegumPanelOpen || in.cableForceVisible;
    }

    bool BubblegumCableVisibilityCore::shouldRenderSnapshot(const CableWorldSnapshot& s, const Inputs& in) const noexcept
    {
        if (!cablesVisible(in))
            return false;

        if (!s.visible)
            return false;

        if (s.state == SendVisualState::DoesNotExist)
            return false;

        return true;
    }
}
