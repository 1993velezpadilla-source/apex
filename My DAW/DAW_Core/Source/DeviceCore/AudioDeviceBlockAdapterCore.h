#pragma once

namespace DAW {

/** Defines how one backend callback is presented to a render graph prepared
    for a maximum number of samples per process call. */
class AudioDeviceBlockAdapterCore
{
public:
    template <typename ProcessChunk>
    static void forEachPreparedChunk(int totalSamples,
                                     int maximumPreparedSamples,
                                     ProcessChunk&& processChunk)
    {
        if (totalSamples <= 0 || maximumPreparedSamples <= 0)
            return;

        // A backend may occasionally deliver more frames than its nominal
        // block size. Never forward more than the maximum used to prepare the
        // graph/plugins. Sub-blocking is bounded, allocation-free control-plane
        // adaptation at the device boundary.
        for (int offset = 0; offset < totalSamples;)
        {
            const int remaining = totalSamples - offset;
            const int chunkSamples = remaining < maximumPreparedSamples
                ? remaining : maximumPreparedSamples;
            processChunk(offset, chunkSamples);
            offset += chunkSamples;
        }
    }
};

} // namespace DAW
