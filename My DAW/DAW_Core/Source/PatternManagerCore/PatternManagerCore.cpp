#include "PatternManagerCore.h"

namespace DAW {

PatternManagerCore::PatternManagerCore(StepSequencerModel& model)
    : model_(model) {}

PatternManagerCore::~PatternManagerCore() = default;

int PatternManagerCore::createPattern(const juce::String& name) {
    int idx = model_.addPattern(name);
    sendChangeMessage();
    return idx;
}

void PatternManagerCore::deletePattern(int index) {
    model_.removePattern(index);
    sendChangeMessage();
}

void PatternManagerCore::renamePattern(int index, const juce::String& newName) {
    model_.getPattern(index).name = newName;
    sendChangeMessage();
}

void PatternManagerCore::clonePattern(int index) {
    model_.clonePattern(index);
    sendChangeMessage();
}

void PatternManagerCore::setCurrentPattern(int index) {
    model_.setCurrentPattern(index);
    sendChangeMessage();
}

int PatternManagerCore::getCurrentPattern() const { return model_.getCurrentPatternIndex(); }
int PatternManagerCore::getPatternCount() const { return model_.getPatternCount(); }

juce::Array<PatternInfo> PatternManagerCore::getPatternList() const {
    juce::Array<PatternInfo> list;
    for (int i = 0; i < model_.getPatternCount(); ++i) {
        const auto& p = model_.getPattern(i);
        list.add({ p.name, i, p.lanes.size(), p.getTotalSteps() });
    }
    return list;
}

} // namespace DAW
