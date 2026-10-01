// =============================================================================
//  ApexSibilantDetectorCore.cpp
//  See header. Finds unvoiced+high-energy runs and flags overlapping notes.
//
//  Drop-in: Source/VocalTuneCore/ApexSibilantDetectorCore.cpp
// =============================================================================

#include "ApexSibilantDetectorCore.h"
#include <algorithm>
#include <cmath>

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
std::vector<ApexSibilantRegion>
ApexSibilantDetectorCore::detect (const ApexTuneAnalysis& a, const Params& p)
{
    std::vector<ApexSibilantRegion> regions;
    if (a.frames.empty() || a.hopSamples <= 0 || a.sampleRate <= 0.0)
        return regions;

    const int   N             = (int) a.frames.size();
    const double framesPerSec = a.sampleRate / (double) a.hopSamples;
    const float  minFramesF   = (p.minDurationMs * 0.001f) * (float) framesPerSec;
    const int    minFrames    = juce::jmax (1, (int) std::ceil (minFramesF));

    int   runStart    = -1;
    int   voicedRun   = 0;
    float peakEnergy  = -100.0f;

    auto closeRun = [&] (int endFrame)
    {
        if (runStart >= 0 && endFrame >= runStart
            && (endFrame - runStart + 1) >= minFrames)
        {
            ApexSibilantRegion r;
            r.startSample = (int64_t) runStart * (int64_t) a.hopSamples;
            r.endSample   = (int64_t) (endFrame + 1) * (int64_t) a.hopSamples;
            r.energyDb    = peakEnergy;
            regions.push_back (r);
        }
        runStart   = -1;
        voicedRun  = 0;
        peakEnergy = -100.0f;
    };

    for (int i = 0; i < N; ++i)
    {
        const auto& f = a.frames[(size_t) i];
        const bool  hot = (f.energyDb >= p.minEnergyDb);

        if (! f.voiced && hot)
        {
            // Sibilant-y frame.
            if (runStart < 0) runStart = i;
            voicedRun  = 0;
            peakEnergy = juce::jmax (peakEnergy, f.energyDb);
        }
        else if (runStart >= 0)
        {
            // Either voiced, or below energy gate.
            if (f.voiced && hot)
            {
                ++voicedRun;
                if (voicedRun > p.graceFrames)
                    closeRun (i - voicedRun);
            }
            else if (! hot)
            {
                // Dropped below gate -- close immediately.
                closeRun (i - 1);
            }
            else
            {
                voicedRun = 0;
            }
        }
    }
    closeRun (N - 1);

    return regions;
}

// -----------------------------------------------------------------------------
void ApexSibilantDetectorCore::markOverlappingNotes
    (std::vector<ApexTuneNote>& notes,
     const std::vector<ApexSibilantRegion>& regions)
{
    for (auto& n : notes)
    {
        for (const auto& r : regions)
        {
            // Half-open overlap test.
            const bool overlaps = (n.startSample < r.endSample)
                               && (r.startSample < n.endSample);
            if (overlaps) { n.sibilantProtected = true; break; }
        }
    }
}

}} // namespace apex::vocaltune
