#pragma once
#include <JuceHeader.h>
#include <vector>

namespace DAW
{
// Computes clip-only references into ONE immutable recording WAV. Every pass
// shares the original file: no rendering, deleting or overwriting media.
// Deliberately refuses ambiguous pre-roll / punch positions rather than
// silently slicing at the wrong sample boundary.
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
            || recordTimelineStart != loopStart || capturedSamples <= 0)
            return passes;
        const int64_t loopLength = loopEnd - loopStart;
        // A solitary pass should behave exactly like a standard recording.
        if (capturedSamples <= loopLength)
            return passes;
        const int64_t count = (capturedSamples / loopLength)
            + ((capturedSamples % loopLength) != 0 ? 1 : 0);
        // A corrupt clock or nonsensical loop range must not allocate
        // unbounded playlist state during project finalization.
        if (count > 1024)
            return passes;
        passes.reserve((size_t)count);
        for (int64_t offset = 0; offset < capturedSamples; offset += loopLength)
        {
            passes.push_back({ offset, juce::jmin(loopLength, capturedSamples - offset),
                               loopStart });
        }
        return passes;
    }
};
} // namespace DAW
