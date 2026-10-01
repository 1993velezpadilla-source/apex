#include "MixerFolderIntegrationCore.h"
#include "../TrackCore/Track.h"

namespace DAW {

MixerFolderIntegrationCore::MixerFolderIntegrationCore(TrackManager& trackManager)
    : trackManager_(trackManager)
{
    hierarchyState_.addListener(this);
    animationCore_.addListener(this);
}

MixerFolderIntegrationCore::~MixerFolderIntegrationCore()
{
    hierarchyState_.removeListener(this);
    animationCore_.removeListener(this);
}

void MixerFolderIntegrationCore::rebuildHierarchy()
{
    // Gather all tracks in order
    std::vector<TrackID> orderedTrackIds;
    for (int i = 0; i < trackManager_.getNumTracks(); ++i)
    {
        if (auto* track = trackManager_.getTrack(i))
            orderedTrackIds.push_back(track->getID());
    }
    
    // Build hierarchy
    hierarchyState_.rebuildHierarchy(
        orderedTrackIds,
        [this](const TrackID& id) -> TrackID {
            if (auto* track = trackManager_.getTrack(id))
                return track->getParentTrackID();
            return {};
        },
        [this](const TrackID& id) -> bool {
            if (auto* track = trackManager_.getTrack(id))
                return track->getRole() == TrackRole::FolderBus;
            return false;
        },
        [this](const TrackID& id) -> juce::Colour {
            if (auto* track = trackManager_.getTrack(id))
                return track->getColor();
            return juce::Colour(0xFF666666);
        }
    );
}

void MixerFolderIntegrationCore::updateLayout(
    const std::vector<TrackID>& visibleTrackOrder,
    const std::function<juce::Rectangle<float>(const TrackID&)>& getBaseStripBounds)
{
    layoutCore_.computeLayout(hierarchyState_, visibleTrackOrder, getBaseStripBounds);
    updateInteractionZones();
}

void MixerFolderIntegrationCore::toggleFolderExpansion(const TrackID& folderId)
{
    bool willExpand = !hierarchyState_.isMixerExpanded(folderId);
    
    hierarchyState_.toggleMixerExpansion(folderId);
    
    if (willExpand)
        animationCore_.startExpand(folderId);
    else
        animationCore_.startCollapse(folderId);
}

void MixerFolderIntegrationCore::expandAllFolders()
{
    hierarchyState_.expandAll();
    
    // Start animations for all folders
    for (const auto& folderId : hierarchyState_.getAllFolderParents())
    {
        animationCore_.startExpand(folderId);
    }
}

void MixerFolderIntegrationCore::collapseAllFolders()
{
    hierarchyState_.collapseAll();
    
    // Start animations for all folders
    for (const auto& folderId : hierarchyState_.getAllFolderParents())
    {
        animationCore_.startCollapse(folderId);
    }
}

void MixerFolderIntegrationCore::paintFolderHierarchy(juce::Graphics& g)
{
    if (!layoutCore_.isLayoutValid())
        return;
    
    auto groups = layoutCore_.getActiveGroupLayouts();
    
    // Paint all group shelves and connectors
    for (const auto& group : groups)
    {
        float animProgress = hierarchyState_.isMixerExpanded(group.parentTrackId) ? 1.0f : 0.0f;
        
        // If animating, use animation progress
        if (animationCore_.isAnimating(group.parentTrackId))
        {
            animProgress = animationCore_.getAnimationProgress(group.parentTrackId);
        }
        
        if (animProgress <= 0.0f)
            continue;
        
        auto groupAccent = hierarchyState_.getGroupAccentColour(group.parentTrackId);
        auto shelfStyle = styleCore_.getShelfStyle(group.depth);
        auto connectorStyle = styleCore_.getConnectorStyle(groupAccent);
        
        // Paint shelf background
        MixerFolderPaintCore::paintChildShelf(g, group.shelfBounds, shelfStyle, animProgress);
        
        // Paint connector
        auto parentStripLayout = layoutCore_.getStripLayout(group.parentTrackId);
        MixerFolderPaintCore::paintConnector(g, group.connectorBounds,
                                            parentStripLayout.baseBounds,
                                            group.shelfBounds,
                                            connectorStyle, animProgress);
    }
}

void MixerFolderIntegrationCore::paintParentStripEnhancements(juce::Graphics& g, const TrackID& trackId)
{
    if (!hierarchyState_.isFolderParent(trackId))
        return;
    
    auto stripLayout = layoutCore_.getStripLayout(trackId);
    if (stripLayout.trackId.isEmpty())
        return;
    
    auto* track = trackManager_.getTrack(trackId);
    if (track == nullptr)
        return;
    
    auto parentStyle = styleCore_.getParentStyle(track->getColor());
    
    // Paint enhancements
    MixerFolderPaintCore::paintParentEnhancements(g, stripLayout.visualBounds, parentStyle);
    
    // Paint badge
    auto badgeBounds = computeBadgeBounds(trackId);
    auto badgeStyle = styleCore_.getBadgeStyle(styleCore_.getBadgeStylePreference());
    int childCount = hierarchyState_.getVisibleChildCount(trackId);
    
    MixerFolderPaintCore::paintParentBadge(g, badgeBounds, badgeStyle, childCount,
                                          styleCore_.shouldShowChildCountBadge());
    
    // Paint expand/collapse arrow
    bool expanded = hierarchyState_.isMixerExpanded(trackId);
    bool hovered = interactionCore_.isExpandButtonHovered(trackId);
    auto arrowBounds = badgeBounds.reduced(2.0f);
    
    MixerFolderPaintCore::paintExpandCollapseArrow(g, arrowBounds, expanded, hovered,
                                                  track->getColor());
}

void MixerFolderIntegrationCore::paintChildStripAccent(juce::Graphics& g, const TrackID& trackId)
{
    if (hierarchyState_.getFolderDepth(trackId) == 0)
        return;  // Not a child
    
    auto stripLayout = layoutCore_.getStripLayout(trackId);
    if (stripLayout.trackId.isEmpty())
        return;
    
    auto parentId = hierarchyState_.getParentTrackId(trackId);
    if (parentId.isEmpty())
        return;
    
    auto groupAccent = hierarchyState_.getGroupAccentColour(parentId);
    auto childStyle = styleCore_.getChildStyle(groupAccent);
    
    MixerFolderPaintCore::paintChildAccent(g, stripLayout.visualBounds, childStyle);
}

bool MixerFolderIntegrationCore::handleMouseMove(const juce::Point<float>& mousePos)
{
    return interactionCore_.handleMouseMove(mousePos);
}

bool MixerFolderIntegrationCore::handleMouseClick(const juce::Point<float>& mousePos)
{
    auto clickedFolderId = interactionCore_.handleMouseClick(mousePos);
    
    if (clickedFolderId.hasValue())
    {
        toggleFolderExpansion(*clickedFolderId);
        return true;
    }
    
    return false;
}

void MixerFolderIntegrationCore::handleRightClick(const juce::Point<float>& mousePos,
                                                 juce::Component& targetComponent)
{
    auto clickedFolderId = interactionCore_.handleMouseClick(mousePos);
    
    if (clickedFolderId.hasValue())
    {
        interactionCore_.showFolderContextMenu(
            *clickedFolderId,
            targetComponent,
            [this](const TrackID&) { expandAllFolders(); },
            [this](const TrackID&) { collapseAllFolders(); }
        );
    }
}

bool MixerFolderIntegrationCore::isTrackVisibleInMixer(const TrackID& trackId) const
{
    return hierarchyState_.isVisibleInMixer(trackId);
}

juce::Rectangle<float> MixerFolderIntegrationCore::getStripVisualBounds(const TrackID& trackId) const
{
    auto stripLayout = layoutCore_.getStripLayout(trackId);
    return stripLayout.visualBounds;
}

bool MixerFolderIntegrationCore::hasActiveAnimations() const
{
    return animationCore_.hasActiveAnimations();
}

void MixerFolderIntegrationCore::updateAnimations()
{
    animationCore_.updateAnimations(juce::Time::getMillisecondCounter());
}

void MixerFolderIntegrationCore::saveExpansionState(juce::ValueTree& projectState) const
{
    std::unordered_set<TrackID> expandedFolders;
    for (const auto& folderId : hierarchyState_.getAllFolderParents())
    {
        if (hierarchyState_.isMixerExpanded(folderId))
            expandedFolders.insert(folderId);
    }
    
    persistenceCore_.saveToValueTree(projectState, expandedFolders);
}

void MixerFolderIntegrationCore::restoreExpansionState(const juce::ValueTree& projectState)
{
    auto expandedFolders = persistenceCore_.restoreFromValueTree(projectState);
    
    for (const auto& folderId : hierarchyState_.getAllFolderParents())
    {
        bool shouldExpand = expandedFolders.count(folderId) > 0;
        hierarchyState_.setMixerExpanded(folderId, shouldExpand);
    }
}

void MixerFolderIntegrationCore::setFolderExpanded(const TrackID& folderId, bool expanded)
{
    bool wasExpanded = hierarchyState_.isMixerExpanded(folderId);
    if (wasExpanded == expanded)
        return;
    hierarchyState_.setMixerExpanded(folderId, expanded);
    if (expanded)
        animationCore_.startExpand(folderId);
    else
        animationCore_.startCollapse(folderId);
}

void MixerFolderIntegrationCore::syncExpansionNoAnim(const std::unordered_set<TrackID>& collapsedIds)
{
    for (const auto& folderId : hierarchyState_.getAllFolderParents())
    {
        bool expanded = collapsedIds.count(folderId) == 0;
        hierarchyState_.setMixerExpanded(folderId, expanded);
    }
    layoutCore_.invalidate();
}

void MixerFolderIntegrationCore::mixerFolderExpansionChanged(const TrackID& folderId, bool expanded)
{
    layoutCore_.invalidate();
    listeners_.call([](Listener& l) { l.mixerFolderLayoutChanged(); });
}

void MixerFolderIntegrationCore::mixerFolderHierarchyRebuilt()
{
    layoutCore_.invalidate();
    listeners_.call([](Listener& l) { l.mixerFolderLayoutChanged(); });
}

void MixerFolderIntegrationCore::folderAnimationStarted(const TrackID& folderId, bool expanding)
{
    listeners_.call([](Listener& l) { l.mixerFolderAnimationActive(); });
}

void MixerFolderIntegrationCore::folderAnimationCompleted(const TrackID& folderId, bool expanding)
{
    // Animation completed — may stop timer if no more active animations
    if (!animationCore_.hasActiveAnimations())
    {
        listeners_.call([](Listener& l) { l.mixerFolderLayoutChanged(); });
    }
}

void MixerFolderIntegrationCore::folderAnimationProgressed(const TrackID& folderId, float progress)
{
    listeners_.call([](Listener& l) { l.mixerFolderAnimationActive(); });
}

void MixerFolderIntegrationCore::updateInteractionZones()
{
    auto folderParents = hierarchyState_.getAllFolderParents();
    
    interactionCore_.updateInteractionZones(
        folderParents,
        [this](const TrackID& folderId) { return computeBadgeBounds(folderId); }
    );
}

juce::Rectangle<float> MixerFolderIntegrationCore::computeBadgeBounds(const TrackID& folderId) const
{
    auto stripLayout = layoutCore_.getStripLayout(folderId);
    if (stripLayout.trackId.isEmpty())
        return {};
    
    // Position badge in top-right corner of strip header
    float badgeWidth = 32.0f;
    float badgeHeight = 14.0f;
    float margin = 4.0f;
    
    return juce::Rectangle<float>(
        stripLayout.visualBounds.getRight() - badgeWidth - margin,
        stripLayout.visualBounds.getY() + margin,
        badgeWidth,
        badgeHeight
    );
}

} // namespace DAW
