#pragma once
#include <JuceHeader.h>
#include <vector>

namespace DAW
{
// Computes clip-only references into ONE immutable recording WAV. Every pass
// shares the original file: no rendering, deleting or overwriting media.
// Accept starts anywhere INSIDE the loop, preserving the initial partial
// pass at its original timeline offset. Refuse outside-loop pre-roll/punch
// positions rather than silently mapping those samples to the wrong take.
struct LoopPlaylistTakePlanCore final
{
    struct Pass
    {
        int64_t sourceStart = 0;
        int64_t length = 0;
        int64_t timelineStart = 0;
    };

    static std::vector<Pass> make(int64_t recordTimelineStart,
                                  int64_t loopStart, int64_t loopEnd,
                                  int64_t capturedSamples, bool automatic) 
    {
        std::vector<Pass> passes;
        if (!automatic || loopStart < 0 || loopEnd <= loopStart
            || recordTimelineStart < loopStart
            || recordTimelineStart >= loopEnd || capturedSamples <= 0)
            return passes;
        const int64_t loopLength = loopEnd - loopStart;
        const int64_t firstPassLength = loopEnd - recordTimelineStart;
        // A solitary pass should behave exactly like a standard recording.
        // Once a wrap occurs, the first take can be shorter than loopLength.
        if (capturedSamples <= firstPassLength)
            return passes;
        const int64_t remaining = capturedSamples - firstPassLength;
        const int64_t additional = (remaining / loopLength)
            + ((remaining % loopLength) != 0 ? 1 : 0);
        // Limit clip and playlist allocation even on corrupted transport data.
        if (additional >= 1024)
            return passes;
        passes.reserve((size_t)(1 + additional));
        passes.push_back({ 0, firstPassLength, recordTimelineStart });
        for (int64_t offset = firstPassLength; offset < capturedSamples;)
        {
            const int64_t length = juce::jmin(loopLength, capturedSamples - offset);
            passes.push_back({ offset, length, loopStart });
            offset += length;
        }
        return passes;
    }
};
} // namespace DAW
