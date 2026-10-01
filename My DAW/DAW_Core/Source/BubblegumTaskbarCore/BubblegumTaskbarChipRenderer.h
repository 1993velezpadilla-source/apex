#pragma once
#include <JuceHeader.h>
#include "BubblegumTaskbarChipModel.h"

namespace DAW {

/**
 * BubblegumTaskbarChipRenderer
 *
 * Paints a single chip. Three visual states blend:
 *   - active       : pink glow behind, accent border, bright label, bold, X visible
 *   - hovered idle : same as idle but X is shown so the user can close without
 *                    restoring the panel first
 *   - idle         : dim fill, dim border, dim label, no X
 *
 * The accent colour is read from the host (each panel can theme its own chip).
 * The Component uses hitsCloseButton() to route clicks on the X to a close
 * action instead of toggling restore/minimize.
 *
 * Stateless. All inputs come from the ChipModel passed in.
 */
class BubblegumTaskbarChipRenderer
{
public:
    struct Config
    {
        juce::Colour fillIdle           { 0xFF0F1219 };
        juce::Colour fillActive         { 0xFF1A1F2A };
        juce::Colour borderIdle         { 0xFF2A2F3D };
        juce::Colour dotIdle            { 0xFF5B6075 };
        juce::Colour labelIdle          { 0xFF9AA0B4 };
        juce::Colour labelActive        { 0xFFD8DCE6 };
        juce::Colour separator          { 0xFF3B4156 };
        juce::Colour closeIcon          { 0xFF7A8094 };
        juce::Colour closeIconHovered   { 0xFFD8DCE6 };

        float cornerRadius              { 16.0f };
        float borderStrokeIdle          { 0.6f  };
        float borderStrokeActive        { 1.0f  };

        float dotRadius                 { 3.5f };
        float dotXOffset                { 16.0f };
        float dotRingRadiusActive       { 6.0f };
        float dotRingStroke             { 0.6f };
        float dotRingAlpha              { 0.4f };

        float labelFontSize             { 12.0f };
        float labelXOffset              { 32.0f };

        float closeAreaWidth            { 28.0f };
        float separatorY1Inset          { 10.0f };
        float separatorY2Inset          { 10.0f };
        float closeIconSize             { 8.0f };
        float closeIconStroke           { 1.2f };

        float glowAlpha                 { 0.18f };
        float glowRadiusMult            { 1.6f  };
    };

    BubblegumTaskbarChipRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g, const BubblegumTaskbarChipModel& chip) const
    {
        const auto b = chip.bounds;
        if (b.isEmpty()) return;

        const juce::Colour accent = chip.host ? chip.host->getChipAccentColour()
                                              : juce::Colour(0xFFFF4F8A);

        if (chip.isActive)
            paintGlow(g, b, accent);

        paintBody(g, b, chip.isActive, accent);
        paintDot(g, b, chip.isActive, accent);
        paintLabel(g, b, chip);

        if (chip.isActive || chip.isHovered)
            paintCloseButton(g, b, chip.isHovered);
    }

    /** Bounds of the close button hit area, in screen coords. */
    juce::Rectangle<float> getCloseButtonBounds(juce::Rectangle<float> chipBounds) const
    {
        return juce::Rectangle<float>(chipBounds.getRight() - config_.closeAreaWidth,
                                      chipBounds.getY(),
                                      config_.closeAreaWidth,
                                      chipBounds.getHeight());
    }

    bool hitsCloseButton(juce::Point<float> p,
                         juce::Rectangle<float> chipBounds) const
    {
        return getCloseButtonBounds(chipBounds).contains(p);
    }

private:
    void paintGlow(juce::Graphics& g, juce::Rectangle<float> b,
                   juce::Colour accent) const
    {
        const auto centre = b.getCentre();
        const float r = juce::jmax(b.getWidth(), b.getHeight()) * 0.5f * config_.glowRadiusMult;
        juce::ColourGradient grad(accent.withAlpha(config_.glowAlpha),
                                  centre.x, centre.y,
                                  accent.withAlpha(0.0f),
                                  centre.x + r, centre.y, true);
        g.setGradientFill(grad);
        g.fillRect(b.expanded(r * 0.5f));
    }

    void paintBody(juce::Graphics& g, juce::Rectangle<float> b,
                   bool active, juce::Colour accent) const
    {
        g.setColour(active ? config_.fillActive : config_.fillIdle);
        g.fillRoundedRectangle(b, config_.cornerRadius);

        g.setColour(active ? accent : config_.borderIdle);
        g.drawRoundedRectangle(b, config_.cornerRadius,
                               active ? config_.borderStrokeActive
                                      : config_.borderStrokeIdle);
    }

    void paintDot(juce::Graphics& g, juce::Rectangle<float> b,
                  bool active, juce::Colour accent) const
    {
        const auto centre = juce::Point<float>(b.getX() + config_.dotXOffset,
                                               b.getCentreY());
        const float r = config_.dotRadius;
        g.setColour(active ? accent : config_.dotIdle);
        g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);

        if (active)
        {
            const float rr = config_.dotRingRadiusActive;
            g.setColour(accent.withAlpha(config_.dotRingAlpha));
            g.drawEllipse(centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f,
                          config_.dotRingStroke);
        }
    }

    void paintLabel(juce::Graphics& g, juce::Rectangle<float> b,
                    const BubblegumTaskbarChipModel& chip) const
    {
        if (chip.host == nullptr) return;
        g.setColour(chip.isActive ? config_.labelActive : config_.labelIdle);
        g.setFont(juce::Font(config_.labelFontSize,
                             chip.isActive ? juce::Font::bold : juce::Font::plain));
        const float rightInset = (chip.isActive || chip.isHovered)
                                     ? config_.closeAreaWidth : 8.0f;
        const juce::Rectangle<float> textRect(b.getX() + config_.labelXOffset,
                                              b.getY(),
                                              b.getWidth() - config_.labelXOffset
                                                  - rightInset,
                                              b.getHeight());
        g.drawText(chip.host->getChipLabel(), textRect,
                   juce::Justification::centredLeft);
    }

    void paintCloseButton(juce::Graphics& g, juce::Rectangle<float> b,
                          bool hovered) const
    {
        const auto area = getCloseButtonBounds(b);

        // Separator line on the left edge of the close area
        g.setColour(config_.separator);
        g.drawLine(area.getX(), area.getY() + config_.separatorY1Inset,
                   area.getX(), area.getBottom() - config_.separatorY2Inset,
                   0.5f);

        // X icon
        const auto centre = area.getCentre();
        const float h = config_.closeIconSize * 0.5f;
        g.setColour(hovered ? config_.closeIconHovered : config_.closeIcon);
        g.drawLine(centre.x - h, centre.y - h, centre.x + h, centre.y + h,
                   config_.closeIconStroke);
        g.drawLine(centre.x + h, centre.y - h, centre.x - h, centre.y + h,
                   config_.closeIconStroke);
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumTaskbarChipRenderer)
};

} // namespace DAW
