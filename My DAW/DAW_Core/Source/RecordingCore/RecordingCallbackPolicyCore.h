#pragma once

namespace DAW {

struct RecordingCallbackPolicyCore
{
    static bool shouldFeedRecorder (bool recordingActive,
                                    bool offlineRendering,
                                    bool routingGraphAvailable) noexcept
    {
        (void) routingGraphAvailable;
        return recordingActive && ! offlineRendering;
    }
};

} // namespace DAW
