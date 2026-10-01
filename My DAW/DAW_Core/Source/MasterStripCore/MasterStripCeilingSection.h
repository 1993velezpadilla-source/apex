#pragma once
#include <JuceHeader.h>
#include "MasterStripCeilingBinding.h"
#include "BubblegumInfoPopupCore.h"
#include "MasterStripFeatureInfoCatalog.h"

namespace DAW {

/**
 * MasterStripCeilingSection
 *
 * UI section for the ceiling controls. Lives on the BACK side of the
 * master strip (mastering view). Cream-faced look matching the analog VU
 * meter aesthetic from the Input Trim panel — visually signals "analog,
 * trustworthy, finish-quality processing".
 *
 * Layout (within the section's bounds):
 *
 *   ┌─────────────────────────────┐
 *   │ CEILING                     │  ← title label
 *   │ ┌──────────┐ ┌──────────┐  │
 *   │ │ MODE     │ │ −0.3 dB  │  │  ← mode dropdown / dB readout (clickable)
 *   │ └──────────┘ └──────────┘  │
 *   │ GR ▓▓▓▓▓░░░░░░░░  −6.2     │  ← gain reduction bar + numeric
 *   └─────────────────────────────┘
 *
 * Click on the mode box → opens a popup menu with the 4 modes.
 * Click on the dB readout → enters edit mode (numeric input, drag, or scroll).
 *
 * The GR bar is a vertical mirror of the limiter's gain reduction in real
 * time: 0 dB at the right (no reduction), more reduction extends the pink
 * fill leftward. The numeric on the right shows the current GR value.
 *
 * Threading: UI thread only. Driven by a juce::Timer at 30 Hz to read GR
 * values from the binding (which reads from the audio-thread atomic).
 */
class MasterStripCeilingSection : public juce::Component
{
public:
    MasterStripCeilingSection()
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, false);
        configureHelpButton();
        addAndMakeVisible(helpBtn_);
    }

    void setBinding(MasterStripCeilingBinding* binding)
    {
        binding_ = binding;
        repaint();
    }

    void presentationTick()
    {
        if (isShowing()
            && binding_ != nullptr && binding_->isBound()
            && binding_->getMode() == MasterStripCeilingBinding::Mode::LookaheadLimiter)
        {
            repaint(grBarBounds_.expanded(2));
            repaint(grNumericBounds_);
        }
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

        // Subtle dark border
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds, 6.0f, 0.5f);

        // Title
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("CEILING", bounds.toNearestInt().reduced(8, 4),
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
        g.drawText(MasterStripCeilingBinding::labelForMode(binding_->getMode()),
                   modeBoxBounds_, juce::Justification::centred, false);

        // dB readout box (right)
        g.setColour(juce::Colour(0xFF0F1219));
        g.fillRoundedRectangle(dbBoxBounds_.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xFFD8DCE6));
        const auto dbText = juce::String(binding_->getCeilingDb(), 1) + " dB";
        g.drawText(dbText, dbBoxBounds_, juce::Justification::centred, false);

        // GR label
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.setFont(juce::Font(8.0f, juce::Font::plain));
        g.drawText("GR", grLabelBounds_,
                   juce::Justification::centredLeft, false);

        // GR bar
        g.setColour(juce::Colour(0xFF1A1F2A));
        g.fillRoundedRectangle(grBarBounds_.toFloat(), 1.0f);

        const float gr = binding_->getGainReductionDb();   // <= 0
        const float grAbs = juce::jlimit(0.0f, 24.0f, -gr); // 0..24
        const float fillNorm = grAbs / 24.0f;
        const float fillW = grBarBounds_.getWidth() * fillNorm;

        if (fillW > 0.5f)
        {
            g.setColour(juce::Colour(0xFFFF4F8A));
            g.fillRoundedRectangle(juce::Rectangle<float>(
                grBarBounds_.getX(), grBarBounds_.getY(),
                fillW, grBarBounds_.getHeight()), 1.0f);
        }

        // GR numeric readout
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        const auto grText = (gr <= -0.05f)
            ? juce::String(gr, 1)
            : juce::String("0.0");
        g.drawText(grText, grNumericBounds_,
                   juce::Justification::centredRight, false);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8, 4);

        const int headerH = 14;
        const int helpBtnSize = juce::jmax(10, juce::jmin(16, headerH - 2));
        helpBtn_.setBounds(bounds.getRight() - helpBtnSize - 2,
                           bounds.getY() + (headerH - helpBtnSize) / 2,
                           helpBtnSize, helpBtnSize);

        // Title row at top (height 14)
        bounds.removeFromTop(14);

        // Row of two boxes (mode + dB) — height 18
        auto boxRow = bounds.removeFromTop(18);
        const int gap = 6;
        const int boxW = (boxRow.getWidth() - gap) / 2;
        modeBoxBounds_ = boxRow.removeFromLeft(boxW);
        boxRow.removeFromLeft(gap);
        dbBoxBounds_   = boxRow;

        bounds.removeFromTop(8);

        // GR row — label + bar + numeric, height 10
        auto grRow = bounds.removeFromTop(10);
        grLabelBounds_   = grRow.removeFromLeft(16);
        grNumericBounds_ = grRow.removeFromRight(28);
        grRow.removeFromLeft(4);
        grRow.removeFromRight(4);
        grBarBounds_     = grRow.withSizeKeepingCentre(grRow.getWidth(), 6);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (binding_ == nullptr || !binding_->isBound()) return;

        if (modeBoxBounds_.contains(e.getPosition()))
        {
            showModePopup();
        }
        else if (dbBoxBounds_.contains(e.getPosition()))
        {
            startDbDrag(e);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!draggingDb_ || binding_ == nullptr) return;

        const float deltaY = (float)(dragStartY_ - e.getScreenPosition().y);
        const float dbPerPixel = e.mods.isShiftDown() ? 0.05f : 0.2f;
        const float newDb = juce::jlimit(
            MasterStripCeilingBinding::kMinCeilingDb,
            MasterStripCeilingBinding::kMaxCeilingDb,
            dragStartDb_ + deltaY * dbPerPixel);

        binding_->setCeilingDb(newDb);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingDb_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (binding_ == nullptr || !binding_->isBound()) return;
        if (dbBoxBounds_.contains(e.getPosition()))
        {
            // Reset to default -0.1 dB
            binding_->setCeilingDb(-0.1f);
            repaint();
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
            BubblegumInfoPopup::showFor(&helpBtn_, MasterStripFeatureInfoCatalog::ceiling());
        };
    }

    void showModePopup()
    {
        if (binding_ == nullptr) return;

        juce::PopupMenu menu;
        const auto labels = MasterStripCeilingBinding::getAllModeLabels();
        const int currentIdx = MasterStripCeilingBinding::indexFromMode(binding_->getMode());
        for (int i = 0; i < labels.size(); ++i)
            menu.addItem(i + 1, labels[i], true, i == currentIdx);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int result)
            {
                if (result <= 0 || binding_ == nullptr) return;
                binding_->setMode(MasterStripCeilingBinding::modeFromIndex(result - 1));
                repaint();
            });
    }

    void startDbDrag(const juce::MouseEvent& e)
    {
        draggingDb_  = true;
        dragStartY_  = e.getScreenPosition().y;
        dragStartDb_ = binding_->getCeilingDb();
    }

    MasterStripCeilingBinding* binding_ { nullptr };
    juce::TextButton helpBtn_ { "?" };

    juce::Rectangle<int> modeBoxBounds_;
    juce::Rectangle<int> dbBoxBounds_;
    juce::Rectangle<int> grLabelBounds_;
    juce::Rectangle<int> grBarBounds_;
    juce::Rectangle<int> grNumericBounds_;

    bool  draggingDb_  { false };
    int   dragStartY_  { 0 };
    float dragStartDb_ { -0.1f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStripCeilingSection)
};

} // namespace DAW
