// ===========================================================================
// ArrangementClipStateCore.h
// Owns the list of all clips on all tracks. CRUD operations.
// Fires change callbacks. This is the single source of truth for clip data.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include <vector>
#include <functional>

namespace ArrangementEditor
{
    class ArrangementClipStateCore
    {
    public:
        ArrangementClipStateCore() = default;

        // -----------------------------------------------------------------------
        // CRUD operations
        // -----------------------------------------------------------------------
        void addClip(const ArrangementClipModel& clip);
        void removeClip(const juce::Uuid& id);
        void updateClip(const ArrangementClipModel& clip);

        ArrangementClipModel* findClip(const juce::Uuid& id);
        const ArrangementClipModel* findClip(const juce::Uuid& id) const;

        const std::vector<ArrangementClipModel>& allClips() const { return m_clips; }

        // Get clips on a specific track
        std::vector<ArrangementClipModel*> clipsOnTrack(int trackIndex);
        std::vector<const ArrangementClipModel*> clipsOnTrack(int trackIndex) const;

        // Get clips in time range
        std::vector<ArrangementClipModel*> clipsInRange(double startTime, double endTime);
        std::vector<const ArrangementClipModel*> clipsInRange(double startTime, double endTime) const;

        // Get clip at exact position
        ArrangementClipModel* clipAtPoint(int trackIndex, double time);

        // Clear all
        void clear();
        void beginBatch();
        void endBatch();

        // -----------------------------------------------------------------------
        // Callbacks
        // -----------------------------------------------------------------------
        std::function<void(const juce::Uuid&)> onClipAdded;
        std::function<void(const juce::Uuid&)> onClipRemoved;
        std::function<void(const juce::Uuid&)> onClipChanged;
        std::function<void()>                  onStateChanged;

    private:
        void notifyStateMutation();
        std::vector<ArrangementClipModel> m_clips;
        int m_batchDepth = 0;
        bool m_batchChanged = false;
    };

} // namespace ArrangementEditor
