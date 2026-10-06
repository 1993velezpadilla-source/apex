#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../FloatingWindowCore/FloatingWindowBase.h"
#include "../ShortcutCore/ShortcutEngineCore.h"

namespace DAW {

// ─── ShortcutCategoryRenderer ─────────────────────────────────────────────────
// Renders grouped shortcut entries inside a scrollable list component.
// Each category has a collapsible header; entries show command + shortcut.

class ShortcutHelpListComponent : public juce::Component
{
public:
    ShortcutHelpListComponent() = default;

    /** Reload the list from grouped results. */
    void setResults(const std::vector<ShortcutSearchEngine::FilteredResult>& results)
    {
        results_ = results;
        updateHeight();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto& psm = PlatformStyleManager::getInstance();
        bool apple = psm.isAppleGUI();
        float corner = apple ? 8.f : 3.f;
        float y = 0.f;

        for (size_t ci = 0; ci < results_.size(); ++ci)
        {
            auto& group = results_[ci];
            bool collapsed = collapsedCategories_.count((int)ci) > 0;

            // Category header bar
            auto headerR = juce::Rectangle<float>(0.f, y, (float)getWidth(), kCategoryH);
            g.setColour(t.colors.surface.withAlpha(apple ? 0.70f : 0.85f));
            g.fillRoundedRectangle(headerR.reduced(2.f, 0.f), corner);
            if (apple)
            {
                g.setColour(t.colors.accent.withAlpha(0.08f));
                g.drawRoundedRectangle(headerR.reduced(2.f, 0.f), corner, 0.5f);
            }

            // Collapse arrow
            g.setColour(t.colors.accent);
            auto arrowR = headerR.withWidth(20.f).translated(6.f, 0.f);
            g.setFont(juce::Font(12.f, juce::Font::bold));
            g.drawText(collapsed ? ">" : "v", arrowR, juce::Justification::centredLeft);

            // Category name
            g.setColour(t.colors.accent);
            g.setFont(t.fonts.bold);
            g.drawText(group.category, headerR.withTrimmedLeft(26.f),
                       juce::Justification::centredLeft);

            // Entry count badge
            g.setColour(t.colors.textSecondary);
            g.setFont(t.fonts.small);
            g.drawText(juce::String((int)group.entries.size()),
                       headerR.withTrimmedRight(10.f), juce::Justification::centredRight);

            y += kCategoryH;

            if (collapsed) continue;

            // Shortcut entries
            for (auto& entry : group.entries)
            {
                auto rowR = juce::Rectangle<float>(0.f, y, (float)getWidth(), kEntryH);

                // Alternate row shading
                bool even = ((int)(y / kEntryH)) % 2 == 0;
                if (even)
                {
                    g.setColour(t.colors.backgroundDark.withAlpha(apple ? 0.15f : 0.3f));
                    g.fillRect(rowR);
                }

                // Hover highlight
                if (rowR.contains(lastMouse_))
                {
                    g.setColour(t.colors.accent.withAlpha(apple ? 0.06f : 0.08f));
                    g.fillRoundedRectangle(rowR.reduced(2.f, 0.f), apple ? 4.f : 0.f);
                }

                // Command name (left)
                g.setColour(t.colors.text);
                g.setFont(t.fonts.regular);
                g.drawText(entry.command, rowR.withTrimmedLeft(30.f).withTrimmedRight(getWidth() * 0.4f),
                           juce::Justification::centredLeft, true);

                // Dotted separator
                float dotStartX = getWidth() * 0.55f;
                float dotEndX   = getWidth() * 0.62f;
                g.setColour(t.colors.textSecondary.withAlpha(apple ? 0.15f : 0.25f));
                for (float dx = dotStartX; dx < dotEndX; dx += 4.f)
                    g.fillEllipse(dx, y + kEntryH * 0.5f - 0.5f, 1.5f, 1.5f);

                // Shortcut key (right, monospace) — platform-style translated
                juce::String displayKey = psm.translateShortcut(entry.shortcut);
                if (apple)
                {
                    // Apple style: show shortcut in a rounded capsule
                    auto keyR = rowR.withLeft((float)getWidth() * 0.64f).withTrimmedRight(8.f);
                    g.setColour(t.colors.surface.withAlpha(0.5f));
                    g.fillRoundedRectangle(keyR.reduced(0.f, 2.f), 4.f);
                    g.setColour(t.colors.accent);
                    g.setFont(t.fonts.mono);
                    g.drawText(displayKey, keyR, juce::Justification::centred, true);
                }
                else
                {
                    // Windows style: plain right-aligned monospace text
                    g.setColour(t.colors.accent.brighter(0.2f));
                    g.setFont(t.fonts.mono);
                    g.drawText(displayKey, rowR.withTrimmedRight(12.f),
                               juce::Justification::centredRight, true);
                }

                y += kEntryH;
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Check if click is on a category header for collapse/expand
        float y = 0.f;
        for (size_t ci = 0; ci < results_.size(); ++ci)
        {
            auto headerR = juce::Rectangle<float>(0.f, y, (float)getWidth(), kCategoryH);
            if (headerR.contains(e.position))
            {
                int idx = (int)ci;
                if (collapsedCategories_.count(idx))
                    collapsedCategories_.erase(idx);
                else
                    collapsedCategories_.insert(idx);
                updateHeight();
                repaint();
                return;
            }
            y += kCategoryH;
            bool collapsed = collapsedCategories_.count((int)ci) > 0;
            if (!collapsed)
                y += results_[ci].entries.size() * kEntryH;
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        lastMouse_ = e.position;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        lastMouse_ = { -1.f, -1.f };
        repaint();
    }

private:
    static constexpr float kCategoryH = 30.f;
    static constexpr float kEntryH    = 24.f;

    std::vector<ShortcutSearchEngine::FilteredResult> results_;
    std::set<int> collapsedCategories_;
    juce::Point<float> lastMouse_ { -1.f, -1.f };

    void updateHeight()
    {
        float h = 0.f;
        for (size_t ci = 0; ci < results_.size(); ++ci)
        {
            h += kCategoryH;
            if (collapsedCategories_.count((int)ci) == 0)
                h += results_[ci].entries.size() * kEntryH;
        }
        setSize(getWidth(), juce::jmax(1, (int)std::ceil(h)));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutHelpListComponent)
};

// ─── ShortcutHelpFloatingWindow ───────────────────────────────────────────────
// Premium floating window showing all shortcuts for the active DAW profile.
// Features: search bar, scrollable list, category collapse, pin/always-on-top,
// copy-to-clipboard button, close button. Inherits drag/resize/snap from base.

class ShortcutHelpFloatingWindow : public FloatingWindowBase,
                                   public ShortcutProfileManager::Listener,
                                   public PlatformStyleManager::Listener
{
public:
    static constexpr int kMinTopY   = 28;
    static constexpr int kSearchH   = 34;
    static constexpr int kToolbarH  = 28;
    static constexpr int kDefaultW  = 520;
    static constexpr int kDefaultH  = 560;

    ShortcutHelpFloatingWindow()
        : FloatingWindowBase("Shortcuts")
    {
        // Create ALL child components BEFORE calling setSize,
        // because setSize triggers resized() -> layoutContent().

        // Search field
        searchField_ = std::make_unique<juce::TextEditor>();
        searchField_->setMultiLine(false);
        searchField_->setTextToShowWhenEmpty("Search shortcuts...",
            Theme::getInstance().colors.textSecondary);
        searchField_->setFont(Theme::getInstance().fonts.regular);
        searchField_->setColour(juce::TextEditor::backgroundColourId,
            Theme::getInstance().colors.backgroundDark);
        searchField_->setColour(juce::TextEditor::textColourId,
            Theme::getInstance().colors.text);
        searchField_->setColour(juce::TextEditor::outlineColourId,
            Theme::getInstance().colors.border);
        searchField_->setColour(juce::TextEditor::focusedOutlineColourId,
            Theme::getInstance().colors.accent);
        searchField_->onTextChange = [this]() { applyFilter(); };
        searchField_->setWantsKeyboardFocus(true);
        addAndMakeVisible(searchField_.get());

        // Scrollable list viewport
        listComponent_ = std::make_unique<ShortcutHelpListComponent>();
        viewport_ = std::make_unique<juce::Viewport>();
        viewport_->setViewedComponent(listComponent_.get(), false);
        viewport_->setScrollBarsShown(true, false);
        viewport_->setScrollBarThickness(8);
        addAndMakeVisible(viewport_.get());

        // NOW safe to set size (triggers layoutContent)
        setSize(kDefaultW, kDefaultH);

        // Register for profile and platform style changes
        ShortcutProfileManager::getInstance().addListener(this);
        PlatformStyleManager::getInstance().addListener(this);

        // Initial load
        refreshFromProfile();

        // Restore persisted position
        auto savedBounds = ShortcutPersistenceManager::getInstance().loadHelpWindowBounds();
        if (!savedBounds.isEmpty())
            setBounds(savedBounds);
    }

    ~ShortcutHelpFloatingWindow() override
    {
        ShortcutProfileManager::getInstance().removeListener(this);
        PlatformStyleManager::getInstance().removeListener(this);
        // Save position
        ShortcutPersistenceManager::getInstance().saveHelpWindowBounds(getBounds());
    }

    // ── FloatingWindowBase ───────────────────────────────────────────────────

    void paint(juce::Graphics& g) override
    {
        FloatingWindowBase::paint(g);

        if (isEmbedded()) return;

        auto& t = Theme::getInstance();
        bool apple = PlatformStyleManager::getInstance().isAppleGUI();
        float corner = apple ? 10.f : 3.f;

        // ── Apple GUI: subtle glass overlay on content area ──────────────
        if (apple)
        {
            auto ca = getContentArea().toFloat();
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.fillRoundedRectangle(ca, 8.f);
        }

        // Toolbar area below title: profile name badge + match count + pin/copy
        auto toolbarR = getContentArea().removeFromTop(kToolbarH).toFloat();
        g.setColour(t.colors.surface.withAlpha(apple ? 0.35f : 0.5f));
        g.fillRect(toolbarR);
        g.setColour(t.colors.border.withAlpha(apple ? 0.15f : 0.3f));
        g.fillRect(toolbarR.getX(), toolbarR.getBottom() - 1.f,
                   toolbarR.getWidth(), 1.f);

        // Profile badge
        g.setColour(t.colors.accent.withAlpha(apple ? 0.10f : 0.15f));
        auto badgeR = toolbarR.withTrimmedLeft(10.f).withWidth(140.f).reduced(0.f, 4.f);
        g.fillRoundedRectangle(badgeR, corner);
        if (apple)
        {
            g.setColour(t.colors.accent.withAlpha(0.3f));
            g.drawRoundedRectangle(badgeR, corner, 0.5f);
        }
        g.setColour(t.colors.accent);
        g.setFont(t.fonts.small);
        g.drawText(ShortcutProfileManager::getInstance().getActiveProfile().displayName,
                   badgeR, juce::Justification::centred);

        // Match count
        g.setColour(t.colors.textSecondary);
        g.setFont(t.fonts.small);
        g.drawText(juce::String(matchCount_) + " shortcuts",
                   toolbarR.withTrimmedRight(80.f), juce::Justification::centredRight);

        // Pin button
        auto pinR = pinBtnRect();
        bool pinHov = pinR.contains(getMouseXYRelative().toFloat());
        g.setColour(pinned_ ? t.colors.accent : (pinHov ? t.colors.surfaceHover : t.colors.surface));
        g.fillRoundedRectangle(pinR, corner);
        g.setColour(pinned_ ? juce::Colours::white : t.colors.textSecondary);
        g.setFont(juce::Font(12.f));
        g.drawText("P", pinR, juce::Justification::centred);

        // Copy button
        auto copyR = copyBtnRect();
        bool copyHov = copyR.contains(getMouseXYRelative().toFloat());
        g.setColour(copyHov ? t.colors.surfaceHover : t.colors.surface);
        g.fillRoundedRectangle(copyR, corner);
        g.setColour(t.colors.textSecondary);
        g.setFont(juce::Font(12.f));
        g.drawText("C", copyR, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Pin button
        if (pinBtnRect().contains(e.position))
        {
            pinned_ = !pinned_;
            setAlwaysOnTop(pinned_);
            repaint();
            return;
        }
        // Copy button
        if (copyBtnRect().contains(e.position))
        {
            copyShortcutsToClipboard();
            return;
        }
        FloatingWindowBase::mouseDown(e);
    }

    // ── Keyboard handling — eat keys while search field is focused ────────
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (searchField_->hasKeyboardFocus(false))
            return false; // let TextEditor handle it
        // Arrow keys for scrolling
        if (key == juce::KeyPress::upKey)
        { viewport_->setViewPosition(0, juce::jmax(0, viewport_->getViewPositionY() - 30)); return true; }
        if (key == juce::KeyPress::downKey)
        { viewport_->setViewPosition(0, viewport_->getViewPositionY() + 30); return true; }
        if (key == juce::KeyPress::pageUpKey)
        { viewport_->setViewPosition(0, juce::jmax(0, viewport_->getViewPositionY() - viewport_->getHeight())); return true; }
        if (key == juce::KeyPress::pageDownKey)
        { viewport_->setViewPosition(0, viewport_->getViewPositionY() + viewport_->getHeight()); return true; }
        if (key == juce::KeyPress::homeKey)
        { viewport_->setViewPosition(0, 0); return true; }
        if (key == juce::KeyPress::endKey)
        { viewport_->setViewPosition(0, listComponent_->getHeight()); return true; }
        if (key == juce::KeyPress::escapeKey)
        { if (onCloseClicked) onCloseClicked(); return true; }
        return false;
    }

    // ── Listener callbacks ───────────────────────────────────────────────────

    void shortcutProfileChanged(const ShortcutProfile&) override { refreshFromProfile(); }
    void platformStyleChanged() override { repaint(); if (listComponent_) listComponent_->repaint(); }

protected:
    void layoutContent() override
    {
        if (!searchField_ || !viewport_ || !listComponent_) return;

        auto ca = getContentArea();

        // Toolbar (profile badge, pin, copy)
        ca.removeFromTop(kToolbarH);

        // Search bar
        auto searchR = ca.removeFromTop(kSearchH).reduced(8, 4);
        searchField_->setBounds(searchR);

        // Scrollable list fills the rest
        auto listArea = ca.reduced(4, 2);
        viewport_->setBounds(listArea);
        int listW = juce::jmax(1, listArea.getWidth() - viewport_->getScrollBarThickness() - 2);
        listComponent_->setSize(listW, juce::jmax(1, listComponent_->getHeight()));
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        FloatingWindowBase::mouseDrag(e);
        clampTopPosition();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        FloatingWindowBase::mouseUp(e);
        clampTopPosition();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        FloatingWindowBase::mouseMove(e);
        repaint(getContentArea().removeFromTop(kToolbarH)); // toolbar hover
    }

private:
    std::unique_ptr<juce::TextEditor>           searchField_;
    std::unique_ptr<juce::Viewport>             viewport_;
    std::unique_ptr<ShortcutHelpListComponent>  listComponent_;
    bool pinned_     = false;
    int  matchCount_ = 0;

    void refreshFromProfile()
    {
        auto& profile = ShortcutProfileManager::getInstance().getActiveProfile();
        setWindowTitle(profile.displayName + " Shortcuts");
        applyFilter();
    }

    void applyFilter()
    {
        auto query = searchField_->getText().trim();
        auto results = ShortcutEngineCore::getInstance().searchActiveProfile(query);

        matchCount_ = 0;
        for (auto& g : results) matchCount_ += (int)g.entries.size();

        listComponent_->setResults(results);
        // Resize viewed component width to match viewport
        auto vw = viewport_->getWidth() - viewport_->getScrollBarThickness() - 2;
        if (vw > 0) listComponent_->setSize(vw, listComponent_->getHeight());

        repaint();
    }

    juce::Rectangle<float> pinBtnRect() const
    {
        auto ca = getContentArea();
        return juce::Rectangle<float>(
            (float)(ca.getRight() - 58), (float)(ca.getY() + 4), 24.f, 20.f);
    }

    juce::Rectangle<float> copyBtnRect() const
    {
        auto ca = getContentArea();
        return juce::Rectangle<float>(
            (float)(ca.getRight() - 30), (float)(ca.getY() + 4), 24.f, 20.f);
    }

    void copyShortcutsToClipboard()
    {
        auto& profile = ShortcutProfileManager::getInstance().getActiveProfile();
        auto& psm = PlatformStyleManager::getInstance();
        juce::String text;
        text += profile.displayName + " Shortcuts\n";
        text += juce::String::repeatedString("=", 50) + "\n\n";

        auto cats = profile.getCategories();
        for (auto& cat : cats)
        {
            text += cat + "\n";
            text += juce::String::repeatedString("-", cat.length()) + "\n";
            auto entries = profile.getEntriesForCategory(cat);
            for (auto& e : entries)
            {
                juce::String key = psm.translateShortcut(e.shortcut);
                text += "  " + e.command;
                int pad = juce::jmax(1, 35 - e.command.length());
                text += juce::String::repeatedString(".", pad);
                text += " " + key + "\n";
            }
            text += "\n";
        }
        juce::SystemClipboard::copyTextToClipboard(text);
    }

    void clampTopPosition()
    {
        auto bounds = getBounds();
        if (bounds.getY() < kMinTopY)
            setTopLeftPosition(bounds.getX(), kMinTopY);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutHelpFloatingWindow)
};

} // namespace DAW
