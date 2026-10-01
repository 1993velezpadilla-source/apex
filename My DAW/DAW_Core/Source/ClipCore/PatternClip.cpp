#include "PatternClip.h"

namespace DAW {

PatternClip::PatternClip(const ClipID& id, const juce::String& name,
                         StablePatternId patternId)
    : Clip(id, name, ClipType::Pattern)
    , patternId_(patternId)
{
}

juce::ValueTree PatternClip::getState() const
{
    auto tree = Clip::getState();

    tree.setProperty("patternId", patternId_.toString(), nullptr);
    tree.setProperty("isLinked", isLinked_, nullptr);
    tree.setProperty("gain", gain_, nullptr);
    tree.setProperty("transpose", transpose_, nullptr);
    tree.setProperty("probability", probability_, nullptr);

    return tree;
}

void PatternClip::restoreState(const juce::ValueTree& state)
{
    Clip::restoreState(state);

    const auto patternIdStr = state.getProperty("patternId", "").toString();
    if (patternIdStr.isNotEmpty())
        patternId_ = StablePatternId(juce::Uuid(patternIdStr));

    isLinked_ = state.getProperty("isLinked", true);
    gain_ = state.getProperty("gain", 1.0f);
    transpose_ = state.getProperty("transpose", 0.0f);
    probability_ = state.getProperty("probability", 1.0f);
}

} // namespace DAW
