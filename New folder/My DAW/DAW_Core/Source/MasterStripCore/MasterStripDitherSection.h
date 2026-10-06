#pragma once
#include <JuceHeader.h>
#include "MasterStripDitherBinding.h"
#include "BubblegumInfoPopupCore.h"
#include "MasterStripFeatureInfoCatalog.h"

namespace DAW {

/**
 * MasterStripDitherSection
 *
 * UI section for the dither controls. Lives on the BACK side of the master
 * strip (mastering view). Same cream-faced visual language as the ceiling
 * section.
 *
 * Layout:
 *
 *   ┌─────────────────────────────┐
 *   │ DITHER                      │  ← title label
 *   │ ┌──────────┐ ┌──────────┐  │
 *   │ │ TPDF     │ │ 24-bit   │  │  ← mode dropdown / bits dropdown
 *   │ └──────────┘ └──────────┘  │
 *   └─────────────────────────────┘
 *
 * Click on the mode box → popup with Off / TPDF / Noise Shaped.
 * Click on the bits box → popup with 16 / 20 / 24.
 *
 * Threading: UI thread only. No timer needed (no real-time meters).
 */
class MasterStripDitherSection : public juce::Component
{
public:
    MasterStripDitherSection()
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, false);
        configureHelpButton();
        addAndMakeVisible(helpBtn_);
    }

    void setBinding(MasterStripDitherBinding* binding)
    {
        binding_ = binding;
        repaint();
    }

    // ── Component ────────────────────────────────────────────────────

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        // Cream face background
        juce::ColourGradient cream(
            juce::Colour(0xFFF4ECD7), bounds.getX(), bounds.getY(),
            juce::Colour(0xFFCFC2A4), bounds.getX(), bounds.getBottom(), false);
        cream.addColour(0.55, juce::Colour(0xFFE8DEC7));
        g.setGradientFill(cream);
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds, 6.0f, 0.5f);

        // Title
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("DITHER", bounds.toNearestInt().reduced(8, 4),
                   juce::Justification::topLeft, false);

        if (binding_ == nullptr || !binding_->isBound())
        {
            g.setColour(juce::Colours::black.withAlpha(0.3f));
            g.drawText("not bound", bounds.toNearestInt(),
                       juce::Justification::centred, false);
            return;
        }

        // Mode box (left)
        g.setColour(juce::Colour(0xFF0F1219));
        g.fillRoundedRectangle(modeBoxBounds_.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xFFFFB3D0));
        g.setFont(juce::Font(9.0f, juce::Font::plain));
        g.drawText(MasterStripDitherBinding::labelForMode(binding_->getMode()),
                   modeBoxBounds_, juce::Justification::centred, false);

        // Bits box (right)
        g.setColour(juce::Colour(0xFF0F1219));
        g.fillRoundedRectangle(bitsBoxBounds_.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xFFD8DCE6));
        g.drawText(MasterStripDitherBinding::labelForBits(binding_->getTargetBits()),
                   bitsBoxBounds_, juce::Justification::centred, false);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8, 4);

        const int headerH = 14;
        const int helpBtnSize = juce::jmax(10, juce::jmin(16, headerH - 2));
        helpBtn_.setBounds(bounds.getRight() - helpBtnSize - 2,
                           bounds.getY() + (headerH - helpBtnSize) / 2,
                           helpBtnSize, helpBtnSize);

        bounds.removeFromTop(14);

        auto boxRow = bounds.removeFromTop(18);
        const int gap = 6;
        const int boxW = (boxRow.getWidth() - gap) / 2;
        modeBoxBounds_ = boxRow.removeFromLeft(boxW);
        boxRow.removeFromLeft(gap);
        bitsBoxBounds_ = boxRow;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (binding_ == nullptr || !binding_->isBound()) return;

        if (modeBoxBounds_.contains(e.getPosition()))
        {
            showModePopup();
        }
        else if (bitsBoxBounds_.contains(e.getPosition()))
        {
            showBitsPopup();
        }
    }

private:
    void configureHelpButton()
    {
        helpBtn_.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFFFF4F8A).withAlpha(0.20f));
        helpBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFF4F8A).withAlpha(0.45f));
        helpBtn_.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xFFFF4F8A));
        helpBtn_.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        helpBtn_.onClick = [this]
        {
            BubblegumInfoPopup::showFor(&helpBtn_, MasterStripFeatureInfoCatalog::dither());
        };
    }

    void showModePopup()
    {
        if (binding_ == nullptr) return;

        juce::PopupMenu menu;
        const auto labels = MasterStripDitherBinding::getAllModeLabels();
        const int currentIdx = MasterStripDitherBinding::indexFromMode(binding_->getMode());
        for (int i = 0; i < labels.size(); ++i)
            menu.addItem(i + 1, labels[i], true, i == currentIdx);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int result)
            {
                if (result <= 0 || binding_ == nullptr) return;
                binding_->setMode(MasterStripDitherBinding::modeFromIndex(result - 1));
                repaint();
            });
    }

    void showBitsPopup()
    {
        if (binding_ == nullptr) return;

        juce::PopupMenu menu;
        const auto bitDepths = MasterStripDitherBinding::getCommonBitDepths();
        const int currentBits = binding_->getTargetBits();
        for (int i = 0; i < bitDepths.size(); ++i)
        {
            const int b = bitDepths[i];
            menu.addItem(i + 1, MasterStripDitherBinding::labelForBits(b),
                         true, b == currentBits);
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this, bitDepths](int result)
            {
                if (result <= 0 || binding_ == nullptr) return;
                binding_->setTargetBits(bitDepths[result - 1]);
                repaint();
            });
    }

    MasterStripDitherBinding* binding_ { nullptr };
    juce::TextButton helpBtn_ { "?" };

    juce::Rectangle<int> modeBoxBounds_;
    juce::Rectangle<int> bitsBoxBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStripDitherSection)
};

} // namespace DAW
