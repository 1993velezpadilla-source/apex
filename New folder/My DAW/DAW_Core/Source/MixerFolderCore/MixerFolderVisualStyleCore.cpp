#include "MixerFolderVisualStyleCore.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

MixerFolderVisualStyleCore::MixerFolderVisualStyleCore()
{
}

MixerFolderVisualStyleCore::BadgeStyleTokens 
MixerFolderVisualStyleCore::getBadgeStyle(BadgeStyle style) const
{
    auto& theme = Theme::getInstance();
    
    BadgeStyleTokens tokens;
    
    switch (style)
    {
        case BadgeStyle::Bus:
            tokens.text = "BUS";
            break;
        case BadgeStyle::Folder:
            tokens.text = "FOLDER";
            break;
        case BadgeStyle::Sum:
            tokens.text = "SUM";
            break;
    }
    
    // Premium dark badge styling
    tokens.backgroundColour = juce::Colour(0xFF1A1A1A);
    tokens.textColour = juce::Colour(0xFFB8B8B8);
    tokens.borderColour = juce::Colour(0xFF505050);
    tokens.fontSize = 8.5f;
    tokens.paddingH = 5.0f;
    tokens.paddingV = 2.5f;
    tokens.cornerRadius = 3.0f;
    tokens.borderThickness = 1.0f;
    
    return tokens;
}

MixerFolderVisualStyleCore::ParentStyleTokens 
MixerFolderVisualStyleCore::getParentStyle(const juce::Colour& trackColour) const
{
    ParentStyleTokens tokens;
    
    // Derive a subtle header accent from track color
    tokens.headerAccent = trackColour.withAlpha(0.7f);
    tokens.headerAccentHeight = 2.5f;
    
    // Stronger border glow
    tokens.borderGlow = trackColour;
    tokens.borderGlowAlpha = 0.4f;
    tokens.borderThickness = 1.5f;
    
    return tokens;
}

MixerFolderVisualStyleCore::ChildStyleTokens 
MixerFolderVisualStyleCore::getChildStyle(const juce::Colour& groupAccentColour) const
{
    ChildStyleTokens tokens;
    
    // Inherited accent for top rail
    tokens.inheritedAccent = groupAccentColour;
    tokens.topRailHeight = 2.0f;
    tokens.topRailAlpha = 0.55f;
    
    // Very subtle tint
    tokens.subtleTint = groupAccentColour;
    tokens.subtleTintAlpha = 0.035f;
    
    return tokens;
}

MixerFolderVisualStyleCore::ShelfStyleTokens 
MixerFolderVisualStyleCore::getShelfStyle(int depth) const
{
    ShelfStyleTokens tokens;
    
    // Subtle elevated shelf background
    tokens.backgroundColour = juce::Colour(0xFF0A0A0A);
    tokens.backgroundAlpha = 0.18f;
    
    // Soft border
    tokens.borderColour = juce::Colour(0xFF303030);
    tokens.borderAlpha = 0.25f;
    tokens.borderThickness = 1.0f;
    tokens.cornerRadius = 6.0f;
    
    // Inner shadow for depth
    tokens.drawInnerShadow = true;
    tokens.innerShadowAlpha = 0.15f;
    tokens.innerShadowBlur = 4.0f;
    
    // Deeper nesting could have slightly different styling if needed
    if (depth > 1)
    {
        tokens.backgroundAlpha *= 0.85f;
        tokens.cornerRadius = 4.0f;
    }
    
    return tokens;
}

MixerFolderVisualStyleCore::ConnectorStyleTokens 
MixerFolderVisualStyleCore::getConnectorStyle(const juce::Colour& groupAccentColour) const
{
    ConnectorStyleTokens tokens;
    
    // Subtle connector derived from group accent
    tokens.lineColour = groupAccentColour;
    tokens.lineAlpha = 0.35f;
    tokens.lineThickness = 1.5f;
    
    // Draw bracket shape
    tokens.drawBracket = true;
    tokens.bracketRadius = 4.0f;
    
    return tokens;
}

} // namespace DAW
