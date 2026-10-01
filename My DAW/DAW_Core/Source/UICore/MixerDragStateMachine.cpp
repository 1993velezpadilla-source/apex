#include "MixerDragStateMachine.h"
#include "MixerPanel.h"
#include "../FolderBusCore/FolderBusCore.h"

namespace DAW {

void MixerDragStateMachine::begin(const TrackID& trackId, juce::Point<int> startPos)
{
    state_ = DragState{};
    state_.draggedTrackId = trackId;
    state_.active = false;
    startPos_ = startPos;
}

bool MixerDragStateMachine::update(juce::Point<int> cursorPos,
                                   const std::vector<MixerStrip*>& strips,
                                   const std::vector<TrackID>& projectOrder)
{
    state_.cursorPos = cursorPos;

    if (!state_.active)
    {
        auto delta = cursorPos - startPos_;
        if (std::abs(delta.x) < kDragThreshold && std::abs(delta.y) < kDragThreshold)
            return false;
        state_.active = true;
    }

    const auto intent     = resolveIntent(cursorPos, strips);
    state_.mode           = intent.mode;
    state_.targetTrackId  = intent.targetTrackId;
    state_.insertionIndex = computeInsertionIndex(cursorPos, strips, projectOrder);

    return true;
}

DragMode MixerDragStateMachine::commit(juce::Point<int> cursorPos,
                                       const std::vector<MixerStrip*>& strips,
                                       const std::vector<TrackID>& projectOrder)
{
    update(cursorPos, strips, projectOrder);
    DragMode result = state_.active ? state_.mode : DragMode::None;
    cancel();
    return result;
}

void MixerDragStateMachine::cancel()
{
    state_ = DragState{};
    startPos_ = {};
}

MixerDragStateMachine::ResolvedIntent MixerDragStateMachine::resolveIntent(
    juce::Point<int> cursorPos, const std::vector<MixerStrip*>& strips) const
{
    // ── Folder-bus drag & drop DISABLED (product decision) ────────────────
    // Every mixer drop resolves to a plain reorder: no folder icon appears
    // when dragging over another strip and no folder is created/adopted on
    // drop. The folder creation/adoption resolution below is retained but
    // unreachable so the feature can be re-enabled later without rewriting.
    return { DragMode::Reorder, {} };

    int idx = getStripCenterIdx(cursorPos, strips);
    if (idx < 0 || idx >= (int)strips.size())
        return {};

    auto* strip = strips[idx];
    if (strip->getTrack().getID() == state_.draggedTrackId)
        return {};

    // Master bus never participates in folder stacks (Logic/Reaper convention)
    if (strip->getTrack().isMaster())
        return {};

    const auto findTrack = [&strips](const TrackID& id) -> const Track*
    {
        for (auto* candidate : strips)
            if (candidate != nullptr && candidate->getTrack().getID() == id)
                return &candidate->getTrack();
        return nullptr;
    };

    // Track::parentTrackID_ is a UI hint derived from FolderBusCore and can be
    // empty/stale; the authoritative hierarchy is FolderBusCore::childToParent_.
    const auto getEffectiveParent = [&](const Track& t) -> TrackID
    {
        const TrackID direct = t.getParentTrackID();
        if (direct.isNotEmpty())
            return direct;
        if (folderBus_ != nullptr)
            return folderBus_->getParentFolderBus(t.getID());
        return {};
    };
    const auto isFolder = [&](const TrackID& id) -> bool
    {
        if (id.isEmpty())
            return false;
        if (folderBus_ != nullptr && folderBus_->isFolderBus(id))
            return true;
        if (const auto* t = findTrack(id))
            return t->getRole() == TrackRole::FolderBus;
        return false;
    };

    // ── 1) Folder strip OR child of an open folder: adopt across the WHOLE
    //       strip (no X edge zones). Dropping anywhere on the folder's visible
    //       child body puts the dragged track inside that folder.
    TrackID semanticTarget;
    DragMode mode = DragMode::CreateFolder;

    const TrackID childParentId = getEffectiveParent(strip->getTrack());
    if (isFolder(strip->getTrack().getID()))
    {
        semanticTarget = strip->getTrack().getID();
        mode = DragMode::AdoptIntoFolder;
    }
    else if (childParentId.isNotEmpty() && isFolder(childParentId))
    {
        semanticTarget = childParentId;
        mode = DragMode::AdoptIntoFolder;
    }
    else if (idx > 0)
    {
        // Stale-link fallback: the hovered strip has no recorded folder parent
        // but sits directly after a folder strip in the layout — it belongs to
        // that folder's child block, which is ONE adoption zone.
        auto* before = strips[(size_t) idx - 1];
        if (before != nullptr && before != strip
            && isFolder(before->getTrack().getID())
            && before->getTrack().getID() != state_.draggedTrackId)
        {
            semanticTarget = before->getTrack().getID();
            mode = DragMode::AdoptIntoFolder;
        }
    }

    // A track already inside this folder reorders among its siblings instead
    // of being re-adopted (the validator rejects re-adoption anyway).
    if (mode == DragMode::AdoptIntoFolder)
    {
        TrackID draggedParent;
        if (const auto* dragged = findTrack(state_.draggedTrackId))
            draggedParent = getEffectiveParent(*dragged);
        if (draggedParent == semanticTarget)
            return {};
    }

    // ── 2) Regular strip: only the centre band (away from the X edge zones)
    //       creates a folder; the strip edges stay "insert before/after".
    if (semanticTarget.isEmpty())
    {
        auto bounds = strip->getBounds();
        int relX = cursorPos.x - bounds.getX();
        if (relX < kEdgeZonePx || relX > bounds.getWidth() - kEdgeZonePx)
            return {};

        semanticTarget = strip->getTrack().getID();
    }

    // Never allow a folder to be adopted into itself or one of its descendants.
    auto ancestorId = semanticTarget;
    for (int guard = 0; guard < 32 && ancestorId.isNotEmpty(); ++guard)
    {
        if (ancestorId == state_.draggedTrackId)
            return {};
        const auto* ancestor = findTrack(ancestorId);
        if (ancestor == nullptr)
            break;
        ancestorId = ancestor->getParentTrackID();
    }

    return { mode, semanticTarget };
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

int MixerDragStateMachine::computeInsertionIndex(
    juce::Point<int> cursorPos,
    const std::vector<MixerStrip*>& strips,
    const std::vector<TrackID>& projectOrder) const
{
    std::vector<TrackReorderCore::VisibleTrackSpan> visibleSpans;
    visibleSpans.reserve(strips.size());

    std::vector<TrackID> visibleOrder;
    visibleOrder.reserve(strips.size());
    for (auto* strip : strips)
    {
        if (strip == nullptr || strip->getTrack().isMaster())
            continue;

        const auto bounds = strip->getBounds();
        visibleSpans.push_back({ strip->getTrack().getID(), bounds.getX(), bounds.getWidth() });
        visibleOrder.push_back(strip->getTrack().getID());
    }

    // The state machine remains usable in isolation by falling back to the
    // visible normal-strip order.  MixerPanel supplies the complete project
    // order so collapsed tracks and the pinned Master never distort the
    // destination coordinate.
    const auto& order = projectOrder.empty() ? visibleOrder : projectOrder;
    return TrackReorderCore::computeInsertionGapForVisibleSpans(
        cursorPos.x, visibleSpans, order);
}

} // namespace DAW
