#pragma once
#include <JuceHeader.h>
#include "BubblegumTaskbarCore.h"
#include "BubblegumTaskbarChipLayoutCore.h"
#include "BubblegumTaskbarChipRenderer.h"

namespace DAW {

/**
 * BubblegumTaskbarComponent
 *
 * The visible strip that lives at the bottom of the DAW shell. Listens to
 * BubblegumTaskbarCore for chip-list changes and repaints accordingly. Owns
 * the layout and renderer instances; they are stateless config holders.
 *
 * Mouse routing:
 *   - click on chip's X area  -> Core::onCloseClicked  -> host->closeFromTaskbar
 *   - click anywhere else     -> Core::onChipClicked   -> toggle restore/minimize
 *   - mouse move              -> updates per-chip hover flag, triggers repaint
 *
 * Place one instance in the shell window. It paints itself transparent over
 * its background colour — the host can layer it on whatever strip surface
 * the DAW already has.
 */
class BubblegumTaskbarComponent : public juce::Component,
                                  private BubblegumTaskbarCore::Listener
{
public:
    struct Config
    {
        juce::Colour stripBackground   { 0xFF06080D };
        juce::Colour topAccentLine     { 0x40FF4F8A };
        juce::Colour titleInk          { 0xFF5B6075 };

        float topAccentLineHeight      { 0.8f  };
        float titleX                   { 20.0f };
        float titleY                   { 8.0f  };
        float titleW                   { 200.0f };
        float titleH                   { 14.0f };
        float titleFontSize            { 11.0f };

        bool  paintBackground          { true };
        bool  paintTitle               { true };
    };

    BubblegumTaskbarComponent()
    {
        BubblegumTaskbarCore::getGlobalInstance().addListener(this);
        setInterceptsMouseClicks(true, false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    ~BubblegumTaskbarComponent() override
    {
        BubblegumTaskbarCore::getGlobalInstance().removeListener(this);
    }

    void setConfig(const Config& c)        { config_ = c; repaint(); }
    const Config& getConfig() const        { return config_; }

    BubblegumTaskbarChipLayoutCore&  getLayout()   { return layout_; }
    BubblegumTaskbarChipRenderer&    getRenderer() { return renderer_; }

    void paint(juce::Graphics& g) override
    {
        if (config_.paintBackground)
        {
            g.fillAll(config_.stripBackground);
            g.setColour(config_.topAccentLine);
            g.fillRect(0.0f, 0.0f,
                       static_cast<float>(getWidth()),
                       config_.topAccentLineHeight);
        }

        if (config_.paintTitle)
        {
            g.setColour(config_.titleInk);
            g.setFont(juce::Font(config_.titleFontSize));
            g.drawText("Bubblegum taskbar",
                       juce::Rectangle<float>(config_.titleX, config_.titleY,
                                              config_.titleW, config_.titleH),
                       juce::Justification::centredLeft);
        }

        auto& chips = BubblegumTaskbarCore::getGlobalInstance().getChips();
        layout_.layout(chips);
        for (const auto& chip : chips)
            renderer_.paint(g, chip);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto pos = e.position;
        auto& chips = BubblegumTaskbarCore::getGlobalInstance().getChips();
        bool changed = false;

        for (auto& chip : chips)
        {
            const bool wasHovered = chip.isHovered;
            chip.isHovered = chip.bounds.contains(pos);
            if (chip.isHovered != wasHovered) changed = true;
        }

        if (changed) repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        auto& chips = BubblegumTaskbarCore::getGlobalInstance().getChips();
        bool changed = false;
        for (auto& chip : chips)
        {
            if (chip.isHovered) { chip.isHovered = false; changed = true; }
        }
        if (changed) repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto pos = e.position;
        auto& core  = BubblegumTaskbarCore::getGlobalInstance();
        auto& chips = core.getChips();

        for (const auto& chip : chips)
        {
            if (! chip.bounds.contains(pos)) continue;

            const bool clickIsClose = (chip.isActive || chip.isHovered)
                                   && renderer_.hitsCloseButton(pos, chip.bounds);

            if (clickIsClose) core.onCloseClicked(chip.host);
            else              core.onChipClicked(chip.host);
            return;
        }
    }

private:
    void taskbarChipsChanged() override { repaint(); }

    Config                          config_;
    BubblegumTaskbarChipLayoutCore  layout_;
    BubblegumTaskbarChipRenderer    renderer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumTaskbarComponent)
};

} // namespace DAW
