#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "../UtilityCore/Types.h"

namespace DAW {

class ClickPatternCore
{
public:
    struct BeatHit
    {
        int  sampleOffsetInBlock { -1 };
        bool isDownbeat          { false };
    };

    void setSampleRate(double sr) noexcept { sampleRate_ = juce::jmax(1.0, sr); }
    void setTempo(double bpm) noexcept     { tempoBpm_   = juce::jmax(20.0, bpm); }
    void setTimeSig(int beats, int unit) noexcept
    {
        beatsPerBar_ = juce::jmax(1, beats);
        beatUnit_    = juce::jmax(1, unit);
    }

    int findBeatsInBlock(int64_t blockStartSamples,
                         int numSamples,
                         BeatHit* outHits,
                         int maxHits) const noexcept
    {
        if (outHits == nullptr || maxHits <= 0 || numSamples <= 0) return 0;
        jassert(sampleRate_ > 0.0 && "sampleRate_ read before prepareToPlay");
        if (sampleRate_ <= 0.0) return 0;

        const double quarterNoteSamples = (60.0 / tempoBpm_) * sampleRate_;
        const double samplesPerBeat = quarterNoteSamples * (4.0 / (double) beatUnit_);
        if (samplesPerBeat <= 0.0) return 0;

        const double firstBeatNum = std::ceil((double) blockStartSamples / samplesPerBeat);
        int hitsFound = 0;

        for (int n = 0; n < maxHits + 4; ++n)
        {
            const double beatNum = firstBeatNum + (double) n;
            const int64_t beatSample = (int64_t) std::llround(beatNum * samplesPerBeat);
            const int64_t offsetInBlock = beatSample - blockStartSamples;

            if (offsetInBlock < 0) continue;
            if (offsetInBlock >= numSamples) break;
            if (hitsFound >= maxHits) break;

            outHits[hitsFound].sampleOffsetInBlock = (int) offsetInBlock;
            outHits[hitsFound].isDownbeat = (((int64_t) std::llround(beatNum)) % beatsPerBar_) == 0;
            ++hitsFound;
        }

        return hitsFound;
    }

    int getBeatsPerBar() const noexcept { return beatsPerBar_; }
    int getBeatUnit() const noexcept { return beatUnit_; }
    double getTempoBpm() const noexcept { return tempoBpm_; }

private:
    double sampleRate_  { 0.0 };
    double tempoBpm_    { 120.0 };
    int    beatsPerBar_ { 4 };
    int    beatUnit_    { 4 };
};

} // namespace DAW
