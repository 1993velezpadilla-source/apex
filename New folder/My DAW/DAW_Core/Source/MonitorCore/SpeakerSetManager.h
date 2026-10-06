#pragma once
#include <JuceHeader.h>
#include <array>

namespace DAW {

/** Describes one physical speaker output destination. */
struct SpeakerSetConfig
{
    juce::String name          = "Main";
    int          outputChannelL = 0;
    int          outputChannelR = 1;
    float        trimDb         = 0.0f;
    bool         enabled        = true;
};

/**
 * SpeakerSetManager — manages available monitor speaker configurations.
 *
 * Up to 3 speaker sets (A / B / C) in v1, each mapping to a hardware
 * output pair with per-set trim offsets for calibration.
 *
 * Future: per-set monitor FX chains, sub feed, headphone set.
 */
class SpeakerSetManager
{
public:
    static constexpr int kMaxSets = 3;

    SpeakerSetManager()
    {
        sets_[0] = { "A", 0, 1, 0.0f, true  };
        sets_[1] = { "B", 0, 1, 0.0f, false };
        sets_[2] = { "C", 0, 1, 0.0f, false };
    }

    const SpeakerSetConfig& getSet(int idx) const noexcept
    {
        return sets_[(size_t)juce::jlimit(0, kMaxSets - 1, idx)];
    }

    SpeakerSetConfig& getSet(int idx) noexcept
    {
        return sets_[(size_t)juce::jlimit(0, kMaxSets - 1, idx)];
    }

    void setEnabled(int idx, bool on) noexcept
    {
        if (idx >= 0 && idx < kMaxSets) sets_[(size_t)idx].enabled = on;
    }

    bool isEnabled(int idx) const noexcept
    {
        return idx >= 0 && idx < kMaxSets && sets_[(size_t)idx].enabled;
    }

    float getTrimGainLinear(int activeSet) const noexcept
    {
        const auto& s = sets_[(size_t)juce::jlimit(0, kMaxSets - 1, activeSet)];
        return std::pow(10.0f, s.trimDb / 20.0f);
    }

private:
    std::array<SpeakerSetConfig, kMaxSets> sets_;
};

} // namespace DAW
