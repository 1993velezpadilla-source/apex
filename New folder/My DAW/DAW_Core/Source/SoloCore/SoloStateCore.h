#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * SoloStateCore — tracks the global solo state across all tracks.
 *
 * Maintains a count of soloed tracks for O(1) "any solo active" queries.
 * Used by the audio engine to quickly determine solo routing behavior.
 */
class SoloStateCore
{
public:
    void trackSoloed() noexcept { soloCount_++; }
    void trackUnsoloed() noexcept { if (soloCount_ > 0) soloCount_--; }

    bool isAnySoloed() const noexcept { return soloCount_ > 0; }
    int getSoloCount() const noexcept { return soloCount_; }

    void reset() noexcept { soloCount_ = 0; }

private:
    int soloCount_ = 0;
};

} // namespace DAW
