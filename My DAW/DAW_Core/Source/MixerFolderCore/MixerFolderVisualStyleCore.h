#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * MixerFolderVisualStyleCore
 * 
 * Defines visual style tokens for folder hierarchy display.
 * - Badge styles
 * - Parent vs child visual differentiation
 * - Border/highlight strengths
 * - Container styles
 * - Connector styles
 * 
 * NO LAYOUT. NO STATE. Pure visual style definitions.
 */
class MixerFolderVisualStyleCore
{
public:
    // Badge style
    enum class BadgeStyle
    {
        Bus,        // "BUS"
        Folder,     // "FOLDER"
        Sum         // "SUM"
    };
    
    struct BadgeStyleTokens
    {
        juce::String text = "BUS";
        juce::Colour backgroundColour;
        juce::Colour textColour;
        juce::Colour borderColour;
        float fontSize = 9.0f;
        float paddingH = 4.0f;
        float paddingV = 2.0f;
        float cornerRadius = 3.0f;
        float borderThickness = 1.0f;
    };
    
    struct ParentStyleTokens
    {
        juce::Colour headerAccent;      // subtle top edge highlight
        float headerAccentHeight = 2.5f;
        juce::Colour borderGlow;        // stronger border/glow
        float borderGlowAlpha = 0.4f;
        float borderThickness = 1.5f;
    };
    
    struct ChildStyleTokens
    {
        juce::Colour inheritedAccent;   // derived from parent group color
        float topRailHeight = 2.0f;     // thin accent rail at top
        float topRailAlpha = 0.55f;
        juce::Colour subtleTint;        // very subtle background tint
        float subtleTintAlpha = 0.035f;
    };
    
    struct ShelfStyleTokens
    {
        juce::Colour backgroundColour;  // shelf panel background
        float backgroundAlpha = 0.18f;
        juce::Colour borderColour;      // shelf border
        float borderAlpha = 0.25f;
        float borderThickness = 1.0f;
        float cornerRadius = 6.0f;
        bool drawInnerShadow = true;
        float innerShadowAlpha = 0.15f;
        float innerShadowBlur = 4.0f;
    };
    
    struct ConnectorStyleTokens
    {
        juce::Colour lineColour;
        float lineAlpha = 0.35f;
        float lineThickness = 1.5f;
        bool drawBracket = true;        // draw subtle bracket shape
        float bracketRadius = 4.0f;
    };
    
    MixerFolderVisualStyleCore();
    
    /** Get badge style for folder parent */
    BadgeStyleTokens getBadgeStyle(BadgeStyle style) const;
    
    /** Get parent strip visual tokens */
    ParentStyleTokens getParentStyle(const juce::Colour& trackColour) const;
    
    /** Get child strip visual tokens */
    ChildStyleTokens getChildStyle(const juce::Colour& groupAccentColour) const;
    
    /** Get shelf visual tokens */
    ShelfStyleTokens getShelfStyle(int depth) const;
    
    /** Get connector visual tokens */
    ConnectorStyleTokens getConnectorStyle(const juce::Colour& groupAccentColour) const;
    
    /** Set global badge style preference */
    void setBadgeStylePreference(BadgeStyle style) { badgeStylePref_ = style; }
    BadgeStyle getBadgeStylePreference() const { return badgeStylePref_; }
    
    /** Enable/disable child count badges */
    void setShowChildCountBadge(bool show) { showChildCountBadge_ = show; }
    bool shouldShowChildCountBadge() const { return showChildCountBadge_; }
    
private:
    BadgeStyle badgeStylePref_ = BadgeStyle::Bus;
    bool showChildCountBadge_ = true;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFolderVisualStyleCore)
};

} // namespace DAW
