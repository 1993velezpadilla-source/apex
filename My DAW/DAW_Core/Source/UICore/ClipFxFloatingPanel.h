#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../PluginHostCore/ClipRegionPluginCore.h"

namespace DAW {

// ============================================================================
//  ClipFxFloatingPanel — Floating FX panel attached to ClipPropertiesWindow
// ============================================================================

namespace FXP {
    inline juce::Colour C(uint32_t argb) { return juce::Colour(argb); }
    constexpr uint32_t kBg        = 0xFF0E0F12;
    constexpr uint32_t kSurface   = 0xFF1A1B20;
    constexpr uint32_t kBorder    = 0xFF2A2B30;
    constexpr uint32_t kAccent    = 0xFF5BC8FF;
    constexpr uint32_t kGreen     = 0xFF52E09A;
    constexpr uint32_t kGold      = 0xFFFFB347;
    constexpr uint32_t kPink      = 0xFFFF6B9D;
    constexpr uint32_t kText      = 0xFFE8E8EC;
    constexpr uint32_t kTextDim   = 0xFF666672;
}

// ============================================================================
//  Compact plugin row with drag handle, bypass, and close
// ============================================================================
class ClipFxPluginRow : public juce::Component
{
public:
    juce::String instanceId;
    juce::String name;
    juce::String manufacturer;
    juce::String format;
    bool bypassed = false;
    int rowIndex = 0;

    std::function<void()> onOpen;
    std::function<void()> onToggleBypass;
    std::function<void()> onRemove;
    std::function<void(int fromIndex, int delta)> onReorder; // delta = scroll steps

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const bool hot = isMouseOver();
        const auto accent = bypassed ? FXP::C(FXP::kTextDim) : FXP::C(FXP::kAccent);

        // Background
        juce::ColourGradient bg(FXP::C(0xFF16171C), b.getX(), b.getY(),
                                FXP::C(0xFF0C0D10), b.getX(), b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(b.reduced(1.f, 0.5f), 3.f);

        // Border + side accent
        g.setColour(accent.withAlpha(hot ? 0.7f : 0.4f));
        g.drawRoundedRectangle(b.reduced(1.f, 0.5f), 3.f, 0.8f);
        g.setColour(accent.withAlpha(bypassed ? 0.15f : 0.45f));
        g.fillRoundedRectangle(b.reduced(1.f, 0.5f).withWidth(2.f), 1.5f);

        // Drag handle (left side)
        auto handleArea = b.withWidth(18.f);
        g.setColour(FXP::C(FXP::kTextDim).withAlpha(hot ? 0.5f : 0.28f));
        for (int i = 0; i < 3; ++i)
        {
            float yy = handleArea.getCentreY() + (i - 1) * 4.f;
            g.fillEllipse(handleArea.getCentreX() - 1.f, yy - 1.f, 2.f, 2.f);
        }

        // Name + Manufacturer
        auto textArea = b.reduced(22.f, 0.f).removeFromLeft(b.getWidth() - 90.f);
        g.setColour(bypassed ? FXP::C(FXP::kTextDim) : FXP::C(FXP::kText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.2f).withStyle("Bold")));
        g.drawText(name, textArea.removeFromTop(getHeight() / 2 + 1), juce::Justification::centredLeft, true);
        g.setColour(FXP::C(FXP::kTextDim).withAlpha(0.75f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.2f)));
        g.drawText(format + " - " + manufacturer, textArea, juce::Justification::centredLeft, true);

        // Buttons on the right
        auto right = b.removeFromRight(64.f);
        auto bypass = right.removeFromLeft(30.f).reduced(4.f, 5.f);
        auto close = right.removeFromLeft(28.f).reduced(4.f, 5.f);

