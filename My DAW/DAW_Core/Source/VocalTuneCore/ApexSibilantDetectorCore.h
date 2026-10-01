// =============================================================================
//  ApexSibilantDetectorCore.h
//  Detects unvoiced-but-energetic regions (sibilants, breaths) so the
//  render core can pass them through dry instead of pitching them.
//
//  Drop-in: Source/VocalTuneCore/ApexSibilantDetectorCore.h
//  Depends on: ApexTuneTypes.h, JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Threading: pure offline. Called once per analysis pass.
//
//  Strategy:
//   Sibilants ('s', 'sh', 't', 'k', etc.) are high-energy but YIN marks them
//   unvoiced (no periodic structure). We find runs of unvoiced frames with
//   energy above a gate and length above a minimum -- those are sibilants
//   or hard consonants. Quiet unvoiced runs (between phrases) are silences
//   and we don't care about them.
//
//   We emit ApexSibilantRegion ranges that the render core will use to
//   blend rendered audio with dry source audio (raised-cosine crossfades).
//   We also mark any *voiced* notes that fall partially inside a sibilant
//   region with sibilantProtected = true so they pass through too.
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"

namespace apex { namespace vocaltune {

struct ApexSibilantRegion
{
    int64_t startSample = 0;
    int64_t endSample   = 0;
    float   energyDb    = -100.0f;   // peak frame energy in the region
};

class ApexSibilantDetectorCore
{
public:
    struct Params
    {
        float minEnergyDb   = -45.0f;  // gate above which unvoiced = sibilant
        float minDurationMs = 20.0f;   // shorter = ignored as click/noise
        int   graceFrames   = 2;       // brief voiced frames don't break a sibilant run
    };

    // Detect sibilant/breath regions from the analysis.
    static std::vector<ApexSibilantRegion> detect (const ApexTuneAnalysis& analysis,
                                                   const Params& params = {});

    // Mark voiced notes that overlap any sibilant region.
    // After this call, those notes have sibilantProtected = true and the
    // render core will pass them through dry.
    static void markOverlappingNotes (std::vector<ApexTuneNote>& notes,
                                      const std::vector<ApexSibilantRegion>& regions);
};

}} // namespace apex::vocaltune
