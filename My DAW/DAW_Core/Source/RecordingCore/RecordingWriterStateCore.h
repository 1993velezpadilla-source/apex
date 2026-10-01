#pragma once
#include "LiveRecordWaveformCore.h"
#include <atomic>

namespace DAW {

struct RecordingWriterStateCore
{
    static void publishWriteResult (bool accepted,
                                    LiveRecordWaveformCore& waveform,
                                    std::atomic<bool>& trackWriterFailed,
                                    std::atomic<bool>& stopRequested) noexcept
    {
        if (accepted)
            return;

        waveform.setInputAvailable (false);
        waveform.setTakeActive (false);
        trackWriterFailed.store (true, std::memory_order_release);
        stopRequested.store (true, std::memory_order_release);
    }

    static bool consumeStopRequest (std::atomic<bool>& stopRequested) noexcept
    {
        return stopRequested.exchange (false, std::memory_order_acq_rel);
    }
};

} // namespace DAW
