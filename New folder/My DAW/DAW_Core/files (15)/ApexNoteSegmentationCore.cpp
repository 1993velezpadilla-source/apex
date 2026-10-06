// =============================================================================
//  ApexNoteSegmentationCore.cpp
//  Frames -> notes. See header for algorithm.
//
//  Drop-in: Source/VocalTuneCore/ApexNoteSegmentationCore.cpp
// =============================================================================

#include "ApexNoteSegmentationCore.h"
#include <algorithm>

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
float ApexNoteSegmentationCore::medianOf (std::vector<float>& scratch)
{
    if (scratch.empty()) return 0.0f;
    const size_t mid = scratch.size() / 2;
    std::nth_element (scratch.begin(), scratch.begin() + (std::ptrdiff_t) mid, scratch.end());
    return scratch[mid];
}

// -----------------------------------------------------------------------------
float ApexNoteSegmentationCore::weightedMedianMidi (const std::vector<float>& midis,
                                                    const std::vector<float>& weights)
{
    if (midis.empty()) return 0.0f;

    // Build (value, weight) pairs and sort by value.
    std::vector<std::pair<float, float>> pairs;
    pairs.reserve (midis.size());

    float total = 0.0f;
    for (size_t i = 0; i < midis.size(); ++i)
    {
        const float w = juce::jmax (1.0e-6f, weights[i]);
        pairs.push_back ({ midis[i], w });
        total += w;
    }

    std::sort (pairs.begin(), pairs.end(),
               [] (const auto& a, const auto& b) { return a.first < b.first; });

    const float halfTotal = 0.5f * total;
    float running = 0.0f;
    for (const auto& p : pairs)
    {
        running += p.second;
        if (running >= halfTotal) return p.first;
    }
    return pairs.back().first;
}

// -----------------------------------------------------------------------------
std::vector<ApexTuneNote> ApexNoteSegmentationCore::segment (const ApexTuneAnalysis& a,
                                                             const Params& p)
{
    std::vector<ApexTuneNote> notes;
    if (a.frames.empty() || a.hopSamples <= 0 || a.sampleRate <= 0.0)
        return notes;

    const int N = (int) a.frames.size();

    // -------------------------------------------------------------------------
    // 1. Build per-frame MIDI (NaN for unvoiced) and median-smooth.
    // -------------------------------------------------------------------------
    std::vector<float> rawMidi   ((size_t) N, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> smoothMidi((size_t) N, std::numeric_limits<float>::quiet_NaN());

    for (int i = 0; i < N; ++i)
    {
        if (a.frames[(size_t) i].voiced && a.frames[(size_t) i].f0Hz > 0.0f)
            rawMidi[(size_t) i] = hzToMidi (a.frames[(size_t) i].f0Hz);
    }

    const int half = juce::jmax (0, p.medianFilterFrames / 2);
    std::vector<float> window;
    window.reserve ((size_t) p.medianFilterFrames);

    for (int i = 0; i < N; ++i)
    {
        if (std::isnan (rawMidi[(size_t) i])) continue;

        window.clear();
        for (int j = juce::jmax (0, i - half); j <= juce::jmin (N - 1, i + half); ++j)
        {
            if (! std::isnan (rawMidi[(size_t) j])) window.push_back (rawMidi[(size_t) j]);
        }
        if (! window.empty()) smoothMidi[(size_t) i] = medianOf (window);
    }

    // -------------------------------------------------------------------------
    // 2. Walk frames with hysteresis; emit candidate segments.
    // -------------------------------------------------------------------------
    struct Segment { int firstFrame; int lastFrame; };
    std::vector<Segment> segments;

    int   curStart       = -1;     // frame index of current segment start
    int   curRoundedMidi = 0;      // semitone bucket of current segment
    int   unvoicedRun    = 0;      // consecutive unvoiced frames inside a segment

    auto closeSegment = [&] (int endFrame)
    {
        if (curStart >= 0 && endFrame >= curStart)
            segments.push_back ({ curStart, endFrame });
        curStart = -1;
        unvoicedRun = 0;
    };

    for (int i = 0; i < N; ++i)
    {
        const bool voiced = ! std::isnan (smoothMidi[(size_t) i]);

        if (! voiced)
        {
            if (curStart >= 0)
            {
                ++unvoicedRun;
                if (unvoicedRun > p.graceFramesUnvoiced)
                    closeSegment (i - unvoicedRun);   // close at last voiced frame
            }
            continue;
        }

        // Voiced frame.
        unvoicedRun = 0;
        const int rounded = (int) std::lround (smoothMidi[(size_t) i]);

        if (curStart < 0)
        {
            curStart       = i;
            curRoundedMidi = rounded;
        }
        else
        {
            const float jump = std::abs (smoothMidi[(size_t) i] - (float) curRoundedMidi);
            if (jump > p.semitoneTolerance)
            {
                closeSegment (i - 1);
                curStart       = i;
                curRoundedMidi = rounded;
            }
        }
    }
    closeSegment (N - 1);

    // -------------------------------------------------------------------------
    // 3. Convert segments to ApexTuneNote, drop under-min-duration ones.
    // -------------------------------------------------------------------------
    const double framesPerSec = a.sampleRate / (double) a.hopSamples;
    const float  minFramesF   = (p.minDurationMs * 0.001f) * (float) framesPerSec;
    const int    minFrames    = juce::jmax (1, (int) std::ceil (minFramesF));

    int noteIndex = 0;
    for (const auto& s : segments)
    {
        const int len = s.lastFrame - s.firstFrame + 1;
        if (len < minFrames) continue;

        // Gather frame MIDIs + confidence weights for this segment.
        std::vector<float> midis, weights;
        midis.reserve   ((size_t) len);
        weights.reserve ((size_t) len);

        for (int i = s.firstFrame; i <= s.lastFrame; ++i)
        {
            if (! std::isnan (smoothMidi[(size_t) i]))
            {
                midis.push_back   (smoothMidi[(size_t) i]);
                weights.push_back (a.frames[(size_t) i].confidence);
            }
        }
        if (midis.empty()) continue;

        ApexTuneNote n;
        n.noteId       = "n_" + juce::String (noteIndex++);
        n.startSample  = (int64_t) s.firstFrame * (int64_t) a.hopSamples;
        n.endSample    = (int64_t) (s.lastFrame + 1) * (int64_t) a.hopSamples;
        n.detectedMidi = weightedMedianMidi (midis, weights);
        n.targetMidi   = n.detectedMidi;            // no correction yet
        n.centsOffset  = 0.0f;
        n.voiced       = true;

        notes.push_back (n);
    }

    return notes;
}

}} // namespace apex::vocaltune
