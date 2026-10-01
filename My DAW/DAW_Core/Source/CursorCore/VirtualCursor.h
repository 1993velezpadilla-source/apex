#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../InteractionModeCore/InteractionModeManager.h"

namespace DAW {

class VirtualCursor : public juce::Component
{
public:
    VirtualCursor()
    {
        setInterceptsMouseClicks(false, false);
    }

    void setMode(InteractionMode mode)
    {
        mode_ = mode;
        bool hybridMode = (mode == InteractionMode::Hybrid);
        setInterceptsMouseClicks(hybridMode, hybridMode);
        setVisible(hybridMode);
        repaint();
    }

    InteractionMode getMode() const noexcept { return mode_; }

    void moveCursor(float dx, float dy)
    {
        cursorX_ = juce::jlimit(0.f, (float) juce::jmax(1, getWidth()),  cursorX_ + dx * sensitivity_);
        cursorY_ = juce::jlimit(0.f, (float) juce::jmax(1, getHeight()), cursorY_ + dy * sensitivity_);
        repaint();
    }

    void setSensitivity(float s) noexcept { sensitivity_ = juce::jlimit(0.1f, 5.f, s); }
    float getSensitivity() const noexcept { return sensitivity_; }
    juce::Point<float> getCursorPosition() const noexcept { return { cursorX_, cursorY_ }; }

    std::function<void()> onRightClickRequested;
    std::function<void()> onScrollToggled;

    void paint(juce::Graphics& g) override
    {
        if (mode_ != InteractionMode::Hybrid)
            return;

        drawPuck(g);
    }

