#pragma once

#include "../FolderBusCore/FolderBusCore.h"
#include <unordered_set>

namespace DAW
{
/** Pure UI projection: retain the original route TrackID as authority while
    resolving geometry to the outermost visible collapsed folder ancestor. */
class BubblegumFolderProjectionCore final
{
public:
    template <typename IsVisible>
    static TrackID resolveVisibleRepresentative(
        const TrackID& trackId,
        const FolderBusCore* folderCore,
        const std::unordered_set<TrackID>& collapsedFolders,
        IsVisible&& isVisible)
    {
        if (folderCore != nullptr)
        {
            for (const auto& ancestorId : folderCore->getAncestorChain(trackId))
                if (collapsedFolders.count(ancestorId) != 0 && isVisible(ancestorId))
                    return ancestorId;
        }

        return isVisible(trackId) ? trackId : TrackID{};
    }
};
}
