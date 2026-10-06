#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include <vector>

namespace DAW {

class MixerStrip;

enum class DragMode
{
    None,
    Reorder,
    AdoptIntoFolder,
    CreateFolder
};

struct DragState
{
    DragMode mode = DragMode::None;
    TrackID draggedTrackId;
    TrackID targetTrackId;
    int insertionIndex = -1;
    juce::Point<int> cursorPos;
    bool active = false;
};

class MixerDragStateMachine
{
public:
    void begin(const TrackID& trackId, juce::Point<int> startPos);
    bool update(juce::Point<int> cursorPos, const std::vector<MixerStrip*>& strips);
    DragMode commit(juce::Point<int> cursorPos, const std::vector<MixerStrip*>& strips);
    void cancel();

    const DragState& getState() const noexcept { return state_; }

private:
    DragMode computeMode(juce::Point<int> cursorPos, const std::vector<MixerStrip*>& strips) const;
    int getStripCenterIdx(juce::Point<int> cursorPos, const std::vector<MixerStrip*>& strips) const;
    int computeInsertionIndex(juce::Point<int> cursorPos, const std::vector<MixerStrip*>& strips) const;

    DragState state_;
    juce::Point<int> startPos_;

    static constexpr int kDragThreshold = 8;
    static constexpr int kEdgeZonePx = 14;
};

} // namespace DAW