    void resized() override
    {
        if (puck_.isEmpty())
            puck_.setPosition(getWidth() - kPuckW - 20, getHeight() - kPuckH - 80);
        else
            puck_.setPosition(
                juce::jlimit(0, juce::jmax(0, getWidth()  - kPuckW), puck_.getX()),
                juce::jlimit(0, juce::jmax(0, getHeight() - kPuckH), puck_.getY()));

        if (cursorX_ == 0.f && cursorY_ == 0.f)
        {
            cursorX_ = (float) getWidth()  * 0.5f;
            cursorY_ = (float) getHeight() * 0.5f;
        }

        layoutPuck();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (mode_ != InteractionMode::Hybrid)
            return;

        auto p = e.position;

        if (puckMinBtn_.contains(p))
        {
            puckMinimized_ = !puckMinimized_;
            repaint();
            return;
        }

        if (puckHandle_.contains(p))
        {
            draggingPuck_ = true;
            puckDragOff_  = p - juce::Point<float>((float) puck_.getX(), (float) puck_.getY());
            return;
        }

        if (puckMinimized_)
            return;

        if (puckBtnR_.contains(p))
        {
            if (onRightClickRequested)
                onRightClickRequested();
            return;
        }

        if (puckBtnS_.contains(p))
        {
            scrollHold_ = !scrollHold_;
            if (onScrollToggled)
                onScrollToggled();
            repaint();
            return;
        }

        if (puckPad_.contains(p))
        {
            padActive_ = true;
            padLastX_  = p.x;
            padLastY_  = p.y;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (mode_ != InteractionMode::Hybrid)
            return;

        auto p = e.position;

        if (draggingPuck_)
        {
            auto np = p - puckDragOff_;
            puck_.setPosition(
                juce::jlimit(0, juce::jmax(0, getWidth()  - puck_.getWidth()),  (int) np.x),
                juce::jlimit(0, juce::jmax(0, getHeight() - puck_.getHeight()), (int) np.y));
            layoutPuck();
            repaint();
            return;
        }

        if (padActive_ && !scrollHold_)
        {
            float dx = (p.x - padLastX_) * sensitivity_;
            float dy = (p.y - padLastY_) * sensitivity_;
            padLastX_ = p.x;
            padLastY_ = p.y;
            moveCursor(dx, dy);
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingPuck_ = false;
        padActive_    = false;
    }

private:
    InteractionMode mode_        = InteractionMode::Touchscreen;
    float           cursorX_     = 0.f;
    float           cursorY_     = 0.f;
    float           sensitivity_ = 1.35f;

    static constexpr int kPuckW = 130;
    static constexpr int kPuckH = 140;

    juce::Rectangle<int>   puck_ { 0, 0, kPuckW, kPuckH };
    juce::Rectangle<float> puckHandle_;
    juce::Rectangle<float> puckPad_;
    juce::Rectangle<float> puckBtnR_;
    juce::Rectangle<float> puckBtnS_;
    juce::Rectangle<float> puckMinBtn_;

    bool puckMinimized_ = false;
    bool draggingPuck_  = false;
    bool padActive_     = false;
    bool scrollHold_    = false;
    juce::Point<float> puckDragOff_;
    float padLastX_ = 0.f;
    float padLastY_ = 0.f;

    void layoutPuck()
    {
        float x = (float) puck_.getX(), y = (float) puck_.getY();
        float w = (float) puck_.getWidth();
        float h = (float) puck_.getHeight();
        float hd = 32.f, pad = 8.f, btnSz = 28.f;

        puckHandle_.setBounds(x, y, w, hd);
        puckMinBtn_.setBounds(x + w - 22.f, y + 6.f, 16.f, 20.f);

        if (!puckMinimized_)
        {
            float padW = w - pad * 3.f - btnSz;
            float padH = h - hd - pad * 2.f;
            puckPad_.setBounds(x + pad, y + hd + pad, padW, padH);
            puckBtnR_.setBounds(x + pad * 2.f + padW, y + hd + pad, btnSz, btnSz);
            puckBtnS_.setBounds(x + pad * 2.f + padW, y + hd + pad + btnSz + pad, btnSz, btnSz);
        }
    }

    void drawPuck(juce::Graphics& g)
    {
        auto& t = Theme::getInstance();
        auto  b = puck_.toFloat();

        if (puckMinimized_)
        {
            g.setColour(t.colors.surface.withAlpha(0.92f));
            g.fillRoundedRectangle(puckHandle_, 8.f);
            g.setColour(t.colors.accent);
            g.drawRoundedRectangle(puckHandle_, 8.f, 1.5f);
            g.setColour(t.colors.text);
            g.setFont(t.fonts.small);
            g.drawText("PAD", puckHandle_, juce::Justification::centred);
            drawMinBtn(g);
            return;
        }

        g.setColour(t.colors.surface.withAlpha(0.93f));
        g.fillRoundedRectangle(b, 10.f);
        g.setColour(t.colors.accent.withAlpha(0.65f));
        g.drawRoundedRectangle(b, 10.f, 1.5f);

        g.setColour(t.colors.backgroundDark.withAlpha(0.85f));
        g.fillRoundedRectangle(puckHandle_, 10.f);
        g.fillRect(puckHandle_.withTrimmedTop(puckHandle_.getHeight() * 0.5f));
        g.setColour(t.colors.text);
        g.setFont(t.fonts.small);
        g.drawText("PAD", puckHandle_, juce::Justification::centred);
        drawMinBtn(g);

        g.setColour(t.colors.backgroundDark.withAlpha(0.65f));
        g.fillRoundedRectangle(puckPad_, 6.f);
        g.setColour(t.colors.border.withAlpha(0.55f));
        g.drawRoundedRectangle(puckPad_, 6.f, 1.f);
        g.setColour(t.colors.textSecondary);
        g.setFont(t.fonts.small);
        g.drawText("drag to move", puckPad_, juce::Justification::centred);

        auto drawBtn = [&](juce::Rectangle<float> r, const juce::String& lbl, bool on, juce::Colour col)
        {
            g.setColour(on ? col : col.withAlpha(0.35f));
            g.fillRoundedRectangle(r, 4.f);
            g.setColour(juce::Colours::white);
            g.setFont(t.fonts.bold);
            g.drawText(lbl, r, juce::Justification::centred);
        };

        drawBtn(puckBtnR_, "R", false, t.colors.transportRecord);
        drawBtn(puckBtnS_, "S", scrollHold_, t.colors.accent);
    }

    void drawMinBtn(juce::Graphics& g)
    {
        auto& t = Theme::getInstance();
        g.setColour(t.colors.textSecondary.withAlpha(0.7f));
        g.setFont(t.fonts.small);
        g.drawText(puckMinimized_ ? "+" : "-", puckMinBtn_, juce::Justification::centred);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirtualCursor)
};

} // namespace DAW
