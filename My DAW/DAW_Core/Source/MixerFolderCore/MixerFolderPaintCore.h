#pragma once
#include <JuceHeader.h>
#include "MixerFolderLayoutCore.h"
#include "MixerFolderVisualStyleCore.h"
#include "MixerFolderAnimationCore.h"

namespace DAW {

/**
 * MixerFolderPaintCore
 * 
 * Renders folder hierarchy visuals in the mixer.
 * - Child shelf backgrounds
 * - Parent-to-child connectors
 * - Group containers
 * - Folder badges
 * - Child accent rails
 * 
 * Pure painting logic. NO STATE MANAGEMENT.
 */
class MixerFolderPaintCore
{
public:
    MixerFolderPaintCore() = default;
    
    /** Paint child shelf background for a group */
    static void paintChildShelf(juce::Graphics& g,
                               const juce::Rectangle<float>& shelfBounds,
                               const MixerFolderVisualStyleCore::ShelfStyleTokens& style,
                               float animationProgress);
    
    /** Paint connector from parent to child shelf */
    static void paintConnector(juce::Graphics& g,
                              const juce::Rectangle<float>& connectorBounds,
                              const juce::Rectangle<float>& parentStripBounds,
                              const juce::Rectangle<float>& shelfBounds,
                              const MixerFolderVisualStyleCore::ConnectorStyleTokens& style,
                              float animationProgress);
    
    /** Paint folder parent badge */
    static void paintParentBadge(juce::Graphics& g,
                                const juce::Rectangle<float>& badgeBounds,
                                const MixerFolderVisualStyleCore::BadgeStyleTokens& style,
                                int childCount,
                                bool showCount);
    
    /** Paint parent strip enhancements (header accent, border glow) */
    static void paintParentEnhancements(juce::Graphics& g,
                                       const juce::Rectangle<float>& stripBounds,
                                       const MixerFolderVisualStyleCore::ParentStyleTokens& style);
    
    /** Paint child strip accent (top rail, subtle tint) */
    static void paintChildAccent(juce::Graphics& g,
                                const juce::Rectangle<float>& stripBounds,
                                const MixerFolderVisualStyleCore::ChildStyleTokens& style);
    
    /** Paint expand/collapse arrow button */
    static void paintExpandCollapseArrow(juce::Graphics& g,
                                        const juce::Rectangle<float>& arrowBounds,
                                        bool expanded,
                                        bool hovered,
                                        const juce::Colour& colour);
    
private:
    static void drawInnerShadow(juce::Graphics& g,
                               const juce::Rectangle<float>& bounds,
                               float cornerRadius,
                               float shadowAlpha,
                               float shadowBlur);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFolderPaintCore)
};

} // namespace DAW
