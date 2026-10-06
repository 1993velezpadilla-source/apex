#pragma once
#include <JuceHeader.h>
#include "MixerFolderHierarchyStateCore.h"
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * MixerFolderInteractionCore
 * 
 * Handles user interaction with folder hierarchy in mixer.
 * - Expand/collapse button hit-testing
 * - Click handling
 * - Hover state management
 * - Optional context menu actions
 * 
 * NO PAINTING. Pure interaction logic.
 */
class MixerFolderInteractionCore
{
public:
    struct InteractionZone
    {
        TrackID folderId;
        juce::Rectangle<float> expandCollapseButtonBounds;
        bool isHovered = false;
    };
    
    MixerFolderInteractionCore() = default;
    
    /** Update interaction zones for visible folder parents */
    void updateInteractionZones(const std::vector<TrackID>& folderParentIds,
                               const std::function<juce::Rectangle<float>(const TrackID&)>& getBadgeBounds);
    
    /** Handle mouse move — returns true if hover state changed */
    bool handleMouseMove(const juce::Point<float>& mousePos);
    
    /** Handle mouse click — returns folder ID if expand/collapse was clicked */
    juce::Optional<TrackID> handleMouseClick(const juce::Point<float>& mousePos);
    
    /** Get interaction zone for a folder */
    const InteractionZone* getInteractionZone(const TrackID& folderId) const;
    
    /** Check if folder expand button is hovered */
    bool isExpandButtonHovered(const TrackID& folderId) const;
    
    /** Clear all hover states */
    void clearHoverStates();
    
    /** Show context menu for folder actions */
    void showFolderContextMenu(const TrackID& folderId,
                              juce::Component& targetComponent,
                              const std::function<void(const TrackID&)>& onExpandAll,
                              const std::function<void(const TrackID&)>& onCollapseAll);
    
private:
    std::unordered_map<TrackID, InteractionZone> interactionZones_;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFolderInteractionCore)
};

} // namespace DAW
