#pragma once
#include <JuceHeader.h>
#include "MixerFolderHierarchyStateCore.h"
#include "MixerFolderLayoutCore.h"
#include "MixerFolderVisualStyleCore.h"
#include "MixerFolderAnimationCore.h"
#include "MixerFolderPaintCore.h"
#include "MixerFolderInteractionCore.h"
#include "MixerFolderPersistenceCore.h"
#include "../TrackCore/Track.h"

namespace DAW {

class TrackManager;

/**
 * MixerFolderIntegrationCore
 * 
 * Central integration point for mixer folder hierarchy system.
 * Orchestrates all the separate cores and provides unified API for mixer UI.
 * 
 * This is the single point of contact between the mixer UI and the folder system.
 */
class MixerFolderIntegrationCore : public MixerFolderHierarchyStateCore::Listener,
                                   public MixerFolderAnimationCore::Listener
{
public:
    MixerFolderIntegrationCore(TrackManager& trackManager);
    ~MixerFolderIntegrationCore() override;
    
    /** Rebuild hierarchy from current track state */
    void rebuildHierarchy();
    
    /** Update layout for current visible tracks and strip positions */
    void updateLayout(const std::vector<TrackID>& visibleTrackOrder,
                     const std::function<juce::Rectangle<float>(const TrackID&)>& getBaseStripBounds);
    
    /** Toggle folder expansion in mixer */
    void toggleFolderExpansion(const TrackID& folderId);
    
    /** Expand all folders */
    void expandAllFolders();
    
    /** Collapse all folders */
    void collapseAllFolders();

    /** Set a single folder's expansion with animation */
    void setFolderExpanded(const TrackID& folderId, bool expanded);

    /** Sync expansion state from an external collapsed-set, no animation (use on rebuild) */
    void syncExpansionNoAnim(const std::unordered_set<TrackID>& collapsedIds);

    /** Paint all folder hierarchy visuals (call from mixer paint) */
    void paintFolderHierarchy(juce::Graphics& g);
    
    /** Paint parent strip enhancements (call from individual strip paint) */
    void paintParentStripEnhancements(juce::Graphics& g, const TrackID& trackId);
    
    /** Paint child strip accent (call from individual strip paint) */
    void paintChildStripAccent(juce::Graphics& g, const TrackID& trackId);
    
    /** Handle mouse move (returns true if repaint needed) */
    bool handleMouseMove(const juce::Point<float>& mousePos);
    
    /** Handle mouse click (returns true if click was consumed) */
    bool handleMouseClick(const juce::Point<float>& mousePos);
    
    /** Handle right-click for context menu */
    void handleRightClick(const juce::Point<float>& mousePos, juce::Component& targetComponent);
    
    /** Check if track should be visible in mixer */
    bool isTrackVisibleInMixer(const TrackID& trackId) const;
    
    /** Get visual bounds for a strip (may be raised if child) */
    juce::Rectangle<float> getStripVisualBounds(const TrackID& trackId) const;
    
    /** Check if any animations are active (to drive repaint timer) */
    bool hasActiveAnimations() const;
    
    /** Update animations (call from timer) */
    void updateAnimations();
    
    /** Save expansion state */
    void saveExpansionState(juce::ValueTree& projectState) const;
    
    /** Restore expansion state */
    void restoreExpansionState(const juce::ValueTree& projectState);
    
    /** Listener for integration events */
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void mixerFolderLayoutChanged() = 0;
        virtual void mixerFolderAnimationActive() = 0;
    };
    
    void addListener(Listener* listener) { listeners_.add(listener); }
    void removeListener(Listener* listener) { listeners_.remove(listener); }
    
    // MixerFolderHierarchyStateCore::Listener
    void mixerFolderExpansionChanged(const TrackID& folderId, bool expanded) override;
    void mixerFolderHierarchyRebuilt() override;
    
    // MixerFolderAnimationCore::Listener
    void folderAnimationStarted(const TrackID& folderId, bool expanding) override;
    void folderAnimationCompleted(const TrackID& folderId, bool expanding) override;
    void folderAnimationProgressed(const TrackID& folderId, float progress) override;
    
private:
    TrackManager& trackManager_;
    
    MixerFolderHierarchyStateCore hierarchyState_;
    MixerFolderLayoutCore layoutCore_;
    MixerFolderVisualStyleCore styleCore_;
    MixerFolderAnimationCore animationCore_;
    MixerFolderInteractionCore interactionCore_;
    MixerFolderPersistenceCore persistenceCore_;
    
    juce::ListenerList<Listener> listeners_;
    
    void updateInteractionZones();
    juce::Rectangle<float> computeBadgeBounds(const TrackID& folderId) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFolderIntegrationCore)
};

} // namespace DAW
