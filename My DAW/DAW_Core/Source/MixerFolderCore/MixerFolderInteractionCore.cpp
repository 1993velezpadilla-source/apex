#include "MixerFolderInteractionCore.h"

namespace DAW {

void MixerFolderInteractionCore::updateInteractionZones(
    const std::vector<TrackID>& folderParentIds,
    const std::function<juce::Rectangle<float>(const TrackID&)>& getBadgeBounds)
{
    interactionZones_.clear();
    
    for (const auto& folderId : folderParentIds)
    {
        InteractionZone zone;
        zone.folderId = folderId;
        zone.expandCollapseButtonBounds = getBadgeBounds(folderId);
        zone.isHovered = false;
        
        interactionZones_[folderId] = zone;
    }
}

bool MixerFolderInteractionCore::handleMouseMove(const juce::Point<float>& mousePos)
{
    bool stateChanged = false;
    
    for (auto& [folderId, zone] : interactionZones_)
    {
        bool wasHovered = zone.isHovered;
        zone.isHovered = zone.expandCollapseButtonBounds.contains(mousePos);
        
        if (wasHovered != zone.isHovered)
            stateChanged = true;
    }
    
    return stateChanged;
}

juce::Optional<TrackID> MixerFolderInteractionCore::handleMouseClick(const juce::Point<float>& mousePos)
{
    for (const auto& [folderId, zone] : interactionZones_)
    {
        if (zone.expandCollapseButtonBounds.contains(mousePos))
            return folderId;
    }
    
    return {};
}

const MixerFolderInteractionCore::InteractionZone* 
MixerFolderInteractionCore::getInteractionZone(const TrackID& folderId) const
{
    auto it = interactionZones_.find(folderId);
    return it != interactionZones_.end() ? &it->second : nullptr;
}

bool MixerFolderInteractionCore::isExpandButtonHovered(const TrackID& folderId) const
{
    auto it = interactionZones_.find(folderId);
    return it != interactionZones_.end() && it->second.isHovered;
}

void MixerFolderInteractionCore::clearHoverStates()
{
    for (auto& [folderId, zone] : interactionZones_)
    {
        zone.isHovered = false;
    }
}

void MixerFolderInteractionCore::showFolderContextMenu(
    const TrackID& folderId,
    juce::Component& targetComponent,
    const std::function<void(const TrackID&)>& onExpandAll,
    const std::function<void(const TrackID&)>& onCollapseAll)
{
    juce::PopupMenu menu;
    
    {
        juce::PopupMenu::Item expandItem("Expand All Folders");
        expandItem.action = [folderId, onExpandAll] { if (onExpandAll) onExpandAll(folderId); };
        menu.addItem(expandItem);
    }
    
    {
        juce::PopupMenu::Item collapseItem("Collapse All Folders");
        collapseItem.action = [folderId, onCollapseAll] { if (onCollapseAll) onCollapseAll(folderId); };
        menu.addItem(collapseItem);
    }
    
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&targetComponent));
}

} // namespace DAW
