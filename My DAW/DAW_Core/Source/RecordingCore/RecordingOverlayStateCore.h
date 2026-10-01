#pragma once

namespace DAW {

struct RecordingOverlayStateCore
{
    static bool shouldShowOverlay (bool takeActive, bool trackArmed) noexcept
    {
        (void) trackArmed;
        return takeActive;
    }
};

} // namespace DAW
