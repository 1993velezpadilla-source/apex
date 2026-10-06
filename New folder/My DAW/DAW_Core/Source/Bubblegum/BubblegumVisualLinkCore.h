#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * BubblegumVisualLinkCore — computes visual link geometry between source and targets.
 *
 * Used by the mixer renderer to draw subtle connection lines
 * from the source strip to each active send target strip.
 */
class BubblegumVisualLinkCore
{
public:
    struct LinkPoint
    {
        TrackID trackId;
        float x = 0.f, y = 0.f;
    };

    struct VisualLink
    {
        LinkPoint source;
        LinkPoint target;
        float sendLevel = 0.0f;
        bool  active    = false;
    };

    std::vector<VisualLink> computeLinks(
        const TrackID& sourceId,
        float sourceX, float sourceY,
        const std::vector<std::pair<TrackID, juce::Point<float>>>& targetPositions,
        const std::function<bool(const TrackID&)>& hasSendFn,
        const std::function<float(const TrackID&)>& getLevelFn) const
    {
        std::vector<VisualLink> links;
        LinkPoint src { sourceId, sourceX, sourceY };

        for (auto& [tid, pos] : targetPositions)
        {
            if (!hasSendFn(tid)) continue;

            VisualLink link;
            link.source    = src;
            link.target    = { tid, pos.x, pos.y };
            link.sendLevel = getLevelFn(tid);
            link.active    = true;
            links.push_back(link);
        }
        return links;
    }
};

} // namespace DAW
