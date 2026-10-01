#pragma once

#include "Track.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

namespace DAW::TrackReorderCore
{
    using GroupReorderRequestedCallback =
        std::function<void(const std::vector<TrackID>&, int)>;

    struct VisibleTrackSpan
    {
        TrackID trackId;
        int start = 0;
        int length = 0;
    };

    /**
     * A reorder destination is a gap in the pre-mutation project order.
     *
     * gap == 0 inserts before the first track and gap == order.size() inserts
     * after the last track.  Keeping this coordinate in the original order
     * lets the transformation compensate for every selected source index
     * before the destination instead of moving against a changing vector.
     */
    struct Plan
    {
        std::vector<TrackID> selectedInProjectOrder;
        std::vector<TrackID> finalOrder;
        std::size_t insertionIndex = 0;
        bool changed = false;
    };

    inline bool contains(const std::vector<TrackID>& ids, const TrackID& id)
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    /** Build one stable-ID block transformation without mutating the model. */
    inline Plan makePlan(const std::vector<TrackID>& originalOrder,
                         const std::vector<TrackID>& requestedSelectedIds,
                         int destinationGap)
    {
        Plan plan;
        plan.selectedInProjectOrder.reserve(requestedSelectedIds.size());
        plan.finalOrder.reserve(originalOrder.size());

        const auto gap = destinationGap <= 0
            ? std::size_t { 0 }
            : std::min<std::size_t>(static_cast<std::size_t>(destinationGap),
                                    originalOrder.size());

        std::vector<TrackID> remaining;
        remaining.reserve(originalOrder.size());

        // Walking the immutable original order both filters stale/protected
        // IDs that are not in this order and establishes the selection's
        // project-relative order.  No click-order or mutable index is used as
        // identity.
        for (const auto& id : originalOrder)
        {
            if (contains(requestedSelectedIds, id))
                plan.selectedInProjectOrder.push_back(id);
            else
                remaining.push_back(id);
        }

        // Translate the original-order gap into the remaining-list
        // coordinate system by counting every non-selected source before it.
        for (std::size_t i = 0; i < gap; ++i)
            if (!contains(requestedSelectedIds, originalOrder[i]))
                ++plan.insertionIndex;

        plan.finalOrder.insert(plan.finalOrder.end(),
                               remaining.begin(),
                               remaining.begin() + static_cast<std::ptrdiff_t>(plan.insertionIndex));
        plan.finalOrder.insert(plan.finalOrder.end(),
                               plan.selectedInProjectOrder.begin(),
                               plan.selectedInProjectOrder.end());
        plan.finalOrder.insert(plan.finalOrder.end(),
                               remaining.begin() + static_cast<std::ptrdiff_t>(plan.insertionIndex),
                               remaining.end());

        plan.changed = !plan.selectedInProjectOrder.empty()
                    && plan.finalOrder != originalOrder;
        return plan;
    }

    inline int indexOf(const std::vector<TrackID>& order, const TrackID& id)
    {
        const auto it = std::find(order.begin(), order.end(), id);
        return it == order.end() ? -1 : static_cast<int>(it - order.begin());
    }

    /**
     * Resolve a pointer coordinate against visible strips into a gap in the
     * complete project order.  Hidden/collapsed tracks are therefore skipped
     * by the presentation geometry but retained by the stable destination
     * identity and project-order index.
     */
    inline int computeInsertionGapForVisibleSpans(
        int coordinate,
        const std::vector<VisibleTrackSpan>& visibleSpans,
        const std::vector<TrackID>& projectOrder)
    {
        if (projectOrder.empty())
            return 0;

        for (const auto& span : visibleSpans)
        {
            const int centre = span.start + span.length / 2;
            if (coordinate <= centre)
            {
                const int projectIndex = indexOf(projectOrder, span.trackId);
                if (projectIndex >= 0)
                    return projectIndex;
            }
        }

        return static_cast<int>(projectOrder.size());
    }
}
