#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumPanelHeaderCore
 *
 * Reusable header bar Component for any Bubblegum-themed floating panel.
 * Paints the title (bold), subtitle (dim), and the two right-side buttons
 * (minimize, close). Hover state is tracked per-button. Click events are
 * dispatched via std::function callbacks so the host panel decides what
 * to do.
 *
 * Stateless except for hover. Title/subtitle/config are settable.
 *
 * Reuse: any future floating panel (EQ, Compressor, etc.) can drop this
 * Component into its top region and wire up the callbacks the same way
 * InputTrimFloatingPanel does.
 */
class BubblegumPanelHeaderCore : public juce::Component
{
public:
    struct Config
    {
        juce::Colour titleColour       { 0xFFD8DCE6 };
        juce::Colour subtitleColour    { 0xFF7A8094 };
        juce::Colour buttonFill        { 0xFF1A1F2A };
        juce::Colour buttonBorder      { 0xFF2A2F3D };
        juce::Colour buttonIcon        { 0xFF9AA0B4 };
        juce::Colour buttonIconHovered { 0xFFD8DCE6 };

        float titleFontSize       { 13.0f };
        float subtitleFontSize    { 11.0f };
        float titleX              { 24.0f };
        float titleY              { 14.0f };
        float subtitleY           { 32.0f };

        float buttonSize          { 22.0f };
        float buttonHeight        { 18.0f };
        float buttonGap           { 8.0f  };
        float buttonRightInset    { 18.0f };
        float buttonCornerRadius  { 5.0f  };
        float buttonStroke        { 0.6f  };
        float iconStroke          { 1.6f  };
        float iconHalfSize        { 4.0f  };
    };

    BubblegumPanelHeaderCore()
    {
        setInterceptsMouseClicks(true, false);
    }

    void setTitle(juce::String t)        { title_ = std::move(t); repaint(); }
    void setSubtitle(juce::String s)     { subtitle_ = std::move(s); repaint(); }
    void setConfig(const Config& c)      { config_ = c; repaint(); }
    const Config& getConfig() const      { return config_; }

    /** Right-edge x where the leftmost button starts. Useful for panels that
     *  want to position custom widgets (e.g. an L+R toggle) just to the left
     *  of the minimize/close buttons. */
    float getRightWidgetEdgeX() const
    {
        return getMinimizeButtonBounds().getX() - config_.buttonGap;
    }

    std::function<void()> onMinimizeClicked;
    std::function<void()> onCloseClicked;

    void paint(juce::Graphics& g) override
    {
        g.setColour(config_.titleColour);
        g.setFont(juce::Font(config_.titleFontSize, juce::Font::bold));
        g.drawText(title_,
                   juce::Rectangle<float>(config_.titleX, config_.titleY,
                                          getWidth() * 0.6f, 16.0f),
                   juce::Justification::centredLeft);

        g.setColour(config_.subtitleColour);
        g.setFont(juce::Font(config_.subtitleFontSize));
        g.drawText(subtitle_,
                   juce::Rectangle<float>(config_.titleX, config_.subtitleY,
                                          getWidth() * 0.6f, 14.0f),
                   juce::Justification::centredLeft);

        paintMinimizeButton(g);
        paintCloseButton(g);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto pos = e.position;
        HoveredButton h = HoveredButton::None;
        if (getCloseButtonBounds().contains(pos))    h = HoveredButton::Close;
        else if (getMinimizeButtonBounds().contains(pos)) h = HoveredButton::Minimize;
        if (h != hoveredButton_) { hoveredButton_ = h; repaint(); }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hoveredButton_ != HoveredButton::None)
        {
            hoveredButton_ = HoveredButton::None;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (getCloseButtonBounds().contains(e.position))
        {
            if (onCloseClicked) onCloseClicked();
            return;
        }
        if (getMinimizeButtonBounds().contains(e.position))
        {
            if (onMinimizeClicked) onMinimizeClicked();
            return;
        }
    }

private:
    enum class HoveredButton { None, Minimize, Close };

    juce::Rectangle<float> getCloseButtonBounds() const
    {
        return juce::Rectangle<float>(
            getWidth() - config_.buttonRightInset - config_.buttonSize,
            config_.titleY,
            config_.buttonSize, config_.buttonHeight);
    }

    juce::Rectangle<float> getMinimizeButtonBounds() const
    {
        return juce::Rectangle<float>(
            getWidth() - config_.buttonRightInset
                - config_.buttonSize * 2.0f - config_.buttonGap,
            config_.titleY,
            config_.buttonSize, config_.buttonHeight);
    }

    void paintButtonChrome(juce::Graphics& g, juce::Rectangle<float> b) const
    {
        g.setColour(config_.buttonFill);
        g.fillRoundedRectangle(b, config_.buttonCornerRadius);
        g.setColour(config_.buttonBorder);
        g.drawRoundedRectangle(b, config_.buttonCornerRadius, config_.buttonStroke);
    }

    void paintMinimizeButton(juce::Graphics& g) const
    {
        const auto b = getMinimizeButtonBounds();
        paintButtonChrome(g, b);

        g.setColour(hoveredButton_ == HoveredButton::Minimize
                        ? config_.buttonIconHovered : config_.buttonIcon);
        const auto c = b.getCentre();
        const float h = config_.iconHalfSize;
        g.drawLine(c.x - h, c.y + 1.0f, c.x + h, c.y + 1.0f, config_.iconStroke);
    }

    void paintCloseButton(juce::Graphics& g) const
    {
        const auto b = getCloseButtonBounds();
        paintButtonChrome(g, b);

        g.setColour(hoveredButton_ == HoveredButton::Close
                        ? config_.buttonIconHovered : config_.buttonIcon);
        const auto c = b.getCentre();
        const float h = config_.iconHalfSize;
        g.drawLine(c.x - h, c.y - h, c.x + h, c.y + h, config_.iconStroke);
        g.drawLine(c.x + h, c.y - h, c.x - h, c.y + h, config_.iconStroke);
    }

    Config        config_;
    juce::String  title_;
    juce::String  subtitle_;
    HoveredButton hoveredButton_ { HoveredButton::None };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumPanelHeaderCore)
};

} // namespace DAW
