#pragma once
#include <JuceHeader.h>
#include "PatternManagerTypes.h"
#include "../StepSequencerCore/StepSequencerModel.h"

namespace DAW {

class PatternManagerCore : public juce::ChangeBroadcaster {
public:
    PatternManagerCore(StepSequencerModel& model);
    ~PatternManagerCore() override;

    int  createPattern(const juce::String& name = "");
    void deletePattern(int index);
    void renamePattern(int index, const juce::String& newName);
    void clonePattern(int index);
    void setCurrentPattern(int index);
    int  getCurrentPattern() const;
    int  getPatternCount() const;
    juce::Array<PatternInfo> getPatternList() const;

private:
    StepSequencerModel& model_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternManagerCore)
};

} // namespace DAW
