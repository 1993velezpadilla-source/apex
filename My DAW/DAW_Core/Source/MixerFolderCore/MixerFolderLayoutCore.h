#pragma once
#include <JuceHeader.h>
#include "MixerFolderHierarchyStateCore.h"

namespace DAW {

/**
 * MixerFolderLayoutCore
 * 
 * Computes layout geometry for folder hierarchy in the mixer.
 * - Base row strip positions
 * - Elevated child shelf positions
 * - Group container bounds
 * - Connector bounds
 * 
 * NO PAINTING. Pure layout math.
 */
class MixerFolderLayoutCore
{
public:
    struct StripLayoutInfo
    {
        TrackID trackId;
        juce::Rectangle<float> baseBounds;      // strip bounds on base mixer row
        juce::Rectangle<float> visualBounds;    // actual visual bounds (may be raised for children)
        bool isChild = false;
        bool isRaised = false;
        float raiseAmount = 0.0f;
        int depth = 0;
    };
    
    struct GroupLayoutInfo
    {
        TrackID parentTrackId;
        juce::Rectangle<float> shelfBounds;     // elevated shelf background
        juce::Rectangle<float> connectorBounds; // parent-to-shelf connector
        std::vector<TrackID> childTrackIds;
        int depth = 0;
    };
    
    MixerFolderLayoutCore() = default;
    
    /** Compute all strip and group layouts from current hierarchy and strip base positions */
    void computeLayout(const MixerFolderHierarchyStateCore& hierarchyState,
                      const std::vector<TrackID>& visibleTrackOrder,
                      const std::function<juce::Rectangle<float>(const TrackID&)>& getBaseStripBounds);
    
    /** Get computed layout for a strip */
    StripLayoutInfo getStripLayout(const TrackID& trackId) const;
    
    /** Get all active group layouts (only expanded folders with visible children) */
    std::vector<GroupLayoutInfo> getActiveGroupLayouts() const;
    
    /** Get group layout for a specific parent */
    const GroupLayoutInfo* getGroupLayout(const TrackID& parentId) const;
    
    /** Check if layout is valid */
    bool isLayoutValid() const { return layoutValid_; }
    
    /** Invalidate layout (forces recompute on next access) */
    void invalidate() { layoutValid_ = false; }
    
    // Layout tuning constants
    static constexpr float kChildRaiseAmount_Level1 = 14.0f;
    static constexpr float kChildRaiseAmount_Level2 = 22.0f;
    static constexpr float kShelfPaddingH = 4.0f;
    static constexpr float kShelfPaddingV = 6.0f;
    static constexpr float kConnectorHeight = 8.0f;
    
private:
    std::unordered_map<TrackID, StripLayoutInfo> stripLayouts_;
    std::unordered_map<TrackID, GroupLayoutInfo> groupLayouts_;
    bool layoutValid_ = false;
    
    float computeRaiseAmount(int depth) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFolderLayoutCore)
};

} // namespace DAW
