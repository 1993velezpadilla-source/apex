#pragma once
#include "Clip.h"
#include "../StepSequencerCore/StepSequencerTypes.h"

namespace DAW {

class PatternClip : public Clip
{
public:
    PatternClip(const ClipID& id, const juce::String& name,
                StablePatternId patternId);

    StablePatternId getPatternId() const { return patternId_; }
    void setPatternId(StablePatternId id) { patternId_ = id; notifyPropertyChanged(); }

    bool isLinked() const { return isLinked_; }
    void setLinked(bool linked) { isLinked_ = linked; notifyPropertyChanged(); }

    float getGain() const { return gain_; }
    void setGain(float g) { gain_ = juce::jlimit(0.0f, 2.0f, g); notifyPropertyChanged(); }

    float getTranspose() const { return transpose_; }
    void setTranspose(float t) { transpose_ = t; notifyPropertyChanged(); }

    float getProbability() const { return probability_; }
    void setProbability(float p) { probability_ = juce::jlimit(0.0f, 1.0f, p); notifyPropertyChanged(); }

    juce::ValueTree getState() const override;
    void restoreState(const juce::ValueTree& state) override;

private:
    StablePatternId patternId_;
    bool isLinked_ = true;
    float gain_ = 1.0f;
    float transpose_ = 0.0f;
    float probability_ = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternClip)
};

} // namespace DAW
