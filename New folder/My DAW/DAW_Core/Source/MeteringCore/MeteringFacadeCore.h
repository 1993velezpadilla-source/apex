#pragma once
#include <JuceHeader.h>
#include "SamplePeakMeterCore.h"
#include "TruePeakMeterCore.h"
#include "RmsMeterCore.h"
#include "LufsMeterCore.h"

namespace DAW {

class MeteringFacadeCore
{
public:
    struct ChannelMetrics
    {
        float samplePeak = 0.0f;
        float samplePeakDb = -160.0f;
        float truePeak = 0.0f;
        float truePeakDb = -160.0f;
        float rms = 0.0f;
        float rmsDb = -160.0f;
        float momentaryLufs = -std::numeric_limits<float>::infinity();
        float shortTermLufs = -std::numeric_limits<float>::infinity();
        float integratedLufs = -std::numeric_limits<float>::infinity();
    };

    struct Metrics
    {
        ChannelMetrics left;
        ChannelMetrics right;
    };

    void prepare(double sampleRate, int blockSize)
    {
        for (auto& p : samplePeak_) p.prepare(sampleRate);
        for (auto& p : truePeak_) p.prepare(sampleRate, blockSize);
        for (auto& r : rms_) r.prepare(sampleRate);
        for (auto& l : lufs_) l.prepare(sampleRate);
    }

    void processBlock(const float* L, const float* R, int numSamples)
    {
        samplePeak_[0].process(L, numSamples);
        samplePeak_[1].process(R, numSamples);
        truePeak_[0].process(L, numSamples);
        truePeak_[1].process(R, numSamples);
        rms_[0].process(L, numSamples);
        rms_[1].process(R, numSamples);
        lufs_[0].process(L, numSamples);
        lufs_[1].process(R, numSamples);
    }

    void processBlock(const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (buffer.getNumChannels() <= 0) return;
        processBlock(buffer.getReadPointer(0),
                     buffer.getReadPointer(juce::jmin(1, buffer.getNumChannels() - 1)),
                     numSamples);
    }

    Metrics getMetrics() const noexcept
    {
        Metrics m;
        fillChannel(m.left, 0);
        fillChannel(m.right, 1);
        return m;
    }

    const Metrics& getPreFaderMetrics() const noexcept { return preFaderMetrics_; }
    const Metrics& getPostFaderMetrics() const noexcept { return postFaderMetrics_; }
    void setPreFaderMetrics(const Metrics& m) noexcept { preFaderMetrics_ = m; }
    void setPostFaderMetrics(const Metrics& m) noexcept { postFaderMetrics_ = m; }

private:
    void fillChannel(ChannelMetrics& m, int ch) const noexcept
    {
        m.samplePeak = samplePeak_[ch].getPeak();
        m.samplePeakDb = samplePeak_[ch].getPeakDb();
        m.truePeak = truePeak_[ch].getPeak();
        m.truePeakDb = truePeak_[ch].getPeakDb();
        m.rms = rms_[ch].getRms();
        m.rmsDb = rms_[ch].getRmsDb();
        m.momentaryLufs = lufs_[ch].getMomentaryLufs();
        m.shortTermLufs = lufs_[ch].getShortTermLufs();
        m.integratedLufs = lufs_[ch].getIntegratedLufs();
    }

    SamplePeakMeterCore samplePeak_[2];
    TruePeakMeterCore truePeak_[2];
    RmsMeterCore rms_[2];
    LufsMeterCore lufs_[2];
    Metrics preFaderMetrics_;
    Metrics postFaderMetrics_;
};

} // namespace DAW
