#pragma once

namespace DAW {

struct OfflineRenderBarrierCore
{
    static bool callbacksDrained (int activeCallbacks) noexcept
    {
        return activeCallbacks <= 0;
    }

    static bool canRender (bool offlineRequested, bool barrierReady) noexcept
    {
        return offlineRequested && barrierReady;
    }

    static bool shouldProcessRealtimePath (bool offlineRequested,
                                           bool projectStateMutation = false) noexcept
    {
        return ! offlineRequested && ! projectStateMutation;
    }
};

} // namespace DAW
