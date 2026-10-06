#include "MixerDragStateMachine.h"
#include "MixerPanel.h"

namespace DAW {

void MixerDragStateMachine::begin(const TrackID& trackId, juce::Point<int> startPos)
{
    state_ = DragState{};
    state_.draggedTrackId = trackId;
    state_.active = false;
    startPos_ = startPos;
}

bool MixerDragStateMachine::update(juce::Point<int> cursorPos,
                                   const std::vector<MixerStrip*>& strips)
{
    state_.cursorPos = cursorPos;

    if (!state_.active)
    {
        auto delta = cursorPos - startPos_;
        if (std::abs(delta.x) < kDragThreshold && std::abs(delta.y) < kDragThreshold)
            return false;
        state_.active = true;
    }

    state_.mode           = computeMode(cursorPos, strips);
    state_.insertionIndex = computeInsertionIndex(cursorPos, strips);

    int targetIdx = getStripCenterIdx(cursorPos, strips);
    if (targetIdx >= 0 && targetIdx < (int)strips.size())
        state_.targetTrackId = strips[targetIdx]->getTrack().getID();
    else
        state_.targetTrackId = {};

    return true;
}

DragMode MixerDragStateMachine::commit(juce::Point<int> cursorPos,
                                       const std::vector<MixerStrip*>& strips)
{
    update(cursorPos, strips);
    DragMode result = state_.active ? state_.mode : DragMode::None;
    cancel();
    return result;
}

void MixerDragStateMachine::cancel()
{
    state_ = DragState{};
    startPos_ = {};
}

DragMode MixerDragStateMachine::computeMode(juce::Point<int> cursorPos,
                                            const std::vector<MixerStrip*>& strips) const
{
    int idx = getStripCenterIdx(cursorPos, strips);
    if (idx < 0 || idx >= (int)strips.size())
        return DragMode::Reorder;

    auto* strip = strips[idx];
    if (strip->getTrack().getID() == state_.draggedTrackId)
        return DragMode::Reorder;

    // Master bus never participates in folder stacks (Logic/Reaper convention)
    if (strip->getTrack().isMaster())
        return DragMode::Reorder;

    auto bounds = strip->getBounds();
    int relX = cursorPos.x - bounds.getX();

    if (relX < kEdgeZonePx || relX > bounds.getWidth() - kEdgeZonePx)
        return DragMode::Reorder;

    if (strip->getTrack().getRole() == TrackRole::FolderBus)
        return DragMode::AdoptIntoFolder;

    return DragMode::CreateFolder;
}

int MixerDragStateMachine::getStripCenterIdx(juce::Point<int> cursorPos,
                                             const std::vector<MixerStrip*>& strips) const
{
    for (int i = 0; i < (int)strips.size(); ++i)
    {
        if (strips[i]->getBounds().contains(cursorPos))
            return i;
    }
    return -1;
}

int MixerDragStateMachine::computeInsertionIndex(juce::Point<int> cursorPos,
                                                 const std::vector<MixerStrip*>& strips) const
{
    for (int i = 0; i < (int)strips.size(); ++i)
    {
        auto b = strips[i]->getBounds();
        if (cursorPos.x <= b.getCentreX())
            return i;
    }
    return (int)strips.size();
}

} // namespace DAW
