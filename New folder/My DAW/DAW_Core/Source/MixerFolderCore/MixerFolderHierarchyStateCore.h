#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include <unordered_set>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * MixerFolderHierarchyStateCore
 * 
 * Central state model for mixer folder hierarchy.
 * Owns parent/child relationships, expansion state, and derived visibility.
 * NO UI CODE. Pure state + logic.
 */
class MixerFolderHierarchyStateCore
{
public:
    struct TrackHierarchyInfo
    {
        TrackID trackId;
        TrackID parentTrackId;
        std::vector<TrackID> childTrackIds;
        bool isFolderParent = false;
        int folderDepth = 0;
        bool mixerExpanded = true;          // expanded in mixer (independent from timeline)
        bool visibleInMixer = true;         // computed visibility based on ancestor collapse
        juce::Colour groupAccentColour;    // derived accent for this group
        int childCount = 0;                 // visible child count
        
        TrackHierarchyInfo() = default;
        explicit TrackHierarchyInfo(const TrackID& id) : trackId(id) {}
    };
    
    MixerFolderHierarchyStateCore() = default;
    
    /** Rebuild hierarchy from current track manager state */
    void rebuildHierarchy(const std::vector<TrackID>& orderedTrackIds,
                         const std::function<TrackID(const TrackID&)>& getParentFunc,
                         const std::function<bool(const TrackID&)>& isFolderFunc,
                         const std::function<juce::Colour(const TrackID&)>& getColorFunc);
    
    /** Toggle mixer expansion for a folder parent */
    void toggleMixerExpansion(const TrackID& folderId);
    
    /** Set mixer expansion state explicitly */
    void setMixerExpanded(const TrackID& folderId, bool expanded);
    
    /** Get mixer expansion state */
    bool isMixerExpanded(const TrackID& folderId) const;
    
    /** Check if track is a folder parent */
    bool isFolderParent(const TrackID& trackId) const;
    
    /** Check if track is visible in mixer (accounting for collapsed ancestors) */
    bool isVisibleInMixer(const TrackID& trackId) const;
    
    /** Get folder depth (0 = root, 1 = child, 2 = grandchild, etc.) */
    int getFolderDepth(const TrackID& trackId) const;
    
    /** Get parent track ID (empty if root) */
    TrackID getParentTrackId(const TrackID& trackId) const;
    
    /** Get child track IDs (empty if not a folder parent) */
    std::vector<TrackID> getChildTrackIds(const TrackID& trackId) const;
    
    /** Get visible child count */
    int getVisibleChildCount(const TrackID& trackId) const;
    
    /** Get group accent colour for this folder */
    juce::Colour getGroupAccentColour(const TrackID& trackId) const;
    
    /** Get all hierarchy info for a track */
    const TrackHierarchyInfo* getHierarchyInfo(const TrackID& trackId) const;
    
    /** Expand all folders in mixer */
    void expandAll();
    
    /** Collapse all folders in mixer */
    void collapseAll();
    
    /** Get all folder parent IDs */
    std::vector<TrackID> getAllFolderParents() const;
    
    /** Listener for state changes */
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void mixerFolderExpansionChanged(const TrackID& folderId, bool expanded) = 0;
        virtual void mixerFolderHierarchyRebuilt() = 0;
    };
    
    void addListener(Listener* listener) { listeners_.add(listener); }
    void removeListener(Listener* listener) { listeners_.remove(listener); }
    
private:
    std::unordered_map<TrackID, TrackHierarchyInfo> hierarchy_;
    std::unordered_set<TrackID> mixerExpandedFolders_;
    juce::ListenerList<Listener> listeners_;
    
    void computeVisibility();
    void computeDepths();
    juce::Colour deriveGroupAccent(const juce::Colour& trackColour) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFolderHierarchyStateCore)
};

} // namespace DAW
