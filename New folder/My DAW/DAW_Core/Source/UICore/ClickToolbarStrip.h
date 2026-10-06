#pragma once
#include <JuceHeader.h>
#include "../ClickCore/ClickStateModel.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

class ClickToolbarStrip : public juce::Component
{
public:
    explicit ClickToolbarStrip(ClickStateModel& state)
        : state_(state)
    {
        startTimerHz(12);
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        const auto mode = state_.getActiveMode();
        const auto bars = state_.getCountInBars();
        const bool muted = state_.muted.load(std::memory_order_relaxed);

        juce::Colour clickColour = mode == ClickActiveMode::Off ? t.colors.controlIdle
            : mode == ClickActiveMode::OnDuringRecord ? juce::Colour(0xFFEAB308)
            : juce::Colour(0xFF22C55E);
        if (muted) clickColour = juce::Colour(0xFF666666);

        g.setColour(clickColour);
        g.fillRoundedRectangle(clickBounds_.toFloat(), 4.0f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(clickBounds_.toFloat(), 4.0f, 1.0f);
        g.setColour(juce::Colours::white);
        g.setFont(t.fonts.small);
        g.drawText(muted ? "Mute" : mode == ClickActiveMode::Off ? "Click" : mode == ClickActiveMode::OnDuringRecord ? "Rec" : "All",
                   clickBounds_, juce::Justification::centred);

        g.setColour(t.colors.controlIdle);
        g.fillRoundedRectangle(countBounds_.toFloat(), 4.0f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(countBounds_.toFloat(), 4.0f, 1.0f);
        g.setColour(t.colors.text);
        g.drawText(bars == ClickCountInBars::None ? "0" : juce::String((int) bars) + "b",
                   countBounds_, juce::Justification::centred);

        auto volArea = volumeBounds_.toFloat();
        g.setColour(t.colors.controlIdle);
        g.fillRoundedRectangle(volArea, 3.0f);
        const float v = juce::jlimit(0.0f, 1.0f, state_.volumeLinear.load(std::memory_order_relaxed));
        g.setColour(t.colors.accent.withAlpha(0.8f));
        g.fillRoundedRectangle(volArea.withWidth(volArea.getWidth() * v), 3.0f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(volArea, 3.0f, 1.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        clickBounds_ = b.removeFromLeft(42).reduced(1, 4);
        countBounds_ = b.removeFromLeft(30).reduced(1, 4);
        volumeBounds_ = b.removeFromLeft(54).reduced(3, 12);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (clickBounds_.contains(e.getPosition()))
        {
            if (e.mods.isRightButtonDown())
            {
                state_.muted.store(!state_.muted.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            else
            {
                const int next = ((int) state_.getActiveMode() + 1) % 3;
                state_.activeMode.store(next, std::memory_order_relaxed);
            }
            repaint();
        }
        else if (countBounds_.contains(e.getPosition()))
        {
            const int current = (int) state_.getCountInBars();
            const int next = current == 0 ? 1 : current == 1 ? 2 : current == 2 ? 4 : 0;
            state_.countInBars.store(next, std::memory_order_relaxed);
            repaint();
        }
        dragStartVolume_ = state_.volumeLinear.load(std::memory_order_relaxed);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!volumeBounds_.contains(e.getMouseDownPosition())) return;
        const float v = juce::jlimit(0.0f, 1.0f, dragStartVolume_ + (float) e.getDistanceFromDragStartX() * 0.01f);
        state_.volumeLinear.store(v, std::memory_order_relaxed);
        repaint();
    }

private:
    class Timer final : public juce::Timer {};
    void startTimerHz(int hz) { timer_.owner = this; timer_.startTimerHz(hz); }
    struct RepaintTimer final : public juce::Timer
    {
        ClickToolbarStrip* owner = nullptr;
        void timerCallback() override { if (owner) owner->repaint(); }
    } timer_;

    ClickStateModel& state_;
    juce::Rectangle<int> clickBounds_;
    juce::Rectangle<int> countBounds_;
    juce::Rectangle<int> volumeBounds_;
    float dragStartVolume_ { 0.6f };
};

} // namespace DAW
