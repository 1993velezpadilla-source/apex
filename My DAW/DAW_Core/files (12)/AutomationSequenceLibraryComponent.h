// ============================================================
// AutomationSequenceLibraryComponent.h  — v3
// APEX · Create Sequence  |  Source/AutomationSequence/
//
// Saved-pattern browser.  Header-only inline implementation.
// Opens as a floating DialogWindow when the user clicks "Library"
// in the main Create Sequence dialog.  Lists all .apexseq files
// from userAppData/APEX/Sequences/ with a tiny live curve preview
// for each entry.  Selecting one fires onPatternSelected() with
// the loaded SequenceParams.
// ============================================================
#pragma once
#include <JuceHeader.h>
#include "AutomationSequenceTypes.h"
#include "AutomationSequenceGeneratorCore.h"
#include "AutomationSequenceHistoryCore.h"
#include <functional>
#include <memory>

namespace APEX {
namespace AutomationSeq {

class LibraryComponent final : public juce::Component {
public:
    static constexpr int kComponentW = 460;
    static constexpr int kComponentH = 540;
    static constexpr int kRowH       = 56;

    std::function<void(const SequenceParams&)> onPatternSelected;
    std::function<void()>                       onClose;

    LibraryComponent() {
        setSize(kComponentW, kComponentH);
        refreshList();

        addAndMakeVisible(listBox_);
        listBox_.setModel(&listModel_);
        listBox_.setRowHeight(kRowH);
        listBox_.setColour(juce::ListBox::backgroundColourId,
                            juce::Colour(0xFF15101A));
        listBox_.setColour(juce::ListBox::outlineColourId,
                            juce::Colour(0xFF38273E));

        listModel_.setOwner(this);

        // Buttons
        addAndMakeVisible(btnClose_);
        btnClose_.setButtonText("Close");
        btnClose_.onClick = [this] { if (onClose) onClose(); };

        addAndMakeVisible(btnOpenFolder_);
        btnOpenFolder_.setButtonText("Show in Folder");
        btnOpenFolder_.onClick = [] {
            HistoryCore::getSavedPatternsDirectory()
                .revealToUser();
        };

        addAndMakeVisible(searchBox_);
        searchBox_.setTextToShowWhenEmpty("Search...",
            juce::Colour(0xFF8E7080));
        searchBox_.onTextChange = [this] {
            filterText_ = searchBox_.getText().trim();
            refreshList();
        };
    }

    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xFF1E1525));
        g.fillRoundedRectangle(b, 6.0f);

        // Title bar
        auto title = b.withHeight(34.0f);
        juce::ColourGradient grad(juce::Colour(0xFF2A1F35),
                                    title.getX(), title.getY(),
                                    juce::Colour(0xFF1E1525),
                                    title.getX(), title.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(title, 6.0f);
        g.fillRect(title.withTop(title.getBottom() - 6.0f));

        g.setColour(juce::Colour(0xFFFFAFD2));
        g.setFont(juce::Font("Inter", 13.0f, juce::Font::bold));
        g.drawText("Pattern Library  ·  "
                   + juce::String(patternNames_.size()) + " saved",
                   title.toNearestInt(), juce::Justification::centred);

        // Border
        g.setColour(juce::Colour(0xFF99305A).withAlpha(0.55f));
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.5f);

        // Empty-state message
        if (patternNames_.isEmpty()) {
            g.setColour(juce::Colour(0xFF8E7080));
            g.setFont(12.0f);
            g.drawText("No patterns saved yet. Create a sequence and click Save.",
                       getLocalBounds().withTrimmedTop(80).withTrimmedBottom(80),
                       juce::Justification::centred);
        }
    }

    void resized() override {
        auto b = getLocalBounds().reduced(8);
        b.removeFromTop(34);   // title

        searchBox_.setBounds(b.removeFromTop(24));
        b.removeFromTop(4);

        auto buttonRow = b.removeFromBottom(30);
        btnClose_     .setBounds(buttonRow.removeFromRight(80));
        buttonRow.removeFromRight(6);
        btnOpenFolder_.setBounds(buttonRow.removeFromRight(120));

        b.removeFromBottom(6);
        listBox_.setBounds(b);
    }

    void refreshList() {
        auto all = HistoryCore::listSavedPatterns();
        patternNames_.clear();
        for (auto& info : all) {
            if (filterText_.isEmpty()
                || info.name.containsIgnoreCase(filterText_))
            {
                patternNames_.add(info.name);
            }
        }
        listBox_.updateContent();
        repaint();
    }

    void loadPatternByName(const juce::String& name) {
        SequenceParams p;
        if (HistoryCore::loadPattern(name, p)) {
            HistoryCore::markSeedAsUsed(p.seed);
            if (onPatternSelected) onPatternSelected(p);
        }
    }

    void deletePatternByName(const juce::String& name) {
        auto* parent = getTopLevelComponent();
        auto* aw = new juce::AlertWindow("Delete pattern",
            "Delete \"" + name + "\" permanently?",
            juce::AlertWindow::WarningIcon, parent);
        aw->addButton("Cancel", 0);
        aw->addButton("Delete", 1);
        aw->enterModalState(true,
            juce::ModalCallbackFunction::create(
                [this, name, aw](int result) {
                    std::unique_ptr<juce::AlertWindow> own(aw);
                    if (result == 1) {
                        HistoryCore::deletePattern(name);
                        refreshList();
                    }
                }),
            false);
    }

