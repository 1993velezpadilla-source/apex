// =============================================================================
//  ApexTuneCacheCore.h
//  Disk-backed cache for YIN analysis results and rendered audio.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneCacheCore.h
//  Depends on: ApexTuneTypes.h, JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Threading: file I/O is thread-safe at the OS level. Multiple worker
//             threads can write different cache files concurrently. We
//             don't currently protect against two workers writing the SAME
//             cache file simultaneously -- in V1, the caller is responsible
//             for serializing analyses/renders per clip.
//
//  Layout on disk:
//    <userAppData>/APEX/VocalTuneCache/
//        <fingerprint>.apxan       <- analysis cache (binary)
//        <fingerprint>_r<ver>.wav  <- render cache (wav)
//
//    fingerprint = SHA-256 of (source filename + size + mtime + analysis params)
//    ver         = clipState.renderVersion at time of render
//
//  Invalidation:
//    - Analysis cache becomes stale when source file changes (mtime/size
//      differ) or when analysis params change. Both are folded into the
//      fingerprint, so stale caches simply don't get hit -- they linger on
//      disk until manually cleared (V1 trade-off; V2 should add LRU eviction).
//
//    - Render cache becomes stale when clipState.renderVersion is bumped
//      (any note edit). New renders write a new <fingerprint>_r<N+1>.wav;
//      the caller can sweep older renderVersions via clearStaleRenders().
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"
#include "ApexPitchDetectionCore.h"   // for Params used in fingerprinting

namespace apex { namespace vocaltune {

class ApexTuneCacheCore
{
public:
    // Cache root: <userAppData>/APEX/VocalTuneCache/. Created on first call.
    static juce::File getCacheRoot();

    // Fingerprint string for a source file + analysis params combo.
    // Stable across runs as long as inputs are identical.
    static juce::String fingerprintSource
        (const juce::File& sourceFile,
         const ApexPitchDetectionCore::Params& params);

    // Resolves cache file paths for a clip + (optional) version override.
    static juce::File analysisCacheFile (const juce::String& fingerprint);
    static juce::File renderCacheFile   (const juce::String& fingerprint, int renderVersion);

    // ---- Analysis cache --------------------------------------------------
    static bool saveAnalysis (const juce::File& dest, const ApexTuneAnalysis& a);
    static bool loadAnalysis (const juce::File& src,  ApexTuneAnalysis& outA);

    // ---- Render cache ----------------------------------------------------
    // Writes mono audio at the given sample rate as a 32-bit float WAV.
    static bool saveRenderWav (const juce::File& dest,
                               const float*      mono,
                               int               numSamples,
                               double            sampleRate);

    // Reads back a mono WAV (any bit depth) into outMono.
    static bool loadRenderWav (const juce::File& src,
                               std::vector<float>& outMono,
                               double& outSampleRate);

    // ---- Maintenance -----------------------------------------------------
    // Deletes <fingerprint>_r*.wav files for renderVersions != keepVersion.
    // Returns the number of files deleted.
    static int clearStaleRenders (const juce::String& fingerprint, int keepVersion);
};

}} // namespace apex::vocaltune
