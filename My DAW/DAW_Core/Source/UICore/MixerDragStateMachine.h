#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../TrackCore/TrackReorderCore.h"
#include <vector>

namespace DAW {

class MixerStrip;
class FolderBusCore;

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
    bool update(juce::Point<int> cursorPos,
                const std::vector<MixerStrip*>& strips,
                const std::vector<TrackID>& projectOrder = {});
    DragMode commit(juce::Point<int> cursorPos,
                    const std::vector<MixerStrip*>& strips,
                    const std::vector<TrackID>& projectOrder = {});
    void cancel();

    /** Authoritative folder topology (FolderBusCore::childToParent_ map).
     *  Used as a fallback when Track::parentTrackID_ is empty/stale so that
     *  dropping onto a visible child of an open folder always resolves the
     *  owning folder as the adoption target. */
    void setFolderBus(FolderBusCore* folderBus) noexcept { folderBus_ = folderBus; }

    const DragState& getState() const noexcept { return state_; }

private:
    struct ResolvedIntent
    {
        DragMode mode = DragMode::Reorder;
        TrackID targetTrackId;
    };

    ResolvedIntent resolveIntent(juce::Point<int> cursorPos,
                                 const std::vector<MixerStrip*>& strips) const;
    int getStripCenterIdx(juce::Point<int> cursorPos, const std::vector<MixerStrip*>& strips) const;
    int computeInsertionIndex(juce::Point<int> cursorPos,
                              const std::vector<MixerStrip*>& strips,
                              const std::vector<TrackID>& projectOrder) const;

    DragState state_;
    juce::Point<int> startPos_;
    FolderBusCore* folderBus_ = nullptr;

    static constexpr int kDragThreshold = 8;
    static constexpr int kEdgeZonePx = 14;
};

} // namespace DAW
