#include "MixerFolderPaintCore.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

void MixerFolderPaintCore::paintChildShelf(juce::Graphics& g,
                                          const juce::Rectangle<float>& shelfBounds,
                                          const MixerFolderVisualStyleCore::ShelfStyleTokens& style,
                                          float animationProgress)
{
    if (shelfBounds.isEmpty() || animationProgress <= 0.0f)
        return;
    
    float alpha = animationProgress;
    
    // Background fill
    g.setColour(style.backgroundColour.withAlpha(style.backgroundAlpha * alpha));
    g.fillRoundedRectangle(shelfBounds, style.cornerRadius);
    
    // Border
    g.setColour(style.borderColour.withAlpha(style.borderAlpha * alpha));
    g.drawRoundedRectangle(shelfBounds, style.cornerRadius, style.borderThickness);
    
    // Inner shadow for depth
    if (style.drawInnerShadow)
    {
        drawInnerShadow(g, shelfBounds, style.cornerRadius, 
                       style.innerShadowAlpha * alpha, style.innerShadowBlur);
    }
}

void MixerFolderPaintCore::paintConnector(juce::Graphics& g,
                                         const juce::Rectangle<float>& connectorBounds,
                                         const juce::Rectangle<float>& parentStripBounds,
                                         const juce::Rectangle<float>& shelfBounds,
                                         const MixerFolderVisualStyleCore::ConnectorStyleTokens& style,
                                         float animationProgress)
{
    if (connectorBounds.isEmpty() || animationProgress <= 0.0f)
        return;
    
    float alpha = animationProgress;
    g.setColour(style.lineColour.withAlpha(style.lineAlpha * alpha));
    
    if (style.drawBracket)
    {
        // Draw subtle bracket from parent center to shelf top
        juce::Path bracket;
        float centerX = connectorBounds.getCentreX();
        float startY = parentStripBounds.getBottom();
        float endY = shelfBounds.getY();
        float midY = (startY + endY) * 0.5f;
        
        bracket.startNewSubPath(centerX, startY);
        bracket.lineTo(centerX, midY);
        bracket.lineTo(centerX - style.bracketRadius, midY + style.bracketRadius);
        
        g.strokePath(bracket, juce::PathStrokeType(style.lineThickness,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
    else
    {
        // Simple vertical line
        g.fillRect(connectorBounds);
    }
}

void MixerFolderPaintCore::paintParentBadge(juce::Graphics& g,
                                           const juce::Rectangle<float>& badgeBounds,
                                           const MixerFolderVisualStyleCore::BadgeStyleTokens& style,
                                           int childCount,
                                           bool showCount)
{
    if (badgeBounds.isEmpty())
        return;
    
    auto& theme = Theme::getInstance();
    
    // Badge background
    g.setColour(style.backgroundColour);
    g.fillRoundedRectangle(badgeBounds, style.cornerRadius);
    
    // Badge border
    g.setColour(style.borderColour);
    g.drawRoundedRectangle(badgeBounds, style.cornerRadius, style.borderThickness);
    
    // Badge text
    g.setColour(style.textColour);
    g.setFont(juce::Font(style.fontSize, juce::Font::bold));
    
    juce::String badgeText = style.text;
    if (showCount && childCount > 0)
        badgeText += " [" + juce::String(childCount) + "]";
    
    g.drawText(badgeText, badgeBounds, juce::Justification::centred);
}

void MixerFolderPaintCore::paintParentEnhancements(juce::Graphics& g,
                                                  const juce::Rectangle<float>& stripBounds,
                                                  const MixerFolderVisualStyleCore::ParentStyleTokens& style)
{
    if (stripBounds.isEmpty())
        return;
    
    // Header accent at top
    g.setColour(style.headerAccent);
    g.fillRect(stripBounds.getX(), stripBounds.getY(), 
              stripBounds.getWidth(), style.headerAccentHeight);
    
    // Stronger border glow
    g.setColour(style.borderGlow.withAlpha(style.borderGlowAlpha));
    g.drawRect(stripBounds, style.borderThickness);
}

void MixerFolderPaintCore::paintChildAccent(juce::Graphics& g,
                                           const juce::Rectangle<float>& stripBounds,
                                           const MixerFolderVisualStyleCore::ChildStyleTokens& style)
{
    if (stripBounds.isEmpty())
        return;
    
    // Top accent rail
    g.setColour(style.inheritedAccent.withAlpha(style.topRailAlpha));
    g.fillRect(stripBounds.getX(), stripBounds.getY(),
              stripBounds.getWidth(), style.topRailHeight);
    
    // Subtle background tint
    g.setColour(style.subtleTint.withAlpha(style.subtleTintAlpha));
    g.fillRect(stripBounds);
}

void MixerFolderPaintCore::paintExpandCollapseArrow(juce::Graphics& g,
                                                   const juce::Rectangle<float>& arrowBounds,
                                                   bool expanded,
                                                   bool hovered,
                                                   const juce::Colour& colour)
{
    if (arrowBounds.isEmpty())
        return;
    
    auto& theme = Theme::getInstance();
    
    // Button background
    g.setColour(hovered ? theme.colors.controlHover : theme.colors.controlIdle.withAlpha(0.5f));
    g.fillRoundedRectangle(arrowBounds, 3.0f);
    
    // Button border
    g.setColour(theme.colors.border.withAlpha(hovered ? 0.6f : 0.35f));
    g.drawRoundedRectangle(arrowBounds, 3.0f, 1.0f);
    
    // Arrow icon
    auto center = arrowBounds.getCentre();
    const float arrowSize = 3.5f;
    
    juce::Path arrow;
    if (expanded)
    {
        // Down arrow (▼)
        arrow.startNewSubPath(center.x - arrowSize, center.y - arrowSize * 0.4f);
        arrow.lineTo(center.x, center.y + arrowSize * 0.6f);
        arrow.lineTo(center.x + arrowSize, center.y - arrowSize * 0.4f);
    }
    else
    {
        // Right arrow (▶)
        arrow.startNewSubPath(center.x - arrowSize * 0.4f, center.y - arrowSize);
        arrow.lineTo(center.x + arrowSize * 0.6f, center.y);
        arrow.lineTo(center.x - arrowSize * 0.4f, center.y + arrowSize);
    }
    
    g.setColour(hovered ? juce::Colours::white : colour);
    g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void MixerFolderPaintCore::drawInnerShadow(juce::Graphics& g,
                                          const juce::Rectangle<float>& bounds,
                                          float cornerRadius,
                                          float shadowAlpha,
                                          float shadowBlur)
{
    // Simple inner shadow effect using gradient at top edge
    juce::ColourGradient gradient(
        juce::Colours::black.withAlpha(shadowAlpha),
        bounds.getX(), bounds.getY(),
        juce::Colours::transparentBlack,
        bounds.getX(), bounds.getY() + shadowBlur,
        false
    );
    
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(bounds.getX(), bounds.getY(),
                          bounds.getWidth(), shadowBlur,
                          cornerRadius);
}

} // namespace DAW