        // Bypass button
        g.setColour(bypassed ? FXP::C(FXP::kGold).withAlpha(0.18f) : FXP::C(FXP::kGreen).withAlpha(0.2f));
        g.fillRoundedRectangle(bypass, 3.f);
        g.setColour(bypassed ? FXP::C(FXP::kGold) : FXP::C(FXP::kGreen));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(7.f).withStyle("Bold")));
        g.drawText(bypassed ? "OFF" : "ON", bypass, juce::Justification::centred);

        // Close button
        g.setColour(FXP::C(FXP::kPink).withAlpha(0.14f));
        g.fillRoundedRectangle(close, 3.f);
        g.setColour(FXP::C(FXP::kPink).withAlpha(0.9f));
        auto icon = close.reduced(5.f, 4.f);
        g.drawRoundedRectangle(icon.withTrimmedTop(icon.getHeight() * 0.18f), 1.2f, 1.1f);
        g.drawLine(icon.getX() + 1.f, icon.getY() + 2.f, icon.getRight() - 1.f, icon.getY() + 2.f, 1.1f);
        g.drawLine(icon.getCentreX() - 2.2f, icon.getY() + 0.5f, icon.getCentreX() + 2.2f, icon.getY() + 0.5f, 1.1f);
        g.drawLine(icon.getX() + 3.f, icon.getY() + 4.f, icon.getX() + 3.f, icon.getBottom() - 2.f, 0.9f);
        g.drawLine(icon.getCentreX(), icon.getY() + 4.f, icon.getCentreX(), icon.getBottom() - 2.f, 0.9f);
        g.drawLine(icon.getRight() - 3.f, icon.getY() + 4.f, icon.getRight() - 3.f, icon.getBottom() - 2.f, 0.9f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto b = getLocalBounds();
        auto right = b.removeFromRight(64);
        auto bypass = right.removeFromLeft(30).reduced(4, 5);
        auto close = right.removeFromLeft(28).reduced(4, 5);

        if (close.contains(e.position.toInt())) { if (onRemove) onRemove(); return; }
        if (bypass.contains(e.position.toInt())) { if (onToggleBypass) onToggleBypass(); return; }
        if (onOpen) onOpen();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (onReorder)
        {
            int delta = wheel.deltaY > 0.05f ? -1 : (wheel.deltaY < -0.05f ? 1 : 0);
            if (delta != 0)
                onReorder(rowIndex, delta);
        }
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
};