private:
    // =======================================================
    // Inner ListBoxModel
    // =======================================================
    class PatternListModel final : public juce::ListBoxModel {
    public:
        void setOwner(LibraryComponent* o) { owner_ = o; }

        int getNumRows() override {
            return owner_ ? owner_->patternNames_.size() : 0;
        }

        void paintListBoxItem(int row, juce::Graphics& g,
                              int w, int h, bool selected) override
        {
            if (!owner_ || row < 0 || row >= owner_->patternNames_.size()) return;
            const auto name = owner_->patternNames_[row];

            // Row background
            g.setColour(selected ? juce::Colour(0xFF38273E)
                                 : juce::Colour(0xFF1E1525));
            g.fillRect(0, 0, w, h);

            // Hover/selected accent
            if (selected) {
                g.setColour(juce::Colour(0xFFFF5FA0));
                g.fillRect(0, 0, 3, h);
            }

            // Name
            g.setColour(juce::Colour(0xFFEDDBE6));
            g.setFont(juce::Font("Inter", 12.0f, juce::Font::bold));
            g.drawText(name, 14, 6, w - 200, 18,
                       juce::Justification::centredLeft);

            // Try to load + render a tiny preview curve
            SequenceParams p;
            if (HistoryCore::loadPattern(name, p)) {
                // Sub-label: mode + steps
                g.setColour(juce::Colour(0xFF8E7080));
                g.setFont(juce::Font("Inter", 10.0f, juce::Font::plain));
                const juce::String sub = juce::String(getShapeLabel(p.mode))
                                       + " · " + juce::String(p.numSteps) + " steps";
                g.drawText(sub, 14, 26, w - 200, 14,
                           juce::Justification::centredLeft);

                // Mini curve preview (right side)
                auto previewRect = juce::Rectangle<float>(
                    static_cast<float>(w - 180), 8.0f, 130.0f, h - 16.0f);
                drawMiniCurve(g, previewRect, p);
            }

            // Bottom divider
            g.setColour(juce::Colour(0xFF38273E));
            g.fillRect(0, h - 1, w, 1);
        }

        void listBoxItemClicked(int row, const juce::MouseEvent& e) override {
            if (!owner_ || row < 0 || row >= owner_->patternNames_.size()) return;
            if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
                showContextMenu(row);
            }
        }

        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
            if (!owner_ || row < 0 || row >= owner_->patternNames_.size()) return;
            owner_->loadPatternByName(owner_->patternNames_[row]);
        }

    private:
        LibraryComponent* owner_ = nullptr;

        void showContextMenu(int row) {
            const auto name = owner_->patternNames_[row];
            juce::PopupMenu m;
            m.addItem("Load",   [o=owner_, name] { o->loadPatternByName(name); });
            m.addItem("Delete", [o=owner_, name] { o->deletePatternByName(name); });
            m.showMenuAsync(juce::PopupMenu::Options());
        }

        static void drawMiniCurve(juce::Graphics& g,
                                   juce::Rectangle<float> r,
                                   const SequenceParams& p)
        {
            // Background
            g.setColour(juce::Colour(0xFF15101A));
            g.fillRoundedRectangle(r, 3.0f);

            // Generate the curve (cheap — kPreviewSamples=512 ops)
            const auto curve = GeneratorCore::generate(p);

            // Draw curve as a path
            juce::Path path;
            const int n = static_cast<int>(curve.samples.size());
            bool started = false;
            for (int i = 0; i < n; ++i) {
                const float x = r.getX() + (i / (float)(n - 1)) * r.getWidth();
                const float y = r.getBottom() - 2.0f
                              - curve.samples[i] * (r.getHeight() - 4.0f);
                if (!started) { path.startNewSubPath(x, y); started = true; }
                else            path.lineTo(x, y);
            }
            g.setColour(juce::Colour(0xFFFFAFD2));
            g.strokePath(path, juce::PathStrokeType(1.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Border
            g.setColour(juce::Colour(0xFF38273E));
            g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
        }
    };

    juce::Array<juce::String> patternNames_;
    juce::String              filterText_;
    juce::ListBox             listBox_;
    PatternListModel          listModel_;
    juce::TextEditor          searchBox_;
    juce::TextButton          btnClose_;
    juce::TextButton          btnOpenFolder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryComponent)
};

} // namespace AutomationSeq
} // namespace APEX
