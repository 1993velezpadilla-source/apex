#include "MixerFolderLayoutCore.h"

namespace DAW {

void MixerFolderLayoutCore::computeLayout(
    const MixerFolderHierarchyStateCore& hierarchyState,
    const std::vector<TrackID>& visibleTrackOrder,
    const std::function<juce::Rectangle<float>(const TrackID&)>& getBaseStripBounds)
{
    stripLayouts_.clear();
    groupLayouts_.clear();
    
    // Phase 1: Compute base strip layouts
    for (const auto& trackId : visibleTrackOrder)
    {
        if (!hierarchyState.isVisibleInMixer(trackId))
            continue;
        
        StripLayoutInfo stripInfo;
        stripInfo.trackId = trackId;
        stripInfo.baseBounds = getBaseStripBounds(trackId);
        stripInfo.depth = hierarchyState.getFolderDepth(trackId);
        stripInfo.isChild = (stripInfo.depth > 0);
        stripInfo.isRaised = stripInfo.isChild;
        stripInfo.raiseAmount = stripInfo.isChild ? computeRaiseAmount(stripInfo.depth) : 0.0f;
        
        // Visual bounds = base bounds raised upward
        stripInfo.visualBounds = stripInfo.baseBounds.translated(0.0f, -stripInfo.raiseAmount);
        
        stripLayouts_[trackId] = stripInfo;
    }
    
    // Phase 2: Compute group layouts for expanded folder parents
    for (const auto& trackId : visibleTrackOrder)
    {
        if (!hierarchyState.isFolderParent(trackId))
            continue;
        if (!hierarchyState.isMixerExpanded(trackId))
            continue;
        
        auto childIds = hierarchyState.getChildTrackIds(trackId);
        if (childIds.empty())
            continue;
        
        // Filter to visible children
        std::vector<TrackID> visibleChildren;
        for (const auto& childId : childIds)
        {
            if (hierarchyState.isVisibleInMixer(childId))
                visibleChildren.push_back(childId);
        }
        
        if (visibleChildren.empty())
            continue;
        
        GroupLayoutInfo groupInfo;
        groupInfo.parentTrackId = trackId;
        groupInfo.childTrackIds = visibleChildren;
        groupInfo.depth = hierarchyState.getFolderDepth(trackId);
        
        // Compute shelf bounds covering all visible children
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float childY = 0.0f;
        float childHeight = 0.0f;
        
        for (const auto& childId : visibleChildren)
        {
            auto it = stripLayouts_.find(childId);
            if (it == stripLayouts_.end())
                continue;
            
            const auto& childVisual = it->second.visualBounds;
            minX = juce::jmin(minX, childVisual.getX());
            maxX = juce::jmax(maxX, childVisual.getRight());
            childY = childVisual.getY();
            childHeight = childVisual.getHeight();
        }
        
        if (minX > maxX)
            continue;
        
        // Shelf background with padding
        groupInfo.shelfBounds = juce::Rectangle<float>(
            minX - kShelfPaddingH,
            childY - kShelfPaddingV,
            (maxX - minX) + kShelfPaddingH * 2.0f,
            childHeight + kShelfPaddingV * 2.0f
        );
        
        // Connector from parent base row to child shelf
        auto parentIt = stripLayouts_.find(trackId);
        if (parentIt != stripLayouts_.end())
        {
            const auto& parentBase = parentIt->second.baseBounds;
            float connectorX = parentBase.getCentreX();
            float connectorY = childY - kShelfPaddingV;
            float connectorWidth = 2.0f;
            
            groupInfo.connectorBounds = juce::Rectangle<float>(
                connectorX - connectorWidth * 0.5f,
                connectorY,
                connectorWidth,
                kConnectorHeight
            );
        }
        
        groupLayouts_[trackId] = groupInfo;
    }
    
    layoutValid_ = true;
}

MixerFolderLayoutCore::StripLayoutInfo 
MixerFolderLayoutCore::getStripLayout(const TrackID& trackId) const
{
    auto it = stripLayouts_.find(trackId);
    return it != stripLayouts_.end() ? it->second : StripLayoutInfo();
}

std::vector<MixerFolderLayoutCore::GroupLayoutInfo> 
MixerFolderLayoutCore::getActiveGroupLayouts() const
{
    std::vector<GroupLayoutInfo> result;
    for (const auto& [parentId, groupInfo] : groupLayouts_)
    {
        result.push_back(groupInfo);
    }
    return result;
}

const MixerFolderLayoutCore::GroupLayoutInfo* 
MixerFolderLayoutCore::getGroupLayout(const TrackID& parentId) const
{
    auto it = groupLayouts_.find(parentId);
    return it != groupLayouts_.end() ? &it->second : nullptr;
}

float MixerFolderLayoutCore::computeRaiseAmount(int depth) const
{
    // Level 1 children: 14px raised
    // Level 2+ children: 22px raised (or could add more per level if needed)
    if (depth == 1)
        return kChildRaiseAmount_Level1;
    else if (depth >= 2)
        return kChildRaiseAmount_Level2;
    return 0.0f;
}

} // namespace DAW