// ============================================================================
//  Floating FX Panel with animation
// ============================================================================
class ClipFxFloatingPanel : public juce::Component,
                            private juce::Timer,
                            private juce::KeyListener
{
public:
    static constexpr int kPanelWidth = 320;
    static constexpr int kPanelMaxHeight = 380;
    static constexpr int kMaxVisibleRows = 6;
    static constexpr int kRowHeight = 34;
    static constexpr int kHeaderHeight = 36;
    static constexpr int kAddBtnHeight = 32;

    std::function<void()> onAddPluginClicked;
    std::function<void(const juce::String&)> onOpenPlugin;
    std::function<void(const juce::String&)> onToggleBypass;
    std::function<void(const juce::String&)> onRemovePlugin;
    std::function<void(int fromIndex, int toIndex)> onReorderPlugin;
    std::function<void()> onClose;
    std::function<bool(const juce::KeyPress&)> onKeyPress;

    ClipFxFloatingPanel()
    {
        setOpaque(false);
        setSize(kPanelWidth, 100);
        setAlwaysOnTop(true);
        setInterceptsMouseClicks(true, true);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
        addKeyListener(this);

        // Setup add button
        addBtn_.onClick = [this]
        {
            if (onAddPluginClicked)
                onAddPluginClicked();
        };
        viewport_.setViewedComponent(&rowsHost_, false);
        viewport_.setScrollBarsShown(true, false, true, false);
        viewport_.setScrollBarThickness(8);
        addAndMakeVisible(viewport_);
        addAndMakeVisible(addBtn_);
    }

    void setPlugins(const std::vector<ClipRegionPluginCore::EntryInfo>& plugins)
    {
        plugins_ = plugins;
        rebuildRows();
        updateSize();
    }

    void show(juce::Point<int> anchorPos)
    {
        anchorPos_ = anchorPos;
        visible_ = true;
        animationPhase_ = 0.0f;
        setVisible(true);
        startTimerHz(60);
    }

    void hide()
    {
        visible_ = false;
        animationPhase_ = 1.0f;
        startTimerHz(60);
    }

    bool isShowing() const { return visible_ && animationPhase_ >= 0.99f; }
    float getAnimationPhase() const noexcept { return animationPhase_; }
    juce::Rectangle<int> getAddButtonScreenBounds() const { return addBtn_.getScreenBounds(); }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float alpha = juce::jlimit(0.f, 1.f, animationPhase_);

        // Shadow
        juce::Path shadowPath;
        shadowPath.addRoundedRectangle(b.reduced(4.f), 8.f);
        g.setColour(juce::Colours::black.withAlpha(0.4f * alpha));
        g.fillPath(shadowPath);

        // Main background with gradient
        juce::ColourGradient mainGrad(FXP::C(FXP::kSurface).withAlpha(0.98f * alpha), b.getX(), b.getY(),
                                      FXP::C(FXP::kBg).withAlpha(0.98f * alpha), b.getX(), b.getBottom(), false);
        g.setGradientFill(mainGrad);
        g.fillRoundedRectangle(b, 8.f);

        // Accent border
        g.setColour(FXP::C(FXP::kAccent).withAlpha(0.5f * alpha));
        g.drawRoundedRectangle(b, 8.f, 1.2f);

        // Top accent glow
        juce::ColourGradient topGlow(FXP::C(FXP::kAccent).withAlpha(0.15f * alpha), b.getX(), b.getY(),
                                     juce::Colours::transparentBlack, b.getX(), b.getY() + 30.f, false);
        g.setGradientFill(topGlow);
        g.fillRoundedRectangle(b.withHeight(30.f), 8.f);

        // Header text
        auto header = b.removeFromTop((float)kHeaderHeight);
        g.setColour(FXP::C(FXP::kText).withAlpha(alpha));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.f).withStyle("Bold")));
        g.drawText("CLIP FX", header.reduced(12.f, 0.f).withTrimmedRight(52.f), juce::Justification::centredLeft);
        g.setColour(FXP::C(FXP::kTextDim).withAlpha(0.7f * alpha));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.f)));
        auto countStr = juce::String(plugins_.size()) + " plugin" + (plugins_.size() == 1 ? "" : "s");
        g.drawText(countStr, header.reduced(12.f, 0.f).withTrimmedRight(24.f), juce::Justification::centredRight);

        auto close = closeButtonBounds().toFloat();
        const bool hotClose = closeButtonHot_;
        g.setColour(FXP::C(FXP::kPink).withAlpha(hotClose ? 0.30f : 0.16f));
        g.fillEllipse(close);
        g.setColour(FXP::C(FXP::kPink).withAlpha(hotClose ? 1.0f : 0.78f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.f).withStyle("Bold")));
        g.drawText("x", close, juce::Justification::centred);

        const float shimmerX = std::fmod(idlePhase_ * 42.f, (float) getWidth() + 80.f) - 40.f;
        juce::ColourGradient shimmer(juce::Colours::transparentBlack, shimmerX - 36.f, 0.f,
                                     FXP::C(FXP::kAccent).withAlpha(0.10f * alpha), shimmerX, 0.f, false);
        shimmer.addColour(1.0, juce::Colours::transparentBlack);
        g.setGradientFill(shimmer);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), 8.f);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool nowHot = closeButtonBounds().contains(e.position.toInt());
        if (nowHot != closeButtonHot_)
        {
            closeButtonHot_ = nowHot;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (closeButtonHot_)
        {
            closeButtonHot_ = false;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        if (closeButtonBounds().contains(e.position.toInt()))
        {
            if (onClose)
                onClose();
            return;
        }

        if (addBtn_.getBounds().contains(e.position.toInt()))
        {
            if (onAddPluginClicked)
                onAddPluginClicked();
            return;
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(kHeaderHeight);
        area.removeFromTop(4);

        // Add button at top
        addBtn_.setBounds(area.removeFromTop(kAddBtnHeight).reduced(4, 0));
        area.removeFromTop(6);
        viewport_.setBounds(area);

        const int contentHeight = (int) pluginRows_.size() * kRowHeight;
        rowsHost_.setSize(juce::jmax(1, viewport_.getWidth() - 2), juce::jmax(contentHeight, viewport_.getHeight()));

        // Plugin rows
        auto rowArea = rowsHost_.getLocalBounds();
        for (auto* row : pluginRows_)
        {
            row->setBounds(rowArea.removeFromTop(kRowHeight).reduced(4, 1));
        }
    }

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (onKeyPress)
            return onKeyPress(key);

        return false;
    }

    bool keyStateChanged(bool, juce::Component*) override
    {
        return false;
    }

    void timerCallback() override
    {
        if (visible_)
        {
            animationPhase_ += 0.15f;
            if (animationPhase_ >= 1.0f)
            {
                animationPhase_ = 1.0f;
            }
        }
        else
        {
            animationPhase_ -= 0.18f;
            if (animationPhase_ <= 0.0f)
            {
                animationPhase_ = 0.0f;
                setVisible(false);
                stopTimer();
            }
        }

        // Smooth easing
        idlePhase_ += 0.025f;
        if (idlePhase_ > juce::MathConstants<float>::twoPi)
            idlePhase_ -= juce::MathConstants<float>::twoPi;
        float easedPhase = animationPhase_ * animationPhase_ * (3.0f - 2.0f * animationPhase_); // smoothstep
        setAlpha(easedPhase);
        repaint();
    }

    juce::Rectangle<int> closeButtonBounds() const
    {
        return { getWidth() - 24, 8, 16, 16 };
    }

    void rebuildRows()
    {
        pluginRows_.clear();

        for (size_t i = 0; i < plugins_.size(); ++i)
        {
            auto* row = pluginRows_.add(new ClipFxPluginRow());
            const auto& info = plugins_[i];
            row->instanceId = info.instanceId;
            row->name = info.name;
            row->manufacturer = info.manufacturer;
            row->format = info.format;
            row->bypassed = info.bypassed;
            row->rowIndex = (int)i;

            row->onOpen = [this, id = info.instanceId]()
            {
                if (onOpenPlugin) onOpenPlugin(id);
            };

            row->onToggleBypass = [this, id = info.instanceId]()
            {
                if (onToggleBypass) onToggleBypass(id);
            };

            row->onRemove = [this, id = info.instanceId]()
            {
                if (onRemovePlugin) onRemovePlugin(id);
            };

            row->onReorder = [this](int fromIndex, int delta)
            {
                int toIndex = juce::jlimit(0, (int)plugins_.size() - 1, fromIndex + delta);
                if (toIndex != fromIndex && onReorderPlugin)
                    onReorderPlugin(fromIndex, toIndex);
            };

            rowsHost_.addAndMakeVisible(row);
        }

        resized();
    }

    void updateSize()
    {
        const int numRows = (int)plugins_.size();
        const int visibleRows = juce::jlimit(0, kMaxVisibleRows, numRows);
        const int rowsHeight = juce::jmax(kRowHeight, visibleRows * kRowHeight);
        const int contentHeight = kHeaderHeight + kAddBtnHeight + 10 + rowsHeight + 12;
        const int finalHeight = juce::jmin(contentHeight, kPanelMaxHeight);
        setSize(kPanelWidth, finalHeight);
    }

    // Add button component
    class AddPluginButton : public juce::Component
    {
    public:
        std::function<void()> onClick;

        void paint(juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            const bool hot = isMouseOver();

            // Background
            juce::ColourGradient bg(FXP::C(FXP::kAccent).withAlpha(hot ? 0.28f : 0.18f), b.getX(), b.getY(),
                                    FXP::C(FXP::kAccent).withAlpha(hot ? 0.18f : 0.08f), b.getX(), b.getBottom(), false);
            g.setGradientFill(bg);
            g.fillRoundedRectangle(b, 4.f);

            // Border
            g.setColour(FXP::C(FXP::kAccent).withAlpha(hot ? 0.7f : 0.45f));
            g.drawRoundedRectangle(b, 4.f, 1.2f);

            // Plus icon
            auto iconArea = b.removeFromLeft(b.getHeight()).reduced(8.f);
            g.setColour(FXP::C(FXP::kAccent).withAlpha(hot ? 1.0f : 0.85f));
            g.fillRoundedRectangle(iconArea.withSizeKeepingCentre(2.f, 12.f), 1.f);
            g.fillRoundedRectangle(iconArea.withSizeKeepingCentre(12.f, 2.f), 1.f);

            // Text
            g.setColour(FXP::C(FXP::kText).withAlpha(hot ? 1.0f : 0.9f));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f).withStyle("Bold")));
            g.drawText("ADD PLUGIN", b.reduced(4.f, 0.f), juce::Justification::centred);
        }

        void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit(const juce::MouseEvent&) override { repaint(); }
    } addBtn_;

    juce::Viewport viewport_;
    juce::Component rowsHost_;
    std::vector<ClipRegionPluginCore::EntryInfo> plugins_;
    juce::OwnedArray<ClipFxPluginRow> pluginRows_;
    juce::Point<int> anchorPos_;
    bool visible_ = false;
    bool closeButtonHot_ = false;
    float animationPhase_ = 0.0f;
    float idlePhase_ = 0.0f;
};

