#include "MixerFolderHierarchyStateCore.h"

namespace DAW {

void MixerFolderHierarchyStateCore::rebuildHierarchy(
    const std::vector<TrackID>& orderedTrackIds,
    const std::function<TrackID(const TrackID&)>& getParentFunc,
    const std::function<bool(const TrackID&)>& isFolderFunc,
    const std::function<juce::Colour(const TrackID&)>& getColorFunc)
{
    hierarchy_.clear();
    
    // Build initial info for each track
    for (const auto& trackId : orderedTrackIds)
    {
        TrackHierarchyInfo info(trackId);
        info.parentTrackId = getParentFunc(trackId);
        info.isFolderParent = isFolderFunc(trackId);
        info.groupAccentColour = deriveGroupAccent(getColorFunc(trackId));
        hierarchy_[trackId] = info;
    }
    
    // Build child lists
    for (auto& [trackId, info] : hierarchy_)
    {
        if (info.parentTrackId.isNotEmpty())
        {
            if (hierarchy_.count(info.parentTrackId) > 0)
            {
                hierarchy_[info.parentTrackId].childTrackIds.push_back(trackId);
            }
        }
    }
    
    // Update child counts
    for (auto& [trackId, info] : hierarchy_)
    {
        info.childCount = (int)info.childTrackIds.size();
    }
    
    computeDepths();
    computeVisibility();
    
    listeners_.call([](Listener& l) { l.mixerFolderHierarchyRebuilt(); });
}

void MixerFolderHierarchyStateCore::toggleMixerExpansion(const TrackID& folderId)
{
    if (mixerExpandedFolders_.count(folderId) > 0)
    {
        mixerExpandedFolders_.erase(folderId);
        computeVisibility();
        listeners_.call([&](Listener& l) { l.mixerFolderExpansionChanged(folderId, false); });
    }
    else
    {
        mixerExpandedFolders_.insert(folderId);
        computeVisibility();
        listeners_.call([&](Listener& l) { l.mixerFolderExpansionChanged(folderId, true); });
    }
}

void MixerFolderHierarchyStateCore::setMixerExpanded(const TrackID& folderId, bool expanded)
{
    bool wasExpanded = mixerExpandedFolders_.count(folderId) > 0;
    if (expanded == wasExpanded)
        return;
    
    if (expanded)
        mixerExpandedFolders_.insert(folderId);
    else
        mixerExpandedFolders_.erase(folderId);
    
    computeVisibility();
    listeners_.call([&](Listener& l) { l.mixerFolderExpansionChanged(folderId, expanded); });
}

bool MixerFolderHierarchyStateCore::isMixerExpanded(const TrackID& folderId) const
{
    return mixerExpandedFolders_.count(folderId) > 0;
}

bool MixerFolderHierarchyStateCore::isFolderParent(const TrackID& trackId) const
{
    auto it = hierarchy_.find(trackId);
    return it != hierarchy_.end() && it->second.isFolderParent;
}

bool MixerFolderHierarchyStateCore::isVisibleInMixer(const TrackID& trackId) const
{
    auto it = hierarchy_.find(trackId);
    return it != hierarchy_.end() && it->second.visibleInMixer;
}

int MixerFolderHierarchyStateCore::getFolderDepth(const TrackID& trackId) const
{
    auto it = hierarchy_.find(trackId);
    return it != hierarchy_.end() ? it->second.folderDepth : 0;
}

TrackID MixerFolderHierarchyStateCore::getParentTrackId(const TrackID& trackId) const
{
    auto it = hierarchy_.find(trackId);
    return it != hierarchy_.end() ? it->second.parentTrackId : TrackID();
}

std::vector<TrackID> MixerFolderHierarchyStateCore::getChildTrackIds(const TrackID& trackId) const
{
    auto it = hierarchy_.find(trackId);
    return it != hierarchy_.end() ? it->second.childTrackIds : std::vector<TrackID>();
}

int MixerFolderHierarchyStateCore::getVisibleChildCount(const TrackID& trackId) const
{
    auto children = getChildTrackIds(trackId);
    int visibleCount = 0;
    for (const auto& childId : children)
    {
        if (isVisibleInMixer(childId))
            ++visibleCount;
    }
    return visibleCount;
}

juce::Colour MixerFolderHierarchyStateCore::getGroupAccentColour(const TrackID& trackId) const
{
    auto it = hierarchy_.find(trackId);
    return it != hierarchy_.end() ? it->second.groupAccentColour : juce::Colour(0xFF444444);
}

const MixerFolderHierarchyStateCore::TrackHierarchyInfo* 
MixerFolderHierarchyStateCore::getHierarchyInfo(const TrackID& trackId) const
{
    auto it = hierarchy_.find(trackId);
    return it != hierarchy_.end() ? &it->second : nullptr;
}

void MixerFolderHierarchyStateCore::expandAll()
{
    for (const auto& [trackId, info] : hierarchy_)
    {
        if (info.isFolderParent)
            mixerExpandedFolders_.insert(trackId);
    }
    computeVisibility();
    listeners_.call([](Listener& l) { l.mixerFolderHierarchyRebuilt(); });
}

void MixerFolderHierarchyStateCore::collapseAll()
{
    mixerExpandedFolders_.clear();
    computeVisibility();
    listeners_.call([](Listener& l) { l.mixerFolderHierarchyRebuilt(); });
}

std::vector<TrackID> MixerFolderHierarchyStateCore::getAllFolderParents() const
{
    std::vector<TrackID> result;
    for (const auto& [trackId, info] : hierarchy_)
    {
        if (info.isFolderParent)
            result.push_back(trackId);
    }
    return result;
}

void MixerFolderHierarchyStateCore::computeVisibility()
{
    for (auto& [trackId, info] : hierarchy_)
    {
        info.visibleInMixer = true;
        info.mixerExpanded = mixerExpandedFolders_.count(trackId) > 0;
        
        // Walk up parent chain — if any ancestor is collapsed, this track is hidden
        TrackID ancestorId = info.parentTrackId;
        int safety = 0;
        while (ancestorId.isNotEmpty() && safety++ < 32)
        {
            if (mixerExpandedFolders_.count(ancestorId) == 0)
            {
                // Ancestor is collapsed → hide this track
                info.visibleInMixer = false;
                break;
            }
            
            auto ancestorIt = hierarchy_.find(ancestorId);
            if (ancestorIt == hierarchy_.end())
                break;
            ancestorId = ancestorIt->second.parentTrackId;
        }
    }
}

void MixerFolderHierarchyStateCore::computeDepths()
{
    for (auto& [trackId, info] : hierarchy_)
    {
        int depth = 0;
        TrackID pid = info.parentTrackId;
        int safety = 0;
        while (pid.isNotEmpty() && safety++ < 32)
        {
            ++depth;
            auto pit = hierarchy_.find(pid);
            if (pit == hierarchy_.end())
                break;
            pid = pit->second.parentTrackId;
        }
        info.folderDepth = depth;
    }
}

juce::Colour MixerFolderHierarchyStateCore::deriveGroupAccent(const juce::Colour& trackColour) const
{
    // Derive a subtle group accent from track color
    // Soften and slightly desaturate for group identity
    float h = 0.f, s = 0.f, b = 0.f;
    trackColour.getHSB(h, s, b);
    s *= 0.65f;
    b = juce::jlimit(0.35f, 0.75f, b);
    return juce::Colour::fromHSV(h, s, b, 1.0f);
}

} // namespace DAW
