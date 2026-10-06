#pragma once
#include "BubblegumCableTypes.h"

namespace bubblegum
{
    class BubblegumCableVisibilityCore
    {
    public:
        struct Inputs
        {
            bool bubblegumPanelOpen = false;
            bool cableForceVisible = false;
        };

        bool cablesVisible(const Inputs& in) const noexcept;
        bool shouldRenderSnapshot(const CableWorldSnapshot& s, const Inputs& in) const noexcept;
    };
}