class ClipFxCableOverlay : public juce::Component
{
public:
    ClipFxCableOverlay()
    {
        setOpaque(false);
        setInterceptsMouseClicks(false, false);
    }

    void setCable(juce::Point<int> panelAnchor, juce::Point<int> buttonAnchor, float alpha)
    {
        previousPanelAnchor_ = panelAnchor_;
        panelAnchor_ = panelAnchor;
        buttonAnchor_ = buttonAnchor;
        alpha_ = juce::jlimit(0.f, 1.f, alpha);
        const auto dx = (float) (panelAnchor_.x - previousPanelAnchor_.x);
        const auto dy = (float) (panelAnchor_.y - previousPanelAnchor_.y);
        const auto dist = std::sqrt(dx * dx + dy * dy);
        tension_ = juce::jlimit(0.f, 1.f, tension_ + dist * 0.035f);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (alpha_ <= 0.01f)
            return;

        auto start = getLocalPoint(getParentComponent(), panelAnchor_).toFloat();
        auto end = getLocalPoint(getParentComponent(), buttonAnchor_).toFloat();

        juce::Path cable;
        cable.startNewSubPath(start);
        float controlOffset = juce::jmax(70.f, std::abs(end.x - start.x) * 0.48f);
        const float rubberSag = (24.f + 58.f * tension_) * std::sin(flowPhase_ * 0.72f + tension_ * 2.4f);
        const float snapBack = tension_ * 72.f;
        cable.cubicTo({ start.x + controlOffset, start.y + rubberSag },
                      { end.x - controlOffset, end.y - rubberSag * 0.35f + snapBack },
                      end);

        g.setColour(FXP::C(FXP::kAccent).withAlpha(0.20f * alpha_));
        g.strokePath(cable, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::ColourGradient grad(FXP::C(FXP::kAccent).withAlpha(0.95f * alpha_), start.x, start.y,
                                  FXP::C(FXP::kAccent).brighter(0.35f).withAlpha(0.95f * alpha_), end.x, end.y, false);
        g.setGradientFill(grad);
        g.strokePath(cable, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(juce::Colours::white.withAlpha(0.35f * alpha_));
        g.strokePath(cable, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float pulse = 0.5f + 0.5f * std::sin(flowPhase_);
        const float t = std::fmod(flowPhase_ * 0.12f, 1.0f);
        juce::Point<float> pulsePoint = cable.getPointAlongPath(cable.getLength() * t);
        g.setColour(FXP::C(FXP::kAccent).brighter(0.45f).withAlpha((0.35f + 0.35f * pulse) * alpha_));
        g.fillEllipse(pulsePoint.x - 7.f, pulsePoint.y - 7.f, 14.f, 14.f);

        g.setColour(FXP::C(FXP::kAccent).withAlpha(0.95f * alpha_));
        g.fillEllipse(end.x - 4.f, end.y - 4.f, 8.f, 8.f);
        g.fillEllipse(start.x - 4.f, start.y - 4.f, 8.f, 8.f);
        g.setColour(FXP::C(FXP::kAccent).withAlpha(0.35f * alpha_));
        g.fillEllipse(end.x - 10.f, end.y - 10.f, 20.f, 20.f);
    }

    void advanceAnimation()
    {
        flowPhase_ += 0.18f;
        tension_ *= 0.975f;
        if (flowPhase_ > juce::MathConstants<float>::twoPi * 8.f)
            flowPhase_ = 0.f;
        repaint();
    }

private:
    juce::Point<int> panelAnchor_;
    juce::Point<int> previousPanelAnchor_;
    juce::Point<int> buttonAnchor_;
    float alpha_ = 0.f;
    float flowPhase_ = 0.f;
    float tension_ = 0.f;
};

} // namespace DAW
